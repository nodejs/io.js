// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_VERSION_H_
#define V8_UTILS_VERSION_H_

#include <cstdint>

#include "src/base/hashing.h"

namespace v8 {

namespace base {
template <typename T>
class Vector;
}  // namespace base

namespace internal {

class V8_EXPORT Version {
 public:
  // Return the various version components.
  static int GetMajor() { return major_; }
  static int GetMinor() { return minor_; }
  static int GetBuild() { return build_; }
  static int GetPatch() { return patch_; }
  static const char* GetEmbedder() { return embedder_; }
  static bool IsCandidate() { return candidate_; }
  static uint32_t Hash() {
    return static_cast<uint32_t>(
        base::hash_combine(major_, minor_, build_, patch_));
  }

  // Calculate the V8 version string.
  static void GetString(base::Vector<char> str);

  // Calculate the SONAME for the V8 shared library.
  static void GetSONAME(base::Vector<char> str);

  static const char* GetVersion() { return version_string_; }

 private:
  // NOTE: can't make these really const because of test-version.cc.
  static int major_;
  static int minor_;
  static int build_;
  static int patch_;
  static const char* embedder_;
  static bool candidate_;
  static const char* soname_;
  static const char* version_string_;

  // In test-version.cc.
  friend void SetVersion(int major, int minor, int build, int patch,
                         const char* embedder, bool candidate,
                         const char* soname);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_VERSION_H_
