// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug-helper-internal.h"
#include "src/compiler/turbofan-types.h"

namespace ic = v8::internal::compiler;

extern "C" {
V8_DEBUG_HELPER_EXPORT const char* _v8_debug_helper_BitsetName(
    uint64_t payload) {
  // Check if payload is a bitset and return the bitset type.
  // This line is duplicating the logic from Type::IsBitset.
  bool is_bit_set = payload & 1;
  if (!is_bit_set) return nullptr;
  ic::BitsetType::bitset bits =
      static_cast<ic::BitsetType::bitset>(payload ^ 1u);
  switch (bits) {
#define RETURN_NAMED_TYPE(type, value) \
  case ic::BitsetType::k##type:        \
    return #type;
    PROPER_BITSET_TYPE_LIST(RETURN_NAMED_TYPE)
    INTERNAL_BITSET_TYPE_LIST(RETURN_NAMED_TYPE)
#undef RETURN_NAMED_TYPE

    default:
      return nullptr;
  }
}
}
