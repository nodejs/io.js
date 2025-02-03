// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/logging/local-logger.h"

#include "src/execution/isolate.h"
#include "src/objects/map.h"

namespace v8 {
namespace internal {

// TODO(leszeks): Add support for logging from off-thread.
LocalLogger::LocalLogger(Isolate* isolate)
    : v8_file_logger_(isolate->v8_file_logger()),
      is_logging_(v8_file_logger_->is_logging()),
      is_listening_to_code_events_(
          v8_file_logger_->is_listening_to_code_events()) {}

void LocalLogger::ScriptDetails(Tagged<Script> script) {
  v8_file_logger_->ScriptDetails(script);
}
void LocalLogger::ScriptEvent(ScriptEventType type, int script_id) {
  v8_file_logger_->ScriptEvent(type, script_id);
}
void LocalLogger::CodeLinePosInfoRecordEvent(
    Address code_start, Tagged<TrustedByteArray> source_position_table,
    JitCodeEvent::CodeType code_type) {
  v8_file_logger_->CodeLinePosInfoRecordEvent(code_start, source_position_table,
                                              code_type);
}

void LocalLogger::MapCreate(Tagged<Map> map) {
  v8_file_logger_->MapCreate(map);
}

void LocalLogger::MapDetails(Tagged<Map> map) {
  v8_file_logger_->MapDetails(map);
}

}  // namespace internal
}  // namespace v8
