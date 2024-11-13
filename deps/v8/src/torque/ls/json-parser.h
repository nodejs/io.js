// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_LS_JSON_PARSER_H_
#define V8_TORQUE_LS_JSON_PARSER_H_

#include <optional>

#include "src/base/macros.h"
#include "src/torque/ls/json.h"
#include "src/torque/utils.h"

namespace v8::internal::torque::ls {

struct JsonParserResult {
  JsonValue value;
  std::optional<TorqueMessage> error;
};

V8_EXPORT_PRIVATE JsonParserResult ParseJson(const std::string& input);

}  // namespace v8::internal::torque::ls

#endif  // V8_TORQUE_LS_JSON_PARSER_H_
