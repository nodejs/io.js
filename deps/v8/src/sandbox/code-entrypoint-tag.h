// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_ENTRYPOINT_TAG_H_
#define V8_SANDBOX_CODE_ENTRYPOINT_TAG_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// A tag to distinguish code pointers with different calling conventions.
//
// When the sandbox is enabled, we assume that an attacker cannot modify memory
// outside of the sandbox and so the code pointer table achieves a form of
// coarse-grained control-flow integrity (CFI) for code running in the sandbox:
// indirect control flow transfers initiated by such code (for example,
// invoking a JavaScript or WebAssembly function or a compiled RegExp) will
// always land at a valid code entrypoint. However, this is not enough:
// different types of code may use different calling conventions or
// incompatible signatures. Further, some internal builtins may not expect to
// be called indirectly in this way at all. CodeEntrypointTags are therefore
// used to achieve fine-grained CFI: used appropriately, they guarantee that
// the callee and caller of such control-flow transfers are compatible. As
// such, two code objects should use the same tag iff they can safely be
// interchanged at all (indirect) callsites.
//
// Implementation-wise, the tags are simply XORed into the top bits of the
// entrypoint pointer in the CPT and hardcoded at the callsite, where the
// pointer is untagged (again via XOR) prior to invoking it. If the tags do not
// match, the resulting pointer will be invalid and cause a safe crash.
// TODO(saelo): on Arm64, we could probably use PAC instead of XORing the tag
// into the pointer. This may be more efficient.
constexpr int kCodeEntrypointTagShift = 48;
enum CodeEntrypointTag : uint64_t {
  // TODO(saelo): eventually, we'll probably want to remove the default tag.
  kDefaultCodeEntrypointTag = 0,
  // TODO(saelo): give these unique tags.
  kJSEntrypointTag = kDefaultCodeEntrypointTag,
  kWasmEntrypointTag = uint64_t{1} << kCodeEntrypointTagShift,
  kBytecodeHandlerEntrypointTag = uint64_t{2} << kCodeEntrypointTagShift,
  kLoadWithVectorICHandlerEntrypointTag = uint64_t{3}
                                          << kCodeEntrypointTagShift,
  kStoreWithVectorICHandlerEntrypointTag = uint64_t{4}
                                           << kCodeEntrypointTagShift,
  kStoreTransitionICHandlerEntrypointTag = uint64_t{5}
                                           << kCodeEntrypointTagShift,
  kRegExpEntrypointTag = uint64_t{6} << kCodeEntrypointTagShift,
  // TODO(saelo): create more of these tags.

  // Tag to use for code that will never be called indirectly via the CPT.
  kInvalidEntrypointTag = uint64_t{0xff} << kCodeEntrypointTagShift,
  // Tag used internally by the code pointer table to mark free entries.
  kFreeCodePointerTableEntryTag = uint64_t{0xffff} << kCodeEntrypointTagShift,
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_CODE_ENTRYPOINT_TAG_H_
