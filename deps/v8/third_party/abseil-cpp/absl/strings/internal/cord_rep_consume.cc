// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/internal/cord_rep_consume.h"

#include <array>
#include <utility>

#include "absl/container/inlined_vector.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/internal/cord_internal.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

namespace {

// Unrefs the provided `substring`, and returns `substring->child`
// Adds or assumes a reference on `substring->child`
CordRep* ClipSubstring(CordRepSubstring* substring) {
  CordRep* child = substring->child;
  if (substring->refcount.IsOne()) {
    delete substring;
  } else {
    CordRep::Ref(child);
    CordRep::Unref(substring);
  }
  return child;
}

}  // namespace

void Consume(CordRep* rep,
             FunctionRef<void(CordRep*, size_t, size_t)> consume_fn) {
  size_t offset = 0;
  size_t length = rep->length;

  if (rep->tag == SUBSTRING) {
    offset += rep->substring()->start;
    rep = ClipSubstring(rep->substring());
  }
  consume_fn(rep, offset, length);
}

void ReverseConsume(CordRep* rep,
                    FunctionRef<void(CordRep*, size_t, size_t)> consume_fn) {
  return Consume(rep, consume_fn);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
