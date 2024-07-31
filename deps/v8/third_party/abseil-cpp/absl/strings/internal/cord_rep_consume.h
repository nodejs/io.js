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

#ifndef ABSL_STRINGS_INTERNAL_CORD_REP_CONSUME_H_
#define ABSL_STRINGS_INTERNAL_CORD_REP_CONSUME_H_

#include <functional>

#include "absl/functional/function_ref.h"
#include "absl/strings/internal/cord_internal.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// Consume() and ReverseConsume() consume CONCAT based trees and invoke the
// provided functor with the contained nodes in the proper forward or reverse
// order, which is used to convert CONCAT trees into other tree or cord data.
// All CONCAT and SUBSTRING nodes are processed internally. The 'offset`
// parameter of the functor is non-zero for any nodes below SUBSTRING nodes.
// It's up to the caller to form these back into SUBSTRING nodes or otherwise
// store offset / prefix information. These functions are intended to be used
// only for migration / transitional code where due to factors such as ODR
// violations, we can not 100% guarantee that all code respects 'new format'
// settings and flags, so we need to be able to parse old data on the fly until
// all old code is deprecated / no longer the default format.
void Consume(CordRep* rep,
             FunctionRef<void(CordRep*, size_t, size_t)> consume_fn);
void ReverseConsume(CordRep* rep,
                    FunctionRef<void(CordRep*, size_t, size_t)> consume_fn);

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_REP_CONSUME_H_
