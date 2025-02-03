// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/loop-unrolling-reducer.h"

#include <optional>

#include "src/base/bits.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/loop-finder.h"

namespace v8::internal::compiler::turboshaft {

using CmpOp = StaticCanonicalForLoopMatcher::CmpOp;
using BinOp = StaticCanonicalForLoopMatcher::BinOp;

void LoopUnrollingAnalyzer::DetectUnrollableLoops() {
  for (const auto& [start, info] : loop_finder_.LoopHeaders()) {
    IterationCount iter_count = GetLoopIterationCount(info);
    loop_iteration_count_.insert({start, iter_count});

    if (ShouldFullyUnrollLoop(start) || ShouldPartiallyUnrollLoop(start)) {
      can_unroll_at_least_one_loop_ = true;
    }

    if (iter_count.IsSmallerThan(kMaxIterForStackCheckRemoval)) {
      stack_checks_to_remove_.insert(start->index().id());
    }
  }
}

IterationCount LoopUnrollingAnalyzer::GetLoopIterationCount(
    const LoopFinder::LoopInfo& info) const {
  const Block* start = info.start;
  DCHECK(start->IsLoop());

  // Checking that the condition for the loop can be computed statically, and
  // that the loop contains no more than kMaxLoopIterationsForFullUnrolling
  // iterations.
  const BranchOp* branch =
      start->LastOperation(*input_graph_).TryCast<BranchOp>();
  if (!branch) {
    // This looks like an infinite loop, or like something weird is used to
    // decide whether to loop or not.
    return {};
  }

  // Checking that one of the successor of the loop header is indeed not in the
  // loop (otherwise, the Branch that ends the loop header is not the Branch
  // that decides to exit the loop).
  const Block* if_true_header = loop_finder_.GetLoopHeader(branch->if_true);
  const Block* if_false_header = loop_finder_.GetLoopHeader(branch->if_false);
  if (if_true_header == if_false_header) {
    return {};
  }

  // If {if_true} is in the loop, then we're looping if the condition is true,
  // but if {if_false} is in the loop, then we're looping if the condition is
  // false.
  bool loop_if_cond_is = if_true_header == start;

  return canonical_loop_matcher_.GetIterCountIfStaticCanonicalForLoop(
      start, branch->condition(), loop_if_cond_is);
}

// Tries to match `phi cmp cst` (or `cst cmp phi`).
bool StaticCanonicalForLoopMatcher::MatchPhiCompareCst(
    OpIndex cond_idx, StaticCanonicalForLoopMatcher::CmpOp* cmp_op,
    OpIndex* phi, uint64_t* cst) const {
  const Operation& cond = matcher_.Get(cond_idx);

  if (const ComparisonOp* cmp = cond.TryCast<ComparisonOp>()) {
    *cmp_op = ComparisonKindToCmpOp(cmp->kind);
  } else {
    return false;
  }

  OpIndex left = cond.input(0);
  OpIndex right = cond.input(1);

  if (matcher_.MatchPhi(left, 2)) {
    if (matcher_.MatchUnsignedIntegralConstant(right, cst)) {
      *phi = left;
      return true;
    }
  } else if (matcher_.MatchPhi(right, 2)) {
    if (matcher_.MatchUnsignedIntegralConstant(left, cst)) {
      *cmp_op = InvertComparisonOp(*cmp_op);
      *phi = right;
      return true;
    }
  }
  return false;
}

bool StaticCanonicalForLoopMatcher::MatchCheckedOverflowBinop(
    OpIndex idx, V<Word>* left, V<Word>* right, BinOp* binop_op,
    WordRepresentation* binop_rep) const {
  if (const ProjectionOp* proj = matcher_.TryCast<ProjectionOp>(idx)) {
    if (proj->index != OverflowCheckedBinopOp::kValueIndex) return false;
    if (const OverflowCheckedBinopOp* binop =
            matcher_.TryCast<OverflowCheckedBinopOp>(proj->input())) {
      *left = binop->left();
      *right = binop->right();
      *binop_op = BinopFromOverflowCheckedBinopKind(binop->kind);
      *binop_rep = binop->rep;
      return true;
    }
  }
  return false;
}

bool StaticCanonicalForLoopMatcher::MatchWordBinop(
    OpIndex idx, V<Word>* left, V<Word>* right, BinOp* binop_op,
    WordRepresentation* binop_rep) const {
  WordBinopOp::Kind kind;
  if (matcher_.MatchWordBinop(idx, left, right, &kind, binop_rep) &&
      BinopKindIsSupported(kind)) {
    *binop_op = BinopFromWordBinopKind(kind);
    return true;
  }
  return false;
}

IterationCount
StaticCanonicalForLoopMatcher::GetIterCountIfStaticCanonicalForLoop(
    const Block* header, OpIndex cond_idx, bool loop_if_cond_is) const {
  CmpOp cmp_op;
  OpIndex phi_idx;
  uint64_t cmp_cst;
  if (!MatchPhiCompareCst(cond_idx, &cmp_op, &phi_idx, &cmp_cst)) {
    return {};
  }
  if (!header->Contains(phi_idx)) {
    // The termination condition for this loop is based on a Phi that is defined
    // in another loop.
    return {};
  }

  const PhiOp& phi = matcher_.Cast<PhiOp>(phi_idx);

  // We have: phi(..., ...) cmp_op cmp_cst
  // eg, for (i = ...; i < 42; ...)
  uint64_t phi_cst;
  if (matcher_.MatchUnsignedIntegralConstant(phi.input(0), &phi_cst)) {
    // We have: phi(phi_cst, ...) cmp_op cmp_cst
    // eg, for (i = 0; i < 42; ...)
    V<Word> left, right;
    BinOp binop_op;
    WordRepresentation binop_rep;
    if (MatchWordBinop(phi.input(1), &left, &right, &binop_op, &binop_rep) ||
        MatchCheckedOverflowBinop(phi.input(1), &left, &right, &binop_op,
                                  &binop_rep)) {
      // We have: phi(phi_cst, ... binop_op ...) cmp_op cmp_cst
      // eg, for (i = 0; i < 42; i = ... + ...)
      if (left == phi_idx) {
        // We have: phi(phi_cst, phi binop_op ...) cmp_op cmp_cst
        // eg, for (i = 0; i < 42; i = i + ...)
        uint64_t binop_cst;
        if (matcher_.MatchUnsignedIntegralConstant(right, &binop_cst)) {
          // We have: phi(phi_cst, phi binop_op binop_cst) cmp_op cmp_cst
          // eg, for (i = 0; i < 42; i = i + 2)
          return CountIterations(cmp_cst, cmp_op, phi_cst, binop_cst, binop_op,
                                 binop_rep, loop_if_cond_is);
        }
      } else if (right == phi_idx) {
        // We have: phi(phi_cst, ... binop_op phi) cmp_op cmp_cst
        // eg, for (i = 0; i < 42; i = ... + i)
        uint64_t binop_cst;
        if (matcher_.MatchUnsignedIntegralConstant(left, &binop_cst)) {
          // We have: phi(phi_cst, binop_cst binop_op phi) cmp_op cmp_cst
          // eg, for (i = 0; i < 42; i = 2 + i)
          return CountIterations(cmp_cst, cmp_op, phi_cst, binop_cst, binop_op,
                                 binop_rep, loop_if_cond_is);
        }
      }
    }
  }

  // The condition is not an operation that we support.
  return {};
}

constexpr bool StaticCanonicalForLoopMatcher::BinopKindIsSupported(
    WordBinopOp::Kind binop_kind) {
  switch (binop_kind) {
    // This list needs to be kept in sync with the `Next` function that follows.
    case WordBinopOp::Kind::kAdd:
    case WordBinopOp::Kind::kMul:
    case WordBinopOp::Kind::kSub:
    case WordBinopOp::Kind::kBitwiseAnd:
    case WordBinopOp::Kind::kBitwiseOr:
    case WordBinopOp::Kind::kBitwiseXor:
      return true;
    default:
      return false;
  }
}

constexpr StaticCanonicalForLoopMatcher::BinOp
StaticCanonicalForLoopMatcher::BinopFromWordBinopKind(WordBinopOp::Kind kind) {
  DCHECK(BinopKindIsSupported(kind));
  switch (kind) {
    case WordBinopOp::Kind::kAdd:
      return BinOp::kAdd;
    case WordBinopOp::Kind::kMul:
      return BinOp::kMul;
    case WordBinopOp::Kind::kSub:
      return BinOp::kSub;
    case WordBinopOp::Kind::kBitwiseAnd:
      return BinOp::kBitwiseAnd;
    case WordBinopOp::Kind::kBitwiseOr:
      return BinOp::kBitwiseOr;
    case WordBinopOp::Kind::kBitwiseXor:
      return BinOp::kBitwiseXor;
    default:
      UNREACHABLE();
  }
}

constexpr StaticCanonicalForLoopMatcher::BinOp
StaticCanonicalForLoopMatcher::BinopFromOverflowCheckedBinopKind(
    OverflowCheckedBinopOp::Kind kind) {
  switch (kind) {
    case OverflowCheckedBinopOp::Kind::kSignedAdd:
      return BinOp::kOverflowCheckedAdd;
    case OverflowCheckedBinopOp::Kind::kSignedMul:
      return BinOp::kOverflowCheckedMul;
    case OverflowCheckedBinopOp::Kind::kSignedSub:
      return BinOp::kOverflowCheckedSub;
  }
}

std::ostream& operator<<(std::ostream& os, const IterationCount& count) {
  if (count.IsExact()) {
    return os << "Exact[" << count.exact_count() << "]";
  } else if (count.IsApprox()) {
    return os << "Approx[" << count.exact_count() << "]";
  } else {
    DCHECK(count.IsUnknown());
    return os << "Unknown";
  }
}

std::ostream& operator<<(std::ostream& os, const CmpOp& cmp) {
  switch (cmp) {
    case CmpOp::kEqual:
      return os << "==";
    case CmpOp::kSignedLessThan:
      return os << "<ˢ";
    case CmpOp::kSignedLessThanOrEqual:
      return os << "<=ˢ";
    case CmpOp::kUnsignedLessThan:
      return os << "<ᵘ";
    case CmpOp::kUnsignedLessThanOrEqual:
      return os << "<=ᵘ";
    case CmpOp::kSignedGreaterThan:
      return os << ">ˢ";
    case CmpOp::kSignedGreaterThanOrEqual:
      return os << ">=ˢ";
    case CmpOp::kUnsignedGreaterThan:
      return os << ">ᵘ";
    case CmpOp::kUnsignedGreaterThanOrEqual:
      return os << ">=ᵘ";
  }
}

std::ostream& operator<<(std::ostream& os, const BinOp& binop) {
  switch (binop) {
    case BinOp::kAdd:
      return os << "+";
    case BinOp::kMul:
      return os << "*";
    case BinOp::kSub:
      return os << "-";
    case BinOp::kBitwiseAnd:
      return os << "&";
    case BinOp::kBitwiseOr:
      return os << "|";
    case BinOp::kBitwiseXor:
      return os << "^";
    case BinOp::kOverflowCheckedAdd:
      return os << "+ᵒ";
    case BinOp::kOverflowCheckedMul:
      return os << "*ᵒ";
    case BinOp::kOverflowCheckedSub:
      return os << "-ᵒ";
  }
}

namespace {

template <class Int>
std::optional<Int> Next(Int val, Int incr,
                        StaticCanonicalForLoopMatcher::BinOp binop_op,
                        WordRepresentation binop_rep) {
  switch (binop_op) {
    case BinOp::kBitwiseAnd:
      return val & incr;
    case BinOp::kBitwiseOr:
      return val | incr;
    case BinOp::kBitwiseXor:
      return val ^ incr;
      // Even regular Add/Sub/Mul probably shouldn't under/overflow here, so we
      // check for overflow in all cases (and C++ signed integer overflow is
      // undefined behavior, so have to use something from base::bits anyways).
#define CASE_ARITH(op)                                                        \
  case BinOp::k##op:                                                          \
  case BinOp::kOverflowChecked##op: {                                         \
    if (binop_rep == WordRepresentation::Word32()) {                          \
      int32_t res;                                                            \
      if (base::bits::Signed##op##Overflow32(                                 \
              static_cast<int32_t>(val), static_cast<int32_t>(incr), &res)) { \
        return std::nullopt;                                                  \
      }                                                                       \
      return static_cast<Int>(res);                                           \
    } else {                                                                  \
      DCHECK_EQ(binop_rep, WordRepresentation::Word64());                     \
      int64_t res;                                                            \
      if (base::bits::Signed##op##Overflow64(val, incr, &res)) {              \
        return std::nullopt;                                                  \
      }                                                                       \
      return static_cast<Int>(res);                                           \
    }                                                                         \
  }
      CASE_ARITH(Add)
      CASE_ARITH(Mul)
      CASE_ARITH(Sub)
#undef CASE_CHECKED
  }
}

template <class Int>
bool Cmp(Int val, Int max, CmpOp cmp_op) {
  switch (cmp_op) {
    case CmpOp::kSignedLessThan:
    case CmpOp::kUnsignedLessThan:
      return val < max;
    case CmpOp::kSignedLessThanOrEqual:
    case CmpOp::kUnsignedLessThanOrEqual:
      return val <= max;
    case CmpOp::kSignedGreaterThan:
    case CmpOp::kUnsignedGreaterThan:
      return val > max;
    case CmpOp::kSignedGreaterThanOrEqual:
    case CmpOp::kUnsignedGreaterThanOrEqual:
      return val >= max;
    case CmpOp::kEqual:
      return val == max;
  }
}

template <class Int>
bool SubWillOverflow(Int lhs, Int rhs) {
  if constexpr (std::is_same_v<Int, int32_t> || std::is_same_v<Int, uint32_t>) {
    int32_t unused;
    return base::bits::SignedSubOverflow32(lhs, rhs, &unused);
  } else {
    static_assert(std::is_same_v<Int, int64_t> ||
                  std::is_same_v<Int, uint64_t>);
    int64_t unused;
    return base::bits::SignedSubOverflow64(lhs, rhs, &unused);
  }
}

template <class Int>
bool DivWillOverflow(Int dividend, Int divisor) {
  if constexpr (std::is_unsigned_v<Int>) {
    return false;
  } else {
    return dividend == std::numeric_limits<Int>::min() && divisor == -1;
  }
}

}  // namespace

// Returns true if the loop
// `for (i = init, i cmp_op max; i = i binop_op binop_cst)` has fewer than
// `max_iter_` iterations.
template <class Int>
IterationCount StaticCanonicalForLoopMatcher::CountIterationsImpl(
    Int init, Int max, CmpOp cmp_op, Int binop_cst, BinOp binop_op,
    WordRepresentation binop_rep, bool loop_if_cond_is) const {
  static_assert(std::is_integral_v<Int>);
  DCHECK_EQ(std::is_unsigned_v<Int>,
            (cmp_op == CmpOp::kUnsignedLessThan ||
             cmp_op == CmpOp::kUnsignedLessThanOrEqual ||
             cmp_op == CmpOp::kUnsignedGreaterThan ||
             cmp_op == CmpOp::kUnsignedGreaterThanOrEqual));

  // It's a bit hard to compute the number of iterations without some kind of
  // (simple) SMT solver, especially when taking overflows into account. Thus,
  // we just simulate the evolution of the loop counter: we repeatedly compute
  // `init binop_op binop_cst`, and compare the result with `max`. This is
  // somewhat inefficient, so it should only be done if `kMaxExactIter` is
  // small.
  DCHECK_LE(kMaxExactIter, 10);

  Int curr = init;
  size_t iter_count = 0;
  for (; iter_count < kMaxExactIter; iter_count++) {
    if (Cmp(curr, max, cmp_op) != loop_if_cond_is) {
      return IterationCount::Exact(iter_count);
    }
    if (auto next = Next(curr, binop_cst, binop_op, binop_rep)) {
      curr = *next;
    } else {
      // There was an overflow, bailing out.
      break;
    }
  }

  if (binop_cst == 0) {
    // If {binop_cst} is 0, the loop should either execute a single time or loop
    // infinitely (since the increment is in the form of "i = i op binop_cst"
    // with op being an arithmetic or bitwise binop). If we didn't detect above
    // that it executes a single time, then we are in the latter case.
    return {};
  }

  // Trying to figure out an approximate number of iterations
  if (binop_op == StaticCanonicalForLoopMatcher::BinOp::kAdd) {
    if (cmp_op ==
            any_of(CmpOp::kUnsignedLessThan, CmpOp::kUnsignedLessThanOrEqual,
                   CmpOp::kSignedLessThan, CmpOp::kSignedLessThanOrEqual) &&
        init < max && !SubWillOverflow(max, init) && loop_if_cond_is) {
      // eg, for (int i = 0; i < 42; i += 2)
      if (binop_cst < 0) {
        // Will either loop forever or rely on underflow wrap-around to
        // eventually stop.
        return {};
      }
      DCHECK(!DivWillOverflow(max - init, binop_cst));
      Int quotient = (max - init) / binop_cst;
      DCHECK_GE(quotient, 0);
      return IterationCount::Approx(quotient);
    }
    if (cmp_op == any_of(CmpOp::kUnsignedGreaterThan,
                         CmpOp::kUnsignedGreaterThanOrEqual,
                         CmpOp::kSignedGreaterThan,
                         CmpOp::kSignedGreaterThanOrEqual) &&
        init > max && !SubWillOverflow(max, init) && loop_if_cond_is) {
      // eg, for (int i = 42; i > 0; i += -2)
      if (binop_cst > 0) {
        // Will either loop forever or rely on overflow wrap-around to
        // eventually stop.
        return {};
      }
      if (DivWillOverflow(max - init, binop_cst)) return {};
      Int quotient = (max - init) / binop_cst;
      DCHECK_GE(quotient, 0);
      return IterationCount::Approx(quotient);
    }
    if (cmp_op == CmpOp::kEqual && !SubWillOverflow(max, init) &&
        !loop_if_cond_is) {
      // eg, for (int i = 0;  i != 42; i += 2)
      // or, for (int i = 42; i != 0;  i += -2)
      if (init < max && binop_cst < 0) {
        // Will either loop forever or rely on underflow wrap-around to
        // eventually stop.
        return {};
      }
      if (init > max && binop_cst > 0) {
        // Will either loop forever or rely on overflow wrap-around to
        // eventually stop.
        return {};
      }

      Int remainder = (max - init) % binop_cst;
      if (remainder != 0) {
        // Will loop forever or rely on over/underflow wrap-around to eventually
        // stop.
        return {};
      }

      Int quotient = (max - init) / binop_cst;
      DCHECK_GE(quotient, 0);
      return IterationCount::Approx(quotient);
    }
  }

  return {};
}

// Returns true if the loop
// `for (i = initial_input, i cmp_op cmp_cst; i = i binop_op binop_cst)` has
// fewer than `max_iter_` iterations.
IterationCount StaticCanonicalForLoopMatcher::CountIterations(
    uint64_t cmp_cst, CmpOp cmp_op, uint64_t initial_input, uint64_t binop_cst,
    BinOp binop_op, WordRepresentation binop_rep, bool loop_if_cond_is) const {
  switch (cmp_op) {
    case CmpOp::kSignedLessThan:
    case CmpOp::kSignedLessThanOrEqual:
    case CmpOp::kSignedGreaterThan:
    case CmpOp::kSignedGreaterThanOrEqual:
    case CmpOp::kEqual:
      if (binop_rep == WordRepresentation::Word32()) {
        return CountIterationsImpl<int32_t>(
            static_cast<int32_t>(initial_input), static_cast<int32_t>(cmp_cst),
            cmp_op, static_cast<int32_t>(binop_cst), binop_op, binop_rep,
            loop_if_cond_is);
      } else {
        DCHECK_EQ(binop_rep, WordRepresentation::Word64());
        return CountIterationsImpl<int64_t>(
            static_cast<int64_t>(initial_input), static_cast<int64_t>(cmp_cst),
            cmp_op, static_cast<int64_t>(binop_cst), binop_op, binop_rep,
            loop_if_cond_is);
      }
    case CmpOp::kUnsignedLessThan:
    case CmpOp::kUnsignedLessThanOrEqual:
    case CmpOp::kUnsignedGreaterThan:
    case CmpOp::kUnsignedGreaterThanOrEqual:
      if (binop_rep == WordRepresentation::Word32()) {
        return CountIterationsImpl<uint32_t>(
            static_cast<uint32_t>(initial_input),
            static_cast<uint32_t>(cmp_cst), cmp_op,
            static_cast<uint32_t>(binop_cst), binop_op, binop_rep,
            loop_if_cond_is);
      } else {
        DCHECK_EQ(binop_rep, WordRepresentation::Word64());
        return CountIterationsImpl<uint64_t>(initial_input, cmp_cst, cmp_op,
                                             binop_cst, binop_op, binop_rep,
                                             loop_if_cond_is);
      }
  }
}

constexpr StaticCanonicalForLoopMatcher::CmpOp
StaticCanonicalForLoopMatcher::ComparisonKindToCmpOp(ComparisonOp::Kind kind) {
  switch (kind) {
    case ComparisonOp::Kind::kEqual:
      return CmpOp::kEqual;
    case ComparisonOp::Kind::kSignedLessThan:
      return CmpOp::kSignedLessThan;
    case ComparisonOp::Kind::kSignedLessThanOrEqual:
      return CmpOp::kSignedLessThanOrEqual;
    case ComparisonOp::Kind::kUnsignedLessThan:
      return CmpOp::kUnsignedLessThan;
    case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
      return CmpOp::kUnsignedLessThanOrEqual;
  }
}
constexpr StaticCanonicalForLoopMatcher::CmpOp
StaticCanonicalForLoopMatcher::InvertComparisonOp(CmpOp op) {
  switch (op) {
    case CmpOp::kEqual:
      return CmpOp::kEqual;
    case CmpOp::kSignedLessThan:
      return CmpOp::kSignedGreaterThan;
    case CmpOp::kSignedLessThanOrEqual:
      return CmpOp::kSignedGreaterThanOrEqual;
    case CmpOp::kUnsignedLessThan:
      return CmpOp::kUnsignedGreaterThan;
    case CmpOp::kUnsignedLessThanOrEqual:
      return CmpOp::kUnsignedGreaterThanOrEqual;
    case CmpOp::kSignedGreaterThan:
      return CmpOp::kSignedLessThan;
    case CmpOp::kSignedGreaterThanOrEqual:
      return CmpOp::kSignedLessThanOrEqual;
    case CmpOp::kUnsignedGreaterThan:
      return CmpOp::kUnsignedLessThan;
    case CmpOp::kUnsignedGreaterThanOrEqual:
      return CmpOp::kUnsignedLessThanOrEqual;
  }
}

}  // namespace v8::internal::compiler::turboshaft
