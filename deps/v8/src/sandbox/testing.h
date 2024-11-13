// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_TESTING_H_
#define V8_SANDBOX_TESTING_H_

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
    // Sandbox testing is not active.
    kDisabled,
    // Mode for demonstrating and validating sandbox bypasses.
    // In this mode, a read-only page is allocated during startup at a random
    // address outside of the sandbox. Only access violations on this page (the
    // "target page") are treated as sandbox bypasses, everything else is
    // filtered out by the crash filter.
    kForTesting,
    // Mode for fuzzing the sandbox.
    // In this mode, the sandbox crash filter is installed and filters out all
    // crashes that do not represent sandbox violations.
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
  V8_EXPORT_PRIVATE static void InstallMemoryCorruptionApiIfEnabled(
      Isolate* isolate);
#endif  // V8_ENABLE_MEMORY_CORRUPTION_API

  // The current sandbox testing mode.
  static Mode mode() { return mode_; }

  // If sandbox testing mode is enabled, returns the address of the target page
  // that needs to be written to to demonstrate a sandbox bypass.
  static Address target_page_base() { return target_page_base_; }

  // If sandbox testing mode is enabled, returns the size of the target page
  // that needs to be written to to demonstrate a sandbox bypass.
  static Address target_page_size() { return target_page_size_; }

  // Returns true if the access violation happened inside the target page.
  static bool IsInsideTargetPage(Address faultaddr);

  // Returns a mapping of instance types to known field offsets. This is useful
  // mainly for the Sandbox.getFieldOffsetOf API which provides access to
  // internal field offsets of HeapObject to JavaScript.
  using FieldOffsets = std::unordered_map<std::string, int>;
  using FieldOffsetMap = std::unordered_map<InstanceType, FieldOffsets>;
  static FieldOffsetMap& GetFieldOffsetMap();

 private:
  static Mode mode_;
  static Address target_page_base_;
  static size_t target_page_size_;
};

#endif  // V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_TESTING_H_
