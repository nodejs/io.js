// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/name-trait.h"

#include <stdio.h>

#include "src/base/logging.h"
#include "src/base/macros.h"

namespace cppgc {

// static
constexpr const char NameProvider::kHiddenName[];

// static
constexpr const char NameProvider::kNoNameDeducible[];

namespace internal {

// static
HeapObjectName NameTraitBase::GetNameFromTypeSignature(const char* signature) {
  // Parsing string of structure:
  //    static HeapObjectName NameTrait<int>::GetNameFor(...) [T = int]
  if (!signature) return {NameProvider::kNoNameDeducible, false};

  const std::string raw(signature);
  const auto start_pos = raw.rfind("T = ") + 4;
  DCHECK_NE(std::string::npos, start_pos);
  const auto len = raw.length() - start_pos - 1;
  const std::string name = raw.substr(start_pos, len).c_str();
  char* name_buffer = new char[name.length() + 1];
  int written = snprintf(name_buffer, name.length() + 1, "%s", name.c_str());
  DCHECK_EQ(static_cast<size_t>(written), name.length());
  USE(written);
  return {name_buffer, false};
}

}  // namespace internal
}  // namespace cppgc
