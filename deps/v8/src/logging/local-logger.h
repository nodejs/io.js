// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LOGGING_LOCAL_LOGGER_H_
#define V8_LOGGING_LOCAL_LOGGER_H_

#include "src/base/logging.h"
#include "src/logging/log.h"

namespace v8 {
namespace internal {

// TODO(leszeks): Add support for logging from off-thread.
class LocalLogger {
 public:
  explicit LocalLogger(Isolate* isolate);

  bool is_logging() const { return is_logging_; }
  bool is_listening_to_code_events() const {
    return is_listening_to_code_events_;
  }
  void ScriptDetails(Tagged<Script> script);
  void ScriptEvent(ScriptEventType type, int script_id);
  void CodeLinePosInfoRecordEvent(
      Address code_start, Tagged<TrustedByteArray> source_position_table,
      JitCodeEvent::CodeType code_type);

  void MapCreate(Tagged<Map> map);
  void MapDetails(Tagged<Map> map);

 private:
  V8FileLogger* v8_file_logger_;
  bool is_logging_;
  bool is_listening_to_code_events_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_LOGGING_LOCAL_LOGGER_H_
