// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/globals.h"
#if defined(TARGET_OS_LINUX)

#include "vm/signal_handler.h"
#include "vm/thread_interrupter.h"

namespace dart {

DECLARE_FLAG(bool, thread_interrupter);
DECLARE_FLAG(bool, trace_thread_interrupter);

class ThreadInterrupterLinux : public AllStatic {
 public:
  static void ThreadInterruptSignalHandler(int signal, siginfo_t* info,
                                           void* context_) {
    if (signal != SIGPROF) {
      return;
    }
    InterruptableThreadState* state = ThreadInterrupter::CurrentThreadState();
    if ((state == NULL) || (state->callback == NULL)) {
      // No interrupter state or callback.
      return;
    }
    ASSERT(Thread::Compare(state->id, Thread::GetCurrentThreadId()));
    // Extract thread state.
    ucontext_t* context = reinterpret_cast<ucontext_t*>(context_);
    mcontext_t mcontext = context->uc_mcontext;
    InterruptedThreadState its;
    its.tid = state->id;
    its.pc = SignalHandler::GetProgramCounter(mcontext);
    its.fp = SignalHandler::GetFramePointer(mcontext);
    its.sp = SignalHandler::GetStackPointer(mcontext);
    state->callback(its, state->data);
  }
};


void ThreadInterrupter::InterruptThread(InterruptableThreadState* state) {
  if (FLAG_trace_thread_interrupter) {
    OS::Print("ThreadInterrupter interrupting %p\n",
              reinterpret_cast<void*>(state->id));
  }
  pthread_kill(state->id, SIGPROF);
}


void ThreadInterrupter::InstallSignalHandler() {
  SignalHandler::Install(ThreadInterrupterLinux::ThreadInterruptSignalHandler);
}


}  // namespace dart

#endif  // defined(TARGET_OS_LINUX)
