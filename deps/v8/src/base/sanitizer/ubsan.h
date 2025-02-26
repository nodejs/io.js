// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// UndefinedBehaviorSanitizer support.

#ifndef V8_BASE_SANITIZER_UBSAN_H_
#define V8_BASE_SANITIZER_UBSAN_H_

#if defined(UNDEFINED_SANITIZER)

#define DISABLE_UBSAN __attribute__((no_sanitize("undefined")))

#else  // !defined(UNDEFINED_SANITIZER)

#define DISABLE_UBSAN

#endif  // !defined(UNDEFINED_SANITIZER)

#endif  // V8_BASE_SANITIZER_UBSAN_H_
