// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_TESTING_H_
#define V8_SANDBOX_TESTING_H_

#include <unordered_map>

#include "src/common/globals.h"
#include "src/objects/instance-type.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX

// Infrastructure for testing the security properties of the sandbox.
class SandboxTesting : public AllStatic {
 public:
  // The different sandbox testing modes.
  enum class Mode {
    // Sandbox testing mode is not active.
    kDisabled,

    // Mode for testing the sandbox.
    //
    // This will enable the crash filter and install the memory corruption API
    // (if enabled at compile time). If a harmless crash is detected, the
    // process is terminated with exist status zero. This is useful so that
    // tests pass if they for example fail a CHECK.
    kForTesting,

    // Mode for fuzzing the sandbox.
    //
    // Similar to kForTesting, but if a harmless crash is detected, the process
    // is terminated with a non-zero exit status so fuzzers can determine that
    // execution terminated prematurely.
    kForFuzzing,
  };

  // Enable sandbox testing mode.
  //
  // This will initialize the sandbox crash filter. The crash filter is a
  // signal handler for a number of fatal signals (e.g. SIGSEGV and SIGBUS) that
  // filters out all crashes that are not considered "sandbox violations".
  // Examples of such "safe" crahses (in the context of the sandbox) are memory
  // access violations inside the sandbox address space or access violations
  // that always lead to an immediate crash (for example, an access to a
  // non-canonical address which may be the result of a tag mismatch in one of
  // the sandbox's pointer tables). On the other hand, if the crash represents
  // a legitimate sandbox violation, the signal is forwarded to the original
  // signal handler which will report the crash appropriately.
  //
  // Currently supported on Linux only.
  V8_EXPORT_PRIVATE static void Enable(Mode mode);

  // Returns whether sandbox testing mode is enabled.
  static bool IsEnabled() { return mode_ != Mode::kDisabled; }

#ifdef V8_ENABLE_MEMORY_CORRUPTION_API
  // A JavaScript API that emulates typical exploit primitives.
  //
  // This can be used for testing the sandbox, for example to write regression
  // tests for bugs in the sandbox or to develop fuzzers.
  V8_EXPORT_PRIVATE static void InstallMemoryCorruptionApi(Isolate* isolate);
#endif  // V8_ENABLE_MEMORY_CORRUPTION_API

  // The current sandbox testing mode.
  static Mode mode() { return mode_; }

  // Returns a mapping of type names to their InstanceType.
  using InstanceTypeMap = std::unordered_map<std::string, InstanceType>;
  static InstanceTypeMap& GetInstanceTypeMap();

  // Returns a mapping of instance types to known field offsets. This is useful
  // mainly for the Sandbox.getFieldOffsetOf API which provides access to
  // internal field offsets of HeapObject to JavaScript.
  using FieldOffsets = std::unordered_map<std::string, int>;
  using FieldOffsetMap = std::unordered_map<InstanceType, FieldOffsets>;
  static FieldOffsetMap& GetFieldOffsetMap();

 private:
  static Mode mode_;
};

#endif  // V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_TESTING_H_
