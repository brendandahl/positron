/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ModuleUtils.h"
#include "nsIModule.h"

#include "mozilla/dom/ScriptSettings.h" // for AutoJSAPI
#include "nsINodeLoader.h"
#include "NodeLoader.h"
#include "nsIFile.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "env-inl.h"
#include "jsapi.h"
#include "nsString.h"
#include "nsAppRunner.h"
#include "base/message_loop.h"

/* << From node_bindings_mac.cc */
#include <errno.h>
#include <sys/select.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
/* >> */

using namespace mozilla;

////////////////////////////////////////////////////////////////////////
// Define the contructor function for the objects
//
// NOTE: This creates an instance of objects by using the default constructor
//
NS_GENERIC_FACTORY_CONSTRUCTOR(NodeLoader)

////////////////////////////////////////////////////////////////////////
// Define a table of CIDs implemented by this module along with other
// information like the function to create an instance, contractid, and
// class name.
//
#define NS_NODELOADER_CONTRACTID \
  "@mozilla.org/positron/nodeloader;1"

#define NS_NODELOADER_CID             \
{ /* 019718E3-CDB5-11d2-8D3C-000000000000 */    \
0x019618e3, 0xcdb5, 0x11d2,                     \
{ 0x8d, 0x3c, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 } }

/* Implementation file */
NS_IMPL_ISUPPORTS(NodeLoader, nsINodeLoader)

NodeLoader::NodeLoader()
{
  /* member initializers and constructor code */
}

NodeLoader::~NodeLoader()
{
  /* destructor code */
}

static void ActivateUVLoopCallback(const v8::FunctionCallbackInfo<v8::Value>& info) {
  printf(">>> ActivateUVLoopCallback()\n");
  v8::Local<v8::External> this_value = v8::Local<v8::External>::Cast(info.Data());
  NodeLoader* nodeLoader = static_cast<NodeLoader*>(this_value->Value());
  nodeLoader->ActivateUVLoop(v8::Isolate::GetCurrent());
}



/* void init (); */
NS_IMETHODIMP NodeLoader::Init(const nsACString& type, JSContext* aContext)
{
  printf(">>> NodeLoader::Init [type=%s]\n", ToNewCString(type));
  v8::V8::Initialize();
  uv_async_init(uv_default_loop(), &call_next_tick_async_, OnCallNextTick);
  call_next_tick_async_.data = this;

  v8::Isolate* isolate = v8::Isolate::New(aContext);
  // v8::Isolate::Scope isolate_scope(isolate);
  // TODO: FIX THIS LEAK
  v8::Isolate::Scope* isolate_scope = new v8::Isolate::Scope(isolate);
  v8::HandleScope handle_scope(isolate);

  v8::Local<v8::Context> context = v8::Context::New(isolate);
  // v8::Context::Scope context_scope(context);
  // TODO: FIX THIS LEAK
  v8::Context::Scope* context_scope = new v8::Context::Scope(context);

  node::Environment* env = CreateEnvironment(context, type);
  set_uv_env(env);
  uv_loop_ = uv_default_loop();
  PreMainMessageLoopRun();
  LoadEnvironment(env);

  return NS_OK;
}

/// ZZZ: Need to figure out exactly when this is run...
void NodeLoader::PreMainMessageLoopRun() {
  // Run user's main script before most things get initialized, so we can have
  // a chance to setup everything.
  PrepareMessageLoop();
  RunMessageLoop();
}

node::Environment* NodeLoader::CreateEnvironment(
      v8::Handle<v8::Context> context, const nsACString& type) {
  v8::Isolate* isolate = context->GetIsolate();
  // Convert the app path to an absolute app path.
  nsCOMPtr<nsIFile> cwdDir;
  nsDirectoryService::gService->Get(NS_OS_CURRENT_WORKING_DIR,
                                    NS_GET_IID(nsIFile),
                                    getter_AddRefs(cwdDir));
  MOZ_ASSERT(cwdDir);

  char* absoluteAppPath = nullptr;
  nsDependentCString appPath(gArgv[1]);
  // Try the path as a relative path to the current working directory.
  nsresult rv = cwdDir->AppendRelativeNativePath(appPath);
  if (NS_SUCCEEDED(rv)) {
    nsString absoluteAppPathString;
    rv = cwdDir->GetPath(absoluteAppPathString);
    // NS_ENSURE_SUCCESS(rv, rv);
    absoluteAppPath = ToNewCString(NS_LossyConvertUTF16toASCII(absoluteAppPathString));
  } else { // It's hopefully an absolute path.
    absoluteAppPath = gArgv[1];
  }

  nsCOMPtr<nsIFile> greDir;
  nsDirectoryService::gService->Get(NS_GRE_DIR,
                                    NS_GET_IID(nsIFile),
                                    getter_AddRefs(greDir));
  MOZ_ASSERT(greDir);
  greDir->AppendNative(NS_LITERAL_CSTRING("modules"));
  greDir->AppendNative(type);
  greDir->AppendNative(NS_LITERAL_CSTRING("init.js"));
  nsAutoString initalScript;
  greDir->GetPath(initalScript);

  int exec_argc;
  const char** exec_argv;
  int argc = 2;
  char **argv = new char *[argc + 1];
  argv[0] = gArgv[0];
  argv[1] = ToNewCString(NS_LossyConvertUTF16toASCII(initalScript));
  node::Init(&argc, const_cast<const char**>(argv),  &exec_argc, &exec_argv);

  node::IsolateData* isolateData = node::CreateIsolateData(isolate, uv_default_loop());
  node::Environment* env = node::CreateEnvironment(
    isolateData,
    context,
    argc, argv, 0, nullptr);

  env->process_object()->Set(v8::String::NewFromUtf8(isolate, "resourcesPath"),
                             v8::String::NewFromUtf8(isolate, absoluteAppPath));
  env->process_object()->Set(v8::String::NewFromUtf8(isolate, "type"),
                             v8::String::NewFromUtf8(isolate, "browser"));

  v8::Local<v8::Value> this_value = v8::External::New(isolate, this);
  v8::Local<v8::FunctionTemplate> acc = v8::FunctionTemplate::New(isolate, ActivateUVLoopCallback, this_value);
  env->process_object()->Set(context, v8::String::NewFromUtf8(isolate, "activateUvLoop"),
                             acc->GetFunction(context).ToLocalChecked());
  // env->process_object()->Set(v8::String::NewFromUtf8(isolate, "doShit", v8::NewStringType::kNormal)
  //                 .ToLocalChecked(),
  //             v8::FunctionTemplate::New(isolate, LogCallback));
  return env;
}

void NodeLoader::LoadEnvironment(node::Environment* env) {
  node::LoadEnvironment(env);
  // TODO
  // mate::EmitEvent(env->isolate(), env->process_object(), "loaded");
}

void NodeLoader::ActivateUVLoop(v8::Isolate* isolate) {
  node::Environment* env = node::Environment::GetCurrent(isolate);
  if (std::find(pending_next_ticks_.begin(), pending_next_ticks_.end(), env) !=
      pending_next_ticks_.end())
    return;

  pending_next_ticks_.push_back(env);
  uv_async_send(&call_next_tick_async_);
}

// static
void NodeLoader::OnCallNextTick(uv_async_t* handle) {
  NodeLoader* self = static_cast<NodeLoader*>(handle->data);
  for (std::list<node::Environment*>::const_iterator it =
           self->pending_next_ticks_.begin();
       it != self->pending_next_ticks_.end(); ++it) {
    node::Environment* env = *it;
    // KickNextTick, copied from node.cc:
    node::Environment::AsyncCallbackScope callback_scope(env);
    if (callback_scope.in_makecallback())
      continue;
    node::Environment::TickInfo* tick_info = env->tick_info();
    if (tick_info->length() == 0)
      env->isolate()->RunMicrotasks();
    v8::Local<v8::Object> process = env->process_object();
    if (tick_info->length() == 0)
      tick_info->set_index(0);
    env->tick_callback_function()->Call(process, 0, nullptr).IsEmpty();
  }

  self->pending_next_ticks_.clear();
}


void NodeLoader::PrepareMessageLoop() {
  // Add dummy handle for libuv, otherwise libuv would quit when there is
  // nothing to do.
  uv_async_init(uv_loop_, &dummy_uv_handle_, nullptr);

  // Start worker that will interrupt main loop when having uv events.
  uv_sem_init(&embed_sem_, 0);
  uv_thread_create(&embed_thread_, EmbedThreadRunner, this);
}
// JSObject* s_test;

void NodeLoader::RunMessageLoop() {
  // DCHECK(!is_browser_ || BrowserThread::CurrentlyOn(BrowserThread::UI));

  // The MessageLoop should have been created, remember the one in main thread.
  message_loop_ = MessageLoop::current();
  // s_test = JS::CurrentGlobalOrNull(cx);

  // Run uv loop for once to give the uv__io_poll a chance to add all events.
  UvRunOnce();
}

void NodeLoader::UvRunOnce() {
  node::Environment* env = uv_env();

  v8::Isolate::Scope isolate_scope(env->isolate());

  // TODO
  // Use Locker in browser process.
  // mate::Locker locker(env->isolate());
  v8::HandleScope handle_scope(env->isolate());

  // Enter node context while dealing with uv events.
  v8::Context::Scope context_scope(env->context());

  // Perform microtask checkpoint after running JavaScript.
  v8::MicrotasksScope script_scope(env->isolate(),
                                   v8::MicrotasksScope::kRunMicrotasks);

  JSContext* cx = JSContextFromIsolate(env->isolate());
  MOZ_ASSERT(cx);
  JSObject* globalObject = JS::CurrentGlobalOrNull(cx);
  MOZ_ASSERT(globalObject);
  dom::AutoEntryScript aes(globalObject, "NodeLoader UvRunOnce");

  // Deal with uv events.
  int r = uv_run(uv_loop_, UV_RUN_NOWAIT);
  printf(">>> NodeLoader::UvRunOnce [r=%d] 0 is quit\n", r);
  // if (r == 0)
  //   message_loop_->QuitWhenIdle();  // Quit from uv.

  // Tell the worker thread to continue polling.
  uv_sem_post(&embed_sem_);
}

void NodeLoader::WakeupMainThread() {
  DCHECK(message_loop_);

  message_loop_->PostTask(NewRunnableMethod(this, &NodeLoader::UvRunOnce));
}

void NodeLoader::WakeupEmbedThread() {
  uv_async_send(&dummy_uv_handle_);
}

// static
void NodeLoader::EmbedThreadRunner(void *arg) {
  NodeLoader* self = static_cast<NodeLoader*>(arg);

  while (true) {
    // Wait for the main loop to deal with events.
    uv_sem_wait(&self->embed_sem_);
    if (self->embed_closed_)
      break;

    // Wait for something to happen in uv loop.
    // Note that the PollEvents() is implemented by derived classes, so when
    // this class is being destructed the PollEvents() would not be available
    // anymore. Because of it we must make sure we only invoke PollEvents()
    // when this class is alive.
    self->PollEvents();
    if (self->embed_closed_)
      break;

    // Deal with event in main thread.
    self->WakeupMainThread();
  }
}

/* << From node_bindings_mac.cc */
void NodeLoader::PollEvents() {
  struct timeval tv;
  int timeout = uv_backend_timeout(uv_loop_);
  if (timeout != -1) {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
  }

  fd_set readset;
  int fd = uv_backend_fd(uv_loop_);
  FD_ZERO(&readset);
  FD_SET(fd, &readset);

  // Wait for new libuv events.
  int r;
  do {
    r = select(fd + 1, &readset, NULL, NULL, timeout == -1 ? NULL : &tv);
  } while (r == -1 && errno == EINTR);
}
/* >> */

NS_DEFINE_NAMED_CID(NS_NODELOADER_CID);

static const mozilla::Module::CIDEntry kEmbeddingCIDs[] = {
    { &kNS_NODELOADER_CID, false, nullptr, NodeLoaderConstructor },
    { nullptr }
};

static const mozilla::Module::ContractIDEntry kEmbeddingContracts[] = {
    { NS_NODELOADER_CONTRACTID, &kNS_NODELOADER_CID },
    { nullptr }
};

static const mozilla::Module kEmbeddingModule = {
    mozilla::Module::kVersion,
    kEmbeddingCIDs,
    kEmbeddingContracts
};

NSMODULE_DEFN(NodeLoader) = &kEmbeddingModule;