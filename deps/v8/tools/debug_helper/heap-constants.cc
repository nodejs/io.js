// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heap-constants.h"

#include "src/common/globals.h"
#include "src/heap/memory-chunk.h"

namespace d = v8::debug_helper;

namespace v8 {
namespace internal {
namespace debug_helper_internal {

std::string FindKnownObject(uintptr_t address,
                            const d::HeapAddresses& heap_addresses) {
  uintptr_t containing_page = MemoryChunk::FromAddress(address)->address();
  uintptr_t offset_in_page = MemoryChunk::AddressToOffset(address);

  // If there's a match with a known page, then search only that page.
  if (containing_page == heap_addresses.map_space_first_page) {
    return FindKnownObjectInMapSpace(offset_in_page);
  }
  if (containing_page == heap_addresses.old_space_first_page) {
    return FindKnownObjectInOldSpace(offset_in_page);
  }
  if (containing_page == heap_addresses.read_only_space_first_page) {
    return FindKnownObjectInReadOnlySpace(offset_in_page);
  }

  // For any unknown pages, compile a list of things this object might be.
  std::string result;
  if (heap_addresses.map_space_first_page == 0) {
    std::string sub_result = FindKnownObjectInMapSpace(offset_in_page);
    if (!sub_result.empty()) {
      result += "maybe " + sub_result;
    }
  }
  if (heap_addresses.old_space_first_page == 0) {
    std::string sub_result = FindKnownObjectInOldSpace(offset_in_page);
    if (!sub_result.empty()) {
      result = (result.empty() ? "" : result + ", ") + "maybe " + sub_result;
    }
  }
  if (heap_addresses.read_only_space_first_page == 0) {
    std::string sub_result = FindKnownObjectInReadOnlySpace(offset_in_page);
    if (!sub_result.empty()) {
      result = (result.empty() ? "" : result + ", ") + "maybe " + sub_result;
    }
  }

  return result;
}

KnownInstanceType FindKnownMapInstanceTypes(
    uintptr_t address, const d::HeapAddresses& heap_addresses) {
  uintptr_t containing_page = MemoryChunk::FromAddress(address)->address();
  uintptr_t offset_in_page = MemoryChunk::AddressToOffset(address);

  // If there's a match with a known page, then search only that page.
  if (containing_page == heap_addresses.map_space_first_page) {
    return KnownInstanceType(
        FindKnownMapInstanceTypeInMapSpace(offset_in_page));
  }
  if (containing_page == heap_addresses.old_space_first_page) {
    return KnownInstanceType(
        FindKnownMapInstanceTypeInOldSpace(offset_in_page));
  }
  if (containing_page == heap_addresses.read_only_space_first_page) {
    return KnownInstanceType(
        FindKnownMapInstanceTypeInReadOnlySpace(offset_in_page));
  }

  // For any unknown pages, compile a list of things this object might be.
  KnownInstanceType result;
  if (heap_addresses.map_space_first_page == 0) {
    int sub_result = FindKnownMapInstanceTypeInMapSpace(offset_in_page);
    if (sub_result >= 0) {
      result.types.push_back(static_cast<i::InstanceType>(sub_result));
    }
  }
  if (heap_addresses.old_space_first_page == 0) {
    int sub_result = FindKnownMapInstanceTypeInOldSpace(offset_in_page);
    if (sub_result >= 0) {
      result.types.push_back(static_cast<i::InstanceType>(sub_result));
    }
  }
  if (heap_addresses.read_only_space_first_page == 0) {
    int sub_result = FindKnownMapInstanceTypeInReadOnlySpace(offset_in_page);
    if (sub_result >= 0) {
      result.types.push_back(static_cast<i::InstanceType>(sub_result));
    }
  }

  return result;
}

}  // namespace debug_helper_internal
}  // namespace internal
}  // namespace v8
