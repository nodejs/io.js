// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/code-kind.h"

namespace v8 {
namespace internal {

const char* CodeKindToString(CodeKind kind) {
  switch (kind) {
#define CASE(name)     \
  case CodeKind::name: \
    return #name;
    CODE_KIND_LIST(CASE)
#undef CASE
  }
  UNREACHABLE();
}

const char* CodeKindToMarker(CodeKind kind, bool context_specialized) {
  switch (kind) {
    case CodeKind::INTERPRETED_FUNCTION:
      return "~";
    case CodeKind::BASELINE:
      return "^";
    case CodeKind::MAGLEV:
      return context_specialized ? "+'" : "+";
    case CodeKind::TURBOFAN_JS:
      return context_specialized ? "*'" : "*";
    default:
      return "";
  }
}

}  // namespace internal
}  // namespace v8
