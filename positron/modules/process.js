/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

'use strict';

// dump("!!!!!! " + process + "\n");

const { classes: Cc, interfaces: Ci, results: Cr, utils: Cu } = Components;

let nodeLoader = Cc["@mozilla.org/positron/nodeloader;1"]
                 .getService(Ci.nsINodeLoader);
// nodeLoader.init("renderer");

// The Node version of this module only re-exports the `process` global,
// because the global is implemented natively.  But we implement the global
// in this file, which is the first module required by the ModuleLoader.

Cu.import('resource://gre/modules/Services.jsm');

const exeFile = Services.dirsvc.get("XREExeF", Ci.nsIFile);

// This file is a special kind of module, as the module loader requires it
// during loader construction and uses it to define the `process` global.
// Thus that global is not available at evaluation time, neither for this module
// nor for any other modules it requires upon evaluation.

const { EventEmitter } = require('events');
const process2 = new EventEmitter();


module.exports = process2;

// Based on addon-sdk/source/lib/sdk/timers.js.
const threadManager = Cc["@mozilla.org/thread-manager;1"].
                      getService(Ci.nsIThreadManager);
let _immediateCallback;
let _needImmediateCallback = false;
let _immediateCallbackScheduled = false;
Object.defineProperty(process2, '_immediateCallback', {
  get() { return _immediateCallback },
  set(value) { _immediateCallback = value },
});
Object.defineProperty(process2, '_needImmediateCallback', {
  get() { return _needImmediateCallback },
  set(value) {
    _needImmediateCallback = value;
    if (_needImmediateCallback && !_immediateCallbackScheduled) {
      _immediateCallbackScheduled = true;
      threadManager.currentThread.dispatch(() => {
        _immediateCallbackScheduled = false;
        _immediateCallback();
      }, Ci.nsIThread.DISPATCH_NORMAL);
    }
  },
});

// This is a stub with a placeholder that browser/init.js and renderer/init.js
// both remove.
process2.argv = [exeFile.leafName, "placeholder that init.js removes"];

// In Node, process2.js is basically a noop, as it just re-exports the existing
// `process2` global.  But in Positron, we implement the `process2` global via
// this module, so we construct the `process2` global by importing this module
// rather than the other way around.

// This comes from Electron, where it's defined by common/init.js, which we
// currently don't load in the main process2 (although we do load it in renderer
// process2es, so the version in init.js overrides this one in those process2es).
//
// TODO: once we enable the loading of init.js in the main process2, remove this.
//
process2.atomBinding = function(name) {
  try {
    return process2.binding("atom_" + process2.type + "_" + name);
  } catch (error) {
    if (/No such module/.test(error.message)) {
      return process2.binding("atom_common_" + name);
    }

    // Note: when atomBinding fails here, it'll always report an error loading
    // atom_common_${name}, but that doesn't mean that we should stub/implement
    // a module with that name.  The equivalent in Electron might be specific
    // to "browser" or "renderer" process2es.
    //
    // To determine which Electron module to stub/implement, search Electron
    // for atom_browser_${name}, atom_renderer_${name}, or atom_common_${name}.
    //
    throw error;
  }
};

// This loads a native binding in Node, but we aren't using real Node yet,
// and instead we're emulating native bindings via JavaScript modules, so we
// load the "native binding module" corresponding to the native binding name.
//
// All such modules are in the modules/gecko/ subdirectory, and some of them
// have the same names as modules provided by Node itself (f.e. the 'buffer'
// module, which calls process2.binding to import the 'buffer' native binding),
// so we specify the absolute URL to the module to avoid name resolution,
// which might find a different module.
//
process2.positronBinding = process2.binding = function(name) {
  return require(`resource:///modules/gecko/${name}.js`);
}

const positronUtil = process2.positronBinding('positron_util');

process2.execPath = exeFile.path;

// Per <https://nodejs.org/api/process2.html#process2_process2_platform>,
// valid values for this property are darwin, freebsd, linux, sunos and win32;
// so we convert winnt to win32.  We also lowercase the value, but otherwise
// we don't modify it, so it *might* contain a value outside the Node set, per
// <https://developer.mozilla.org/en-US/docs/Mozilla/Developer_guide/Build_Instructions/OS_TARGET>.
process2.platform = Services.appinfo.OS.toLowerCase().replace(/^winnt$/, 'win32');

process2.versions = {
  node: '0',
  chrome: Services.appinfo.platformVersion,
  electron: Services.appinfo.version,
};

process2.release = {
  name: 'node',
};

Object.defineProperty(process2, 'pid', {
  get() { return Services.appinfo.processID },
});

// We might be able to implement this by using nsIEnvironment, although that API
// doesn't provide enumeration, whereas this one presumably does.
process2.env = {};
positronUtil.makeStub('process.env')();

process2.type = 'window' in global ? 'renderer' : 'browser';
