/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_NodeLoader_h
#define mozilla_dom_NodeLoader_h

#include "nsINodeLoader.h"
#include "nsISupports.h"
#include "uv.h"
#include "node.h"
#include <list>

class MessageLoop;

class NodeLoader final : public nsINodeLoader
{
public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSINODELOADER


  NodeLoader();

  // Create the environment and load node.js.
  node::Environment* CreateEnvironment(v8::Handle<v8::Context> context, const nsACString& type);

  // Load node.js in the environment.
  void LoadEnvironment(node::Environment* env);

  void ActivateUVLoop(v8::Isolate* isolate);

  // Prepare for message loop integration.
  void PrepareMessageLoop();

  // Do message loop integration.
  /* virtual */ void RunMessageLoop();

  // Gets/sets the environment to wrap uv loop.
  void set_uv_env(node::Environment* env) { uv_env_ = env; }
  node::Environment* uv_env() const { return uv_env_; }

  // Main thread's libuv loop.
  uv_loop_t* uv_loop_;

  // Main thread's MessageLoop.
  MessageLoop* message_loop_;

protected:
  // Called to poll events in new thread.
  // virtual void PollEvents() = 0;
  void PollEvents();

  // Run the libuv loop for once.
  void UvRunOnce();

  // Make the main thread run libuv loop.
  void WakeupMainThread();

  // Interrupt the PollEvents.
  void WakeupEmbedThread();

private:
  ~NodeLoader();

  static void OnCallNextTick(uv_async_t* handle);

  uv_async_t call_next_tick_async_;
  std::list<node::Environment*> pending_next_ticks_;

  // Thread to poll uv events.
  static void EmbedThreadRunner(void *arg);

  // Whether the libuv loop has ended.
  bool embed_closed_;

  // Dummy handle to make uv's loop not quit.
  uv_async_t dummy_uv_handle_;

  // Thread for polling events.
  uv_thread_t embed_thread_;

  // Semaphore to wait for main loop in the embed thread.
  uv_sem_t embed_sem_;

  // Environment that to wrap the uv loop.
  node::Environment* uv_env_;

  // base::WeakPtrFactory<NodeBindings> weak_factory_;

  void PreMainMessageLoopRun();

protected:
  /* additional members */
};

#endif // mozilla_dom_NodeLoader_h