/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * A minimally-viable Node-like module loader.
 */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;
const Cu = Components.utils;

Cu.import('resource://gre/modules/Console.jsm');
Cu.import('resource://gre/modules/Services.jsm');

const subScriptLoader = Cc['@mozilla.org/moz/jssubscript-loader;1'].
                        getService(Ci.mozIJSSubScriptLoader);

this.EXPORTED_SYMBOLS = ["ModuleLoader"];

const systemPrincipal = Cc["@mozilla.org/systemprincipal;1"].
                        createInstance(Ci.nsIPrincipal);

// The default list of "global paths" (i.e. search paths for modules
// specified by name rather than path).  The list of global paths for a given
// ModuleLoader depends on the process type, so the ModuleLoader constructor
// clones this list and prepends a process-type-specific path to the clone.
const DEFAULT_GLOBAL_PATHS = [
  // Electron standard modules that are common to both process types.
  'common/api/exports/',

  // Node standard modules, which are copied directly from Node where possible.
  'node/',
];

const windowLoaders = new WeakMap();

/**
 * Construct a module loader (`require()` global function).
 *
 * @param processType {String}
 *        the type of Electron process for which to construct the loader,
 *        either "browser" (i.e. the main process) or "renderer"
 *
 * @param window {DOMWindow}
 *        for a renderer process loader, the window associated with the process
 *
 * @return {ModuleLoader} the module loader
 */

function ModuleLoader(processType, window) {
  const globalPaths = DEFAULT_GLOBAL_PATHS.slice();
  // Prepend a process-type-specific path to the global paths for this loader.
  globalPaths.unshift(processType + '/api/exports/');

  /**
   * Mapping from module IDs (resource: URLs) to module objects.
   *
   * @keys {string} The ID (resource: URL) for the module.
   * @values {object} An object representing the module.
   */
  const modules = new Map();

  /**
   * The module-wide global object, which is available via a reference
   * to the `global` symbol as well as implicitly via the sandbox prototype.
   * This object is shared across all modules loaded by this loader.
   */
  this.global = {
    // Electron assumes these exist on the global object, so we define them.
    // I wonder if it assumes that all window.* properties will exist, in which
    // case we should instead make the window be the global's prototype.
    window: processType === 'renderer' ? window : undefined,
    location: processType === 'renderer' ? window.location : undefined,
    document: processType === 'renderer' ? window.document : undefined,

    // This comes from Console.jsm, and I don't think it's exactly the same
    // as the Console Web API.
    // XXX Replace this with the real Console Web API (or at least ensure
    // that the current version is compatible with it).
    console: console,

    // Other global properties, like `process` and `Buffer`, are defined
    // after we define the require() function, so we can use that function
    // to implement them.

    // XXX Also define setImmediate and clearImmediate.
  };

  /**
   * Add default properties to the module-specific global object.
   *
   * @param  moduleGlobalObj {Object} the module-specific global object
   *                                  to which we add default properties
   * @param  module          {Object} the object that identifies the module
   *                                  and caches its exported symbols
   */
  this.injectModuleGlobals = function(moduleGlobalObj, module) {
    moduleGlobalObj.exports = module.exports;
    moduleGlobalObj.module = module;
    moduleGlobalObj.require = this.require.bind(this, module);
    moduleGlobalObj.global = this.global;

    if (processType === 'renderer') {
      moduleGlobalObj.window = window;
      moduleGlobalObj.document = window.document;
      // We shouldn't really inject this into every module, and we won't
      // actually need it once we make web-view depend on mozbrowser.
      // But we inject it here for now to work around a web-view error.
      // XXX Remove this after modifying web-view to use mozbrowser.
      moduleGlobalObj.HTMLObjectElement = window.HTMLObjectElement;
    }
  };

  /**
   * Import a module.
   *
   * @param  requirer {Object} the module importing this module.
   * @param  path     {string} the path to the module being imported.
   * @return          {Object} an `exports` object.
   */
  this.require = function(requirer, path) {
    // dump('require: ' + requirer.id + ' requires ' + path + '\n');

    if (path === 'native_module') {
      return this;
    }

    let uri, file;

    if (path.indexOf("://") !== -1) {
      // A URL.
      uri = Services.io.newURI(path, null, null);
      file = uri.QueryInterface(Ci.nsIFileURL).file;
    } else if (path.indexOf('/') === 0) {
      // An absolute path.
      // XXX Figure out if this is a filesystem path rather than a package path
      // and should be converted to a file: URL instead of a resource: URL.
      uri = Services.io.newURI('resource:///modules' + path + '.js', null, null);
      file = uri.QueryInterface(Ci.nsIFileURL).file;
    } else {
      // A relative path or built-in module name.
      let baseURI = Services.io.newURI(requirer.id, null, null);
      uri = Services.io.newURI(path + '.js', null, baseURI);
      file = uri.QueryInterface(Ci.nsIFileURL).file;

      if (!file.exists() && path.indexOf('/') === -1) {
        // It isn't a relative path; perhaps it's a built-in module name.
        for (let globalPath of globalPaths) {
          uri = Services.io.newURI('resource:///modules/' + globalPath + path + '.js', null, null);
          file = uri.QueryInterface(Ci.nsIFileURL).file;
          if (file.exists()) {
            break;
          }
        }
      }
    }

    if (!file.exists()) {
      throw new Error(`No such module: ${path} (required by ${requirer.id})`);
    }

    // dump('require: module found at ' + uri.spec + '\n');

    // The module object.  This gets exposed to the module itself,
    // and it also gets cached for reuse, so multiple `require(module)` calls
    // return a single instance of the module.
    let module = {
      id: uri.spec,
      exports: {},
      paths: globalPaths.slice(),
      parent: requirer,
    };

    // Return module immediately if it's already started loading.  Note that
    // this can occur before the module is fully loaded, if there are circular
    // dependencies.
    if (modules.has(module.id)) {
      return modules.get(module.id).exports;
    }
    modules.set(module.id, module);

    // Provide the Components object to the "native binding modules" (which
    // implement APIs in JS that Node/Electron implement with native bindings).
    const wantComponents = uri.spec.startsWith('resource:///modules/gecko/');

    let sandbox = new Cu.Sandbox(systemPrincipal, {
      sandboxName: uri.spec,
      wantComponents: wantComponents,
      sandboxPrototype: this.global,
    });

    this.injectModuleGlobals(sandbox, module);

    // XXX Move these into injectModuleGlobals().
    sandbox.__filename = file.path;
    sandbox.__dirname = file.parent.path;

    try {
      // XXX evalInSandbox?
      subScriptLoader.loadSubScript(uri.spec, sandbox, 'UTF-8');
      return module.exports;
    } catch(ex) {
      modules.delete(module.id);
      // dump('require: error loading module ' + path + ' from ' + uri.spec + ': ' + ex + '\n' + ex.stack + '\n');
      throw ex;
    }
  };

  // The `process` global is complicated.  It's implemented as a module
  // that uses `require` to import other modules, but it also needs to exist
  // before those modules are evaluated, since it's a global that's supposed
  // to be available in them.
  //
  // So we declare it first here as a stub, then require the module to finish
  // initializing it.
  //
  this.global.process = {
    type: processType,
  };
  this.require({}, 'resource:///modules/gecko/process.js');

  this.global.Buffer = this.require({}, 'resource:///modules/node/buffer.js').Buffer;

  // Require the Electron init.js script for the given process type.
  //
  // We only do this for renderer processes for now.  For browser processes,
  // we instead require the one browser process module that renderer/init.js
  // depends on having already been required, browser/rpc-server.js.
  //
  // Eventually, we should get browser/init.js working and require it here,
  // at which point it'll require browser/rpc-server.js for us.
  //
  if (processType === 'renderer') {
    this.require({}, 'resource:///modules/renderer/init.js');
    this.require({}, 'resource:///modules/renderer/web-view/web-view.js');
    this.require({}, 'resource:///modules/renderer/web-view/web-view-attributes.js');
  } else {
    this.require({}, 'resource:///modules/browser/rpc-server.js');
  }
}

ModuleLoader.getLoaderForWindow = function(window) {
  let loader = windowLoaders.get(window);
  if (!loader) {
    loader = new ModuleLoader('renderer', window);
    windowLoaders.set(window, loader);
  }
  return loader;
};
