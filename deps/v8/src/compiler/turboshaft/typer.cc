// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/typer.h"

namespace v8::internal::compiler::turboshaft {

void Typer::BranchRefinements::RefineTypes(const Operation& condition,
                                           bool then_branch, Zone* zone) {
  if (const ComparisonOp* comparison = condition.TryCast<ComparisonOp>()) {
    Type lhs = type_getter_(comparison->left());
    Type rhs = type_getter_(comparison->right());

    bool is_signed, is_less_than;
    switch (comparison->kind) {
      case ComparisonOp::Kind::kEqual:
        // TODO(nicohartmann@): Add support for equality.
        return;
      case ComparisonOp::Kind::kSignedLessThan:
        is_signed = true;
        is_less_than = true;
        break;
      case ComparisonOp::Kind::kSignedLessThanOrEqual:
        is_signed = true;
        is_less_than = false;
        break;
      case ComparisonOp::Kind::kUnsignedLessThan:
        is_signed = false;
        is_less_than = true;
        break;
      case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
        is_signed = false;
        is_less_than = false;
        break;
    }

    Type l_refined;
    Type r_refined;

    if (lhs.IsNone() || rhs.IsNone()) {
      type_refiner_(comparison->left(), Type::None());
      type_refiner_(comparison->right(), Type::None());
      return;
    } else if (lhs.IsAny() || rhs.IsAny()) {
      // If either side has any type, there is not much we can do.
      return;
    }

    switch (comparison->rep.value()) {
      case RegisterRepresentation::Word32(): {
        if (is_signed) {
          // TODO(nicohartmann@): Support signed comparison.
          return;
        }
        Word32Type l = Typer::TruncateWord32Input(lhs, true, zone).AsWord32();
        Word32Type r = Typer::TruncateWord32Input(rhs, true, zone).AsWord32();
        Type l_restrict, r_restrict;
        using OpTyper = WordOperationTyper<32>;
        if (is_less_than) {
          std::tie(l_restrict, r_restrict) =
              then_branch
                  ? OpTyper::RestrictionForUnsignedLessThan_True(l, r, zone)
                  : OpTyper::RestrictionForUnsignedLessThan_False(l, r, zone);
        } else {
          std::tie(l_restrict, r_restrict) =
              then_branch
                  ? OpTyper::RestrictionForUnsignedLessThanOrEqual_True(l, r,
                                                                        zone)
                  : OpTyper::RestrictionForUnsignedLessThanOrEqual_False(l, r,
                                                                         zone);
        }

        // Special handling for word32 restriction, because the inputs might
        // have been truncated from word64 implicitly.
        l_refined = RefineWord32Type<true>(lhs, l_restrict, zone);
        r_refined = RefineWord32Type<true>(rhs, r_restrict, zone);
        break;
      }
      case RegisterRepresentation::Float64(): {
        Float64Type l = lhs.AsFloat64();
        Float64Type r = rhs.AsFloat64();
        Type l_restrict, r_restrict;
        using OpTyper = FloatOperationTyper<64>;
        if (is_less_than) {
          std::tie(l_restrict, r_restrict) =
              then_branch ? OpTyper::RestrictionForLessThan_True(l, r, zone)
                          : OpTyper::RestrictionForLessThan_False(l, r, zone);
        } else {
          std::tie(l_restrict, r_restrict) =
              then_branch
                  ? OpTyper::RestrictionForLessThanOrEqual_True(l, r, zone)
                  : OpTyper::RestrictionForLessThanOrEqual_False(l, r, zone);
        }

        l_refined = l_restrict.IsNone() ? Type::None()
                                        : Float64Type::Intersect(
                                              l, l_restrict.AsFloat64(), zone);
        r_refined = r_restrict.IsNone() ? Type::None()
                                        : Float64Type::Intersect(
                                              r, r_restrict.AsFloat64(), zone);
        break;
      }
      default:
        return;
    }

    // In some cases, the refined type is not a subtype of the old type,
    // because it cannot be represented precisely. In this case we keep the
    // old type to be stable.
    if (l_refined.IsSubtypeOf(lhs)) {
      type_refiner_(comparison->left(), l_refined);
    }
    if (r_refined.IsSubtypeOf(rhs)) {
      type_refiner_(comparison->right(), r_refined);
    }
  }
}

}  // namespace v8::internal::compiler::turboshaft
