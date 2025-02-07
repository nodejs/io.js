// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_DATA_VIEW_OPS_H_
#define V8_BUILTINS_DATA_VIEW_OPS_H_

#include <stdint.h>

#include "src/base/logging.h"

// DataView operations that are handled as well-known imports.
#define DATAVIEW_OP_LIST(V) \
  V(BigInt64)               \
  V(BigUint64)              \
  V(Float32)                \
  V(Float64)                \
  V(Int8)                   \
  V(Int16)                  \
  V(Int32)                  \
  V(Uint8)                  \
  V(Uint16)                 \
  V(Uint32)

enum DataViewOp : uint8_t {
#define V(Name) kGet##Name, kSet##Name,
  DATAVIEW_OP_LIST(V)
#undef V
      kByteLength
};

constexpr const char* ToString(DataViewOp op) {
  switch (op) {
#define V(Name)                            \
  case DataViewOp::kGet##Name:             \
    return "DataView.prototype.get" #Name; \
  case DataViewOp::kSet##Name:             \
    return "DataView.prototype.set" #Name;
  DATAVIEW_OP_LIST(V)
#undef V
  case DataViewOp::kByteLength:
    return "get DataView.prototype.byteLength";
  default:
    UNREACHABLE();
  }
}

#endif  // V8_BUILTINS_DATA_VIEW_OPS_H_
