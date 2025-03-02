// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_FEEDBACK_SOURCE_H_
#define V8_COMPILER_FEEDBACK_SOURCE_H_

#include "src/compiler/heap-refs.h"
#include "src/objects/feedback-vector.h"

namespace v8 {
namespace internal {
namespace compiler {

struct FeedbackSource {
  FeedbackSource() { DCHECK(!IsValid()); }
  V8_EXPORT_PRIVATE FeedbackSource(IndirectHandle<FeedbackVector> vector_,
                                   FeedbackSlot slot_);
  FeedbackSource(FeedbackVectorRef vector_, FeedbackSlot slot_);

  bool IsValid() const { return !vector.is_null() && !slot.IsInvalid(); }
  int index() const;

  IndirectHandle<FeedbackVector> vector;
  FeedbackSlot slot;

  struct Hash {
    size_t operator()(FeedbackSource const& source) const {
      return base::hash_combine(source.vector.address(), source.slot);
    }
  };

  struct Equal {
    bool operator()(FeedbackSource const& lhs,
                    FeedbackSource const& rhs) const {
      return lhs.vector.equals(rhs.vector) && lhs.slot == rhs.slot;
    }
  };
};

bool operator==(FeedbackSource const&, FeedbackSource const&);
bool operator!=(FeedbackSource const&, FeedbackSource const&);

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           FeedbackSource const&);
inline size_t hash_value(const FeedbackSource& value) {
  return FeedbackSource::Hash()(value);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_FEEDBACK_SOURCE_H_
