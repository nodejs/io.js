// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_PARAMETER_DIFFERENCE_H_
#define V8_TORQUE_PARAMETER_DIFFERENCE_H_

#include <optional>
#include <vector>

#include "src/torque/types.h"

namespace v8::internal::torque {

class ParameterDifference {
 public:
  ParameterDifference(const TypeVector& to, const TypeVector& from) {
    DCHECK_EQ(to.size(), from.size());
    for (size_t i = 0; i < to.size(); ++i) {
      AddParameter(to[i], from[i]);
    }
  }

  // An overload is selected if it is strictly better than all alternatives.
  // This means that it has to be strictly better in at least one parameter,
  // and better or equally good in all others.
  //
  // When comparing a pair of corresponding parameters of two overloads...
  // ... they are considered equally good if:
  //     - They are equal.
  //     - Both require some implicit conversion.
  // ... one is considered better if:
  //     - It is a strict subtype of the other.
  //     - It doesn't require an implicit conversion, while the other does.
  bool StrictlyBetterThan(const ParameterDifference& other) const {
    DCHECK_EQ(difference_.size(), other.difference_.size());
    bool better_parameter_found = false;
    for (size_t i = 0; i < difference_.size(); ++i) {
      std::optional<const Type*> a = difference_[i];
      std::optional<const Type*> b = other.difference_[i];
      if (a == b) {
        continue;
      } else if (a && b && a != b && (*a)->IsSubtypeOf(*b)) {
        DCHECK(!(*b)->IsSubtypeOf(*a));
        better_parameter_found = true;
      } else if (a && !b) {
        better_parameter_found = true;
      } else {
        return false;
      }
    }
    return better_parameter_found;
  }

 private:
  // Pointwise difference between call arguments and a signature.
  // {std::nullopt} means that an implicit conversion was necessary,
  // otherwise we store the supertype found in the signature.
  std::vector<std::optional<const Type*>> difference_;

  void AddParameter(const Type* to, const Type* from) {
    if (from->IsSubtypeOf(to)) {
      difference_.push_back(to);
    } else if (IsAssignableFrom(to, from)) {
      difference_.push_back(std::nullopt);
    } else {
      UNREACHABLE();
    }
  }
};

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_PARAMETER_DIFFERENCE_H_
