// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMMON_THREAD_LOCAL_STORAGE_H_
#define V8_COMMON_THREAD_LOCAL_STORAGE_H_

#include "include/v8config.h"

#if defined(COMPONENT_BUILD) || defined(V8_TLS_USED_IN_LIBRARY)
#define V8_TLS_LIBRARY_MODE 1
#else
#define V8_TLS_LIBRARY_MODE 0
#endif

// In shared libraries always hide the thread_local variable behind a call.
// This avoids complexity with "global-dyn" and allows to use "local-dyn"
// instead, across all platforms. On non-shared (release) builds, don't hide
// the variable behind the call (to improve performance in access time), but use
// different tls models on different platforms. On Windows, since chrome is
// linked into the chrome.dll which is always linked to chrome.exe at static
// link time (DT_NEEDED in ELF terms), use "init-exec". On Android, since the
// library can be opened with "dlopen" (through JNI), use "local-dyn". On other
// systems (Linux/ChromeOS/MacOS) use the fastest "local-exec".

//         |_____component_____|___non-component___|
// ________|_tls_model__|_hide_|_tls_model__|_hide_|
// Windows | local-dyn  | yes  | init-exec  |  no  |
// Android | local-dyn  | yes  | local-dyn  |  no  |
// Other   | local-dyn  | yes  | local-exec |  no  |
#if V8_TLS_LIBRARY_MODE
#define V8_TLS_MODEL "local-dynamic"
#else
#if defined(V8_TARGET_OS_WIN)
#define V8_TLS_MODEL "initial-exec"
#elif defined(V8_TARGET_OS_ANDROID)
#define V8_TLS_MODEL "local-dynamic"
#elif defined(V8_TARGET_OS_CHROMEOS)
// TODO(336738728): Figure out why ChromeOS can't use "local-exec".
#define V8_TLS_MODEL "local-dynamic"
#else
#define V8_TLS_MODEL "local-exec"
#endif
#endif

#if V8_TLS_LIBRARY_MODE

#define V8_TLS_DECLARE_GETTER(Name, Type, Member) \
  static V8_NOINLINE Type Name();
#define V8_TLS_DEFINE_GETTER(Name, Type, Member) \
  V8_NOINLINE Type Name() { return Member; }

#else  // !V8_TLS_LIBRARY_MODE

#define V8_TLS_DECLARE_GETTER(Name, Type, Member) \
  static V8_INLINE Type Name() { return Member; }
#define V8_TLS_DEFINE_GETTER(Name, Type, Member)

#endif  // V8_TLS_LIBRARY_MODE

#endif  // V8_COMMON_THREAD_LOCAL_STORAGE_H_
