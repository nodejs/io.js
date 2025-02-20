// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_EXTERNAL_MEMORY_ACCOUNTER_H_
#define INCLUDE_EXTERNAL_MEMORY_ACCOUNTER_H_

#include <stdint.h>

#include "v8-isolate.h"

namespace v8 {

/**
 * This class is used to give V8 an indication of the amount of externally
 * allocated memory that is kept alive by JavaScript objects. V8 uses this to
 * decide when to perform garbage collections. Registering externally allocated
 * memory will trigger garbage collections more often than it would otherwise in
 * an attempt to garbage collect the JavaScript objects that keep the externally
 * allocated memory alive. Instances of ExternalMemoryAccounter check that the
 * reported external memory is back to 0 on destruction.
 */
class V8_EXPORT ExternalMemoryAccounter {
 public:
  /**
   * Returns the amount of external memory registered for `isolate`.
   */
  static int64_t GetTotalAmountOfExternalAllocatedMemoryForTesting(
      const Isolate* isolate);

  ExternalMemoryAccounter() = default;
  ~ExternalMemoryAccounter();
  ExternalMemoryAccounter(ExternalMemoryAccounter&&);
  ExternalMemoryAccounter& operator=(ExternalMemoryAccounter&&);
  ExternalMemoryAccounter(const ExternalMemoryAccounter&) = delete;
  ExternalMemoryAccounter& operator=(const ExternalMemoryAccounter&) = delete;

  /**
   * Reports an increase of `size` bytes of external memory.
   */
  void Increase(Isolate* isolate, size_t size);
  /**
   * Reports an update of `delta` bytes of external memory.
   */
  void Update(Isolate* isolate, int64_t delta);
  /**
   * Reports an decrease of `size` bytes of external memory.
   */
  void Decrease(Isolate* isolate, size_t size);

 private:
#ifdef V8_ENABLE_MEMORY_ACCOUNTING_CHECKS
  size_t amount_of_external_memory_ = 0;
  v8::Isolate* isolate_ = nullptr;
#endif
};

}  // namespace v8

#endif  // INCLUDE_EXTERNAL_MEMORY_ACCOUNTER_H_
