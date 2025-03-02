// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/feedback-source.h"

namespace v8 {
namespace internal {
namespace compiler {

FeedbackSource::FeedbackSource(IndirectHandle<FeedbackVector> vector_,
                               FeedbackSlot slot_)
    : vector(vector_), slot(slot_) {
  DCHECK(!slot.IsInvalid());
}

FeedbackSource::FeedbackSource(FeedbackVectorRef vector_, FeedbackSlot slot_)
    : FeedbackSource(vector_.object(), slot_) {}

int FeedbackSource::index() const {
  CHECK(IsValid());
  return FeedbackVector::GetIndex(slot);
}

bool operator==(FeedbackSource const& lhs, FeedbackSource const& rhs) {
  return FeedbackSource::Equal()(lhs, rhs);
}

bool operator!=(FeedbackSource const& lhs, FeedbackSource const& rhs) {
  return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const FeedbackSource& p) {
  if (p.IsValid()) {
    return os << "FeedbackSource(" << p.slot << ")";
  }
  return os << "FeedbackSource(INVALID)";
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
