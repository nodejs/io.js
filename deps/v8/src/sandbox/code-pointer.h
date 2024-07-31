// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_POINTER_H_
#define V8_SANDBOX_CODE_POINTER_H_

#include "src/common/globals.h"
#include "src/sandbox/code-entrypoint-tag.h"

namespace v8 {
namespace internal {

// Read the pointer to a Code's entrypoint via a code pointer.
// Only available when the sandbox is enabled as it requires the code pointer
// table.
V8_INLINE Address ReadCodeEntrypointViaCodePointerField(Address field_address,
                                                        CodeEntrypointTag tag);

// Writes the pointer to a Code's entrypoint via a code pointer.
// Only available when the sandbox is enabled as it requires the code pointer
// table.
V8_INLINE void WriteCodeEntrypointViaCodePointerField(Address field_address,
                                                      Address value,
                                                      CodeEntrypointTag tag);

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_CODE_POINTER_H_
