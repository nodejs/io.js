// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PLATFORM_WRAPPERS_H_
#define V8_BASE_PLATFORM_WRAPPERS_H_

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

namespace v8::base {

inline FILE* Fopen(const char* filename, const char* mode) {
#if V8_OS_STARBOARD
  return NULL;
#else
  return fopen(filename, mode);
#endif
}

inline int Fclose(FILE* stream) {
#if V8_OS_STARBOARD
  return -1;
#else
  return fclose(stream);
#endif
}

}  // namespace v8::base

#endif  // V8_BASE_PLATFORM_WRAPPERS_H_
