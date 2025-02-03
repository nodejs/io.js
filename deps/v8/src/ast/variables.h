// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_VARIABLES_H_
#define V8_AST_VARIABLES_H_

#include "src/ast/ast-value-factory.h"
#include "src/base/threaded-list.h"
#include "src/common/globals.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

// The AST refers to variables via VariableProxies - placeholders for the actual
// variables. Variables themselves are never directly referred to from the AST,
// they are maintained by scopes, and referred to from VariableProxies and Slots
// after binding and variable allocation.
class Variable final : public ZoneObject {
 public:
  Variable(Scope* scope, const AstRawString* name, VariableMode mode,
           VariableKind kind, InitializationFlag initialization_flag,
           MaybeAssignedFlag maybe_assigned_flag = kNotAssigned,
           IsStaticFlag is_static_flag = IsStaticFlag::kNotStatic)
      : scope_(scope),
        name_(name),
        local_if_not_shadowed_(nullptr),
        next_(nullptr),
        index_(-1),
        initializer_position_(kNoSourcePosition),
        bit_field_(MaybeAssignedFlagField::encode(maybe_assigned_flag) |
                   InitializationFlagField::encode(initialization_flag) |
                   VariableModeField::encode(mode) |
                   IsUsedField::encode(false) |
                   ForceContextAllocationBit::encode(false) |
                   LocationField::encode(VariableLocation::UNALLOCATED) |
                   VariableKindField::encode(kind) |
                   IsStaticFlagField::encode(is_static_flag)),
        hole_check_analysis_bit_field_(HoleCheckBitmapIndexField::encode(
                                           kUncacheableHoleCheckBitmapIndex) |
                                       ForceHoleInitializationFlagField::encode(
                                           kHoleInitializationNotForced)) {
    // Var declared variables never need initialization.
    DCHECK(!(mode == VariableMode::kVar &&
             initialization_flag == kNeedsInitialization));
    DCHECK_IMPLIES(is_static_flag == IsStaticFlag::kStatic,
                   IsImmutableLexicalOrPrivateVariableMode(mode));
  }

  explicit Variable(Variable* other);

  // The source code for an eval() call may refer to a variable that is
  // in an outer scope about which we don't know anything (it may not
  // be the script scope). scope() is nullptr in that case. Currently the
  // scope is only used to follow the context chain length.
  Scope* scope() const { return scope_; }

  // This is for adjusting the scope of temporaries used when desugaring
  // parameter initializers.
  void set_scope(Scope* scope) { scope_ = scope; }

  Handle<String> name() const { return name_->string(); }
  const AstRawString* raw_name() const { return name_; }
  VariableMode mode() const { return VariableModeField::decode(bit_field_); }
  void set_mode(VariableMode mode) {
    bit_field_ = VariableModeField::update(bit_field_, mode);
  }
  void set_is_static_flag(IsStaticFlag is_static_flag) {
    bit_field_ = IsStaticFlagField::update(bit_field_, is_static_flag);
  }
  IsStaticFlag is_static_flag() const {
    return IsStaticFlagField::decode(bit_field_);
  }
  bool is_static() const { return is_static_flag() == IsStaticFlag::kStatic; }

  bool has_forced_context_allocation() const {
    return ForceContextAllocationBit::decode(bit_field_);
  }
  void ForceContextAllocation() {
    DCHECK(IsUnallocated() || IsContextSlot() || IsLookupSlot() ||
           location() == VariableLocation::MODULE);
    bit_field_ = ForceContextAllocationBit::update(bit_field_, true);
  }
  bool is_used() { return IsUsedField::decode(bit_field_); }
  void set_is_used() { bit_field_ = IsUsedField::update(bit_field_, true); }
  MaybeAssignedFlag maybe_assigned() const {
    return MaybeAssignedFlagField::decode(bit_field_);
  }
  void clear_maybe_assigned() {
    bit_field_ = MaybeAssignedFlagField::update(bit_field_, kNotAssigned);
  }
  void SetMaybeAssigned() {
    if (IsImmutableLexicalVariableMode(mode())) {
      return;
    }
    // Private names are only initialized once by us.
    if (name_->IsPrivateName()) {
      return;
    }
    // If this variable is dynamically shadowing another variable, then that
    // variable could also be assigned (in the non-shadowing case).
    if (has_local_if_not_shadowed()) {
      // Avoid repeatedly marking the same tree of variables by only recursing
      // when this variable's maybe_assigned status actually changes.
      if (!maybe_assigned()) {
        local_if_not_shadowed()->SetMaybeAssigned();
      }
      DCHECK_IMPLIES(
          (!IsImmutableLexicalVariableMode(local_if_not_shadowed()->mode())),
          local_if_not_shadowed()->maybe_assigned());
    }
    set_maybe_assigned();
  }

  bool requires_brand_check() const {
    return IsPrivateMethodOrAccessorVariableMode(mode());
  }

  int initializer_position() { return initializer_position_; }
  void set_initializer_position(int pos) { initializer_position_ = pos; }

  bool IsUnallocated() const {
    return location() == VariableLocation::UNALLOCATED;
  }
  bool IsParameter() const { return location() == VariableLocation::PARAMETER; }
  bool IsStackLocal() const { return location() == VariableLocation::LOCAL; }
  bool IsStackAllocated() const { return IsParameter() || IsStackLocal(); }
  bool IsContextSlot() const { return location() == VariableLocation::CONTEXT; }
  bool IsLookupSlot() const { return location() == VariableLocation::LOOKUP; }
  bool IsGlobalObjectProperty() const;

  // True for 'let' and 'const' variables declared in the script scope of a REPL
  // script.
  bool IsReplGlobal() const;

  bool is_dynamic() const { return IsDynamicVariableMode(mode()); }

  // Returns the InitializationFlag this Variable was created with.
  // Scope analysis may allow us to relax this initialization
  // requirement, which will be reflected in the return value of
  // binding_needs_init().
  InitializationFlag initialization_flag() const {
    return InitializationFlagField::decode(bit_field_);
  }

  // Whether this variable needs to be initialized with the hole at
  // declaration time. Only returns valid results after scope analysis.
  bool binding_needs_init() const {
    DCHECK_IMPLIES(initialization_flag() == kNeedsInitialization,
                   IsLexicalVariableMode(mode()) ||
                       IsPrivateMethodOrAccessorVariableMode(mode()));
    DCHECK_IMPLIES(IsHoleInitializationForced(),
                   initialization_flag() == kNeedsInitialization);

    // Always initialize if hole initialization was forced during
    // scope analysis.
    if (IsHoleInitializationForced()) return true;

    // If initialization was not forced, no need for initialization
    // for stack allocated variables, since UpdateNeedsHoleCheck()
    // in scopes.cc has proven that no VariableProxy refers to
    // this variable in such a way that a runtime hole check
    // would be generated.
    if (IsStackAllocated()) return false;

    // Otherwise, defer to the flag set when this Variable was constructed.
    return initialization_flag() == kNeedsInitialization;
  }

  enum ForceHoleInitializationFlag {
    kHoleInitializationNotForced = 0,
    kHasHoleCheckUseInDifferentClosureScope = 1 << 0,
    kHasHoleCheckUseInSameClosureScope = 1 << 1,
    kHasHoleCheckUseInUnknownScope = kHasHoleCheckUseInDifferentClosureScope |
                                     kHasHoleCheckUseInSameClosureScope
  };
  ForceHoleInitializationFlag force_hole_initialization_flag_field() const {
    return ForceHoleInitializationFlagField::decode(
        hole_check_analysis_bit_field_);
  }

  bool IsHoleInitializationForced() const {
    return force_hole_initialization_flag_field() !=
           kHoleInitializationNotForced;
  }

  bool HasHoleCheckUseInSameClosureScope() const {
    return force_hole_initialization_flag_field() &
           kHasHoleCheckUseInSameClosureScope;
  }

  // Called during scope analysis when a VariableProxy is found to
  // reference this Variable in such a way that a hole check will
  // be required at runtime.
  void ForceHoleInitialization(ForceHoleInitializationFlag flag) {
    DCHECK_EQ(kNeedsInitialization, initialization_flag());
    DCHECK_NE(kHoleInitializationNotForced, flag);
    DCHECK(IsLexicalVariableMode(mode()) ||
           IsPrivateMethodOrAccessorVariableMode(mode()));
    hole_check_analysis_bit_field_ |=
        ForceHoleInitializationFlagField::encode(flag);
  }

  // The first N-1 lexical bindings that need hole checks in a compilation are
  // numbered, where N is the number of bits in HoleCheckBitmap. This number is
  // an index into a bitmap that the BytecodeGenerator uses to elide redundant
  // hole checks.
  using HoleCheckBitmap = uint64_t;

  // The 0th index is reserved for bindings for which the BytecodeGenerator
  // should not elide hole checks, such as for bindings beyond the first N-1.
  //
  // This index in the bitmap must always be 0.
  static constexpr uint8_t kUncacheableHoleCheckBitmapIndex = 0;
  static constexpr uint8_t kHoleCheckBitmapBits =
      std::numeric_limits<HoleCheckBitmap>::digits;

  void ResetHoleCheckBitmapIndex() {
    hole_check_analysis_bit_field_ = HoleCheckBitmapIndexField::update(
        hole_check_analysis_bit_field_, kUncacheableHoleCheckBitmapIndex);
  }

  void RememberHoleCheckInBitmap(HoleCheckBitmap& bitmap,
                                 ZoneVector<Variable*>& list) {
    DCHECK(v8_flags.ignition_elide_redundant_tdz_checks);
    uint8_t index = HoleCheckBitmapIndex();
    if (V8_UNLIKELY(index == kUncacheableHoleCheckBitmapIndex)) {
      index = list.size() + 1;
      // The bitmap is full.
      if (index == kHoleCheckBitmapBits) return;
      AssignHoleCheckBitmapIndex(list, index);
    }
    bitmap |= HoleCheckBitmap{1} << index;
    DCHECK_EQ(
        0, bitmap & (HoleCheckBitmap{1} << kUncacheableHoleCheckBitmapIndex));
  }

  bool HasRememberedHoleCheck(HoleCheckBitmap bitmap) const {
    uint8_t index = HoleCheckBitmapIndex();
    bool result = bitmap & (HoleCheckBitmap{1} << index);
    DCHECK_IMPLIES(index == kUncacheableHoleCheckBitmapIndex, !result);
    return result;
  }

  bool throw_on_const_assignment(LanguageMode language_mode) const {
    return kind() != SLOPPY_FUNCTION_NAME_VARIABLE || is_strict(language_mode);
  }

  bool is_this() const { return kind() == THIS_VARIABLE; }
  bool is_sloppy_function_name() const {
    return kind() == SLOPPY_FUNCTION_NAME_VARIABLE;
  }

  bool is_parameter() const { return kind() == PARAMETER_VARIABLE; }
  bool is_sloppy_block_function() {
    return kind() == SLOPPY_BLOCK_FUNCTION_VARIABLE;
  }

  Variable* local_if_not_shadowed() const {
    DCHECK((mode() == VariableMode::kDynamicLocal ||
            mode() == VariableMode::kDynamic) &&
           has_local_if_not_shadowed());
    return local_if_not_shadowed_;
  }

  bool has_local_if_not_shadowed() const {
    return local_if_not_shadowed_ != nullptr;
  }

  void set_local_if_not_shadowed(Variable* local) {
    local_if_not_shadowed_ = local;
  }

  VariableLocation location() const {
    return LocationField::decode(bit_field_);
  }
  VariableKind kind() const { return VariableKindField::decode(bit_field_); }

  int index() const { return index_; }

  bool IsReceiver() const {
    DCHECK(IsParameter());

    return index_ == -1;
  }

  bool IsExport() const {
    DCHECK_EQ(location(), VariableLocation::MODULE);
    DCHECK_NE(index(), 0);
    return index() > 0;
  }

  void AllocateTo(VariableLocation location, int index) {
    DCHECK(IsUnallocated() ||
           (this->location() == location && this->index() == index));
    DCHECK_IMPLIES(location == VariableLocation::MODULE, index != 0);
    bit_field_ = LocationField::update(bit_field_, location);
    DCHECK_EQ(location, this->location());
    index_ = index;
  }

  void MakeParameterNonSimple() {
    DCHECK(is_parameter());
    bit_field_ = VariableModeField::update(bit_field_, VariableMode::kLet);
    bit_field_ =
        InitializationFlagField::update(bit_field_, kNeedsInitialization);
  }

  static InitializationFlag DefaultInitializationFlag(VariableMode mode) {
    DCHECK(IsDeclaredVariableMode(mode));
    return mode == VariableMode::kVar ? kCreatedInitialized
                                      : kNeedsInitialization;
  }

  // Rewrites the VariableLocation of repl script scope 'lets' to REPL_GLOBAL.
  void RewriteLocationForRepl();

  using List = base::ThreadedList<Variable>;

 private:
  Scope* scope_;
  const AstRawString* name_;

  // If this field is set, this variable references the stored locally bound
  // variable, but it might be shadowed by variable bindings introduced by with
  // blocks or sloppy 'eval' calls between the reference scope (inclusive) and
  // the binding scope (exclusive).
  Variable* local_if_not_shadowed_;
  Variable* next_;
  int index_;
  int initializer_position_;
  uint16_t bit_field_;
  uint16_t hole_check_analysis_bit_field_;

  void set_maybe_assigned() {
    bit_field_ = MaybeAssignedFlagField::update(bit_field_, kMaybeAssigned);
  }

  uint8_t HoleCheckBitmapIndex() const {
    return HoleCheckBitmapIndexField::decode(hole_check_analysis_bit_field_);
  }

  void AssignHoleCheckBitmapIndex(ZoneVector<Variable*>& list,
                                  uint8_t next_index);

  using VariableModeField = base::BitField16<VariableMode, 0, 4>;
  using VariableKindField = VariableModeField::Next<VariableKind, 3>;
  using LocationField = VariableKindField::Next<VariableLocation, 3>;
  using ForceContextAllocationBit = LocationField::Next<bool, 1>;
  using IsUsedField = ForceContextAllocationBit::Next<bool, 1>;
  using InitializationFlagField = IsUsedField::Next<InitializationFlag, 1>;
  using MaybeAssignedFlagField =
      InitializationFlagField::Next<MaybeAssignedFlag, 1>;
  using IsStaticFlagField = MaybeAssignedFlagField::Next<IsStaticFlag, 1>;

  using HoleCheckBitmapIndexField = base::BitField16<uint8_t, 0, 8>;
  using ForceHoleInitializationFlagField =
      HoleCheckBitmapIndexField::Next<ForceHoleInitializationFlag, 2>;

  Variable** next() { return &next_; }
  friend List;
  friend base::ThreadedListTraits<Variable>;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_AST_VARIABLES_H_
