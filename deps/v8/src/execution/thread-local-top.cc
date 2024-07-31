// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/thread-local-top.h"

#include "src/base/sanitizer/msan.h"
#include "src/execution/isolate.h"
#include "src/execution/simulator.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/trap-handler/trap-handler.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

void ThreadLocalTop::Clear() {
  try_catch_handler_ = nullptr;
  isolate_ = nullptr;
  c_entry_fp_ = kNullAddress;
  c_function_ = kNullAddress;
  context_ = Context();
  topmost_script_having_context_ = Context();
  thread_id_ = ThreadId();
  pending_handler_entrypoint_ = kNullAddress;
  pending_handler_constant_pool_ = kNullAddress;
  pending_handler_fp_ = kNullAddress;
  pending_handler_sp_ = kNullAddress;
  num_frames_above_pending_handler_ = 0;
  last_api_entry_ = kNullAddress;
  pending_message_ = Tagged<Object>();
  rethrowing_message_ = false;
  handler_ = kNullAddress;
  simulator_ = nullptr;
  js_entry_sp_ = kNullAddress;
  external_callback_scope_ = nullptr;
  current_vm_state_ = EXTERNAL;
  current_embedder_state_ = nullptr;
  top_backup_incumbent_scope_ = nullptr;
  failed_access_check_callback_ = nullptr;
  thread_in_wasm_flag_address_ = kNullAddress;
  central_stack_limit_ = kNullAddress;
  central_stack_sp_ = kNullAddress;
  secondary_stack_sp_ = kNullAddress;
  secondary_stack_limit_ = kNullAddress;
}

void ThreadLocalTop::Initialize(Isolate* isolate) {
  Clear();
  isolate_ = isolate;
  thread_id_ = ThreadId::Current();
#if V8_ENABLE_WEBASSEMBLY
  thread_in_wasm_flag_address_ = reinterpret_cast<Address>(
      trap_handler::GetThreadInWasmThreadLocalAddress());
  is_on_central_stack_flag_ = true;
#endif  // V8_ENABLE_WEBASSEMBLY
#ifdef USE_SIMULATOR
  simulator_ = Simulator::current(isolate);
#endif
}

void ThreadLocalTop::Free() {}

#if defined(USE_SIMULATOR)
void ThreadLocalTop::StoreCurrentStackPosition() {
  last_api_entry_ = simulator_->get_sp();
}
#elif defined(V8_USE_ADDRESS_SANITIZER)
void ThreadLocalTop::StoreCurrentStackPosition() {
  last_api_entry_ = reinterpret_cast<Address>(GetCurrentStackPosition());
}
#endif

}  // namespace internal
}  // namespace v8
