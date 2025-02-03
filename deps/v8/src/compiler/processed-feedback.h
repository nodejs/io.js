// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PROCESSED_FEEDBACK_H_
#define V8_COMPILER_PROCESSED_FEEDBACK_H_

#include "src/compiler/heap-refs.h"

namespace v8 {
namespace internal {
namespace compiler {

class BinaryOperationFeedback;
class TypeOfOpFeedback;
class CallFeedback;
class CompareOperationFeedback;
class ElementAccessFeedback;
class ForInFeedback;
class GlobalAccessFeedback;
class InstanceOfFeedback;
class LiteralFeedback;
class MegaDOMPropertyAccessFeedback;
class NamedAccessFeedback;
class RegExpLiteralFeedback;
class TemplateObjectFeedback;

class ProcessedFeedback : public ZoneObject {
 public:
  enum Kind {
    kInsufficient,
    kBinaryOperation,
    kCall,
    kCompareOperation,
    kElementAccess,
    kForIn,
    kGlobalAccess,
    kInstanceOf,
    kTypeOf,
    kLiteral,
    kMegaDOMPropertyAccess,
    kNamedAccess,
    kRegExpLiteral,
    kTemplateObject,
  };
  Kind kind() const { return kind_; }

  FeedbackSlotKind slot_kind() const { return slot_kind_; }
  bool IsInsufficient() const { return kind() == kInsufficient; }

  BinaryOperationFeedback const& AsBinaryOperation() const;
  TypeOfOpFeedback const& AsTypeOf() const;
  CallFeedback const& AsCall() const;
  CompareOperationFeedback const& AsCompareOperation() const;
  ElementAccessFeedback const& AsElementAccess() const;
  ForInFeedback const& AsForIn() const;
  GlobalAccessFeedback const& AsGlobalAccess() const;
  InstanceOfFeedback const& AsInstanceOf() const;
  NamedAccessFeedback const& AsNamedAccess() const;
  MegaDOMPropertyAccessFeedback const& AsMegaDOMPropertyAccess() const;
  LiteralFeedback const& AsLiteral() const;
  RegExpLiteralFeedback const& AsRegExpLiteral() const;
  TemplateObjectFeedback const& AsTemplateObject() const;

 protected:
  ProcessedFeedback(Kind kind, FeedbackSlotKind slot_kind);

 private:
  Kind const kind_;
  FeedbackSlotKind const slot_kind_;
};

class InsufficientFeedback final : public ProcessedFeedback {
 public:
  explicit InsufficientFeedback(FeedbackSlotKind slot_kind);
};

class GlobalAccessFeedback : public ProcessedFeedback {
 public:
  GlobalAccessFeedback(PropertyCellRef cell, FeedbackSlotKind slot_kind);
  GlobalAccessFeedback(ContextRef script_context, int slot_index,
                       bool immutable, FeedbackSlotKind slot_kind);
  explicit GlobalAccessFeedback(FeedbackSlotKind slot_kind);  // Megamorphic

  bool IsMegamorphic() const;

  bool IsPropertyCell() const;
  PropertyCellRef property_cell() const;

  bool IsScriptContextSlot() const;
  ContextRef script_context() const;
  int slot_index() const;
  bool immutable() const;

  OptionalObjectRef GetConstantHint(JSHeapBroker* broker) const;

 private:
  OptionalObjectRef const cell_or_context_;
  int const index_and_immutable_;
};

class KeyedAccessMode {
 public:
  static KeyedAccessMode FromNexus(FeedbackNexus const& nexus);

  AccessMode access_mode() const;
  bool IsLoad() const;
  bool IsStore() const;
  KeyedAccessLoadMode load_mode() const;
  KeyedAccessStoreMode store_mode() const;

 private:
  AccessMode const access_mode_;
  union LoadStoreMode {
    LoadStoreMode(KeyedAccessLoadMode load_mode);
    LoadStoreMode(KeyedAccessStoreMode store_mode);
    KeyedAccessLoadMode load_mode;
    KeyedAccessStoreMode store_mode;
  } const load_store_mode_;

  KeyedAccessMode(AccessMode access_mode, KeyedAccessLoadMode load_mode);
  KeyedAccessMode(AccessMode access_mode, KeyedAccessStoreMode store_mode);
};

class ElementAccessFeedback : public ProcessedFeedback {
 public:
  ElementAccessFeedback(Zone* zone, KeyedAccessMode const& keyed_mode,
                        FeedbackSlotKind slot_kind);

  KeyedAccessMode keyed_mode() const;

  // A transition group is a target and a possibly empty set of sources that can
  // transition to the target. It is represented as a non-empty vector with the
  // target at index 0.
  using TransitionGroup = ZoneVector<MapRef>;
  ZoneVector<TransitionGroup> const& transition_groups() const;

  bool HasOnlyStringMaps(JSHeapBroker* broker) const;

  void AddGroup(TransitionGroup&& group);

  // Refine {this} by trying to restrict it to the maps in {inferred_maps}. A
  // transition group's target is kept iff it is in {inferred_maps} or if more
  // than one of its sources is in {inferred_maps}. Here's an (unrealistic)
  // example showing all the possible situations:
  //
  // inferred_maps = [a0, a2, c1, c2, d1, e0, e1]
  //
  // Groups before:                     Groups after:
  // [a0, a1, a2]                       [a0, a2]
  // [b0]
  // [c0, c1, c2, c3]                   [c0, c1, c2]
  // [d0, d1]                           [d1]
  // [e0, e1]                           [e0, e1]
  //
  ElementAccessFeedback const& Refine(
      JSHeapBroker* broker, ZoneVector<MapRef> const& inferred_maps) const;
  ElementAccessFeedback const& Refine(
      JSHeapBroker* broker, ZoneRefSet<Map> const& inferred_maps,
      bool always_keep_group_target = true) const;

 private:
  KeyedAccessMode const keyed_mode_;
  ZoneVector<TransitionGroup> transition_groups_;
};

class NamedAccessFeedback : public ProcessedFeedback {
 public:
  NamedAccessFeedback(NameRef name, ZoneVector<MapRef> const& maps,
                      FeedbackSlotKind slot_kind);

  NameRef name() const { return name_; }
  ZoneVector<MapRef> const& maps() const { return maps_; }

 private:
  NameRef const name_;
  ZoneVector<MapRef> const maps_;
};

class MegaDOMPropertyAccessFeedback : public ProcessedFeedback {
 public:
  MegaDOMPropertyAccessFeedback(FunctionTemplateInfoRef info_ref,
                                FeedbackSlotKind slot_kind);

  FunctionTemplateInfoRef info() const { return info_; }

 private:
  FunctionTemplateInfoRef const info_;
};

class CallFeedback : public ProcessedFeedback {
 public:
  CallFeedback(OptionalHeapObjectRef target, float frequency,
               SpeculationMode mode, CallFeedbackContent call_feedback_content,
               FeedbackSlotKind slot_kind)
      : ProcessedFeedback(kCall, slot_kind),
        target_(target),
        frequency_(frequency),
        mode_(mode),
        content_(call_feedback_content) {}

  OptionalHeapObjectRef target() const { return target_; }
  float frequency() const { return frequency_; }
  SpeculationMode speculation_mode() const { return mode_; }
  CallFeedbackContent call_feedback_content() const { return content_; }

 private:
  OptionalHeapObjectRef const target_;
  float const frequency_;
  SpeculationMode const mode_;
  CallFeedbackContent const content_;
};

template <class T, ProcessedFeedback::Kind K>
class SingleValueFeedback : public ProcessedFeedback {
 public:
  explicit SingleValueFeedback(T value, FeedbackSlotKind slot_kind)
      : ProcessedFeedback(K, slot_kind), value_(value) {
    DCHECK(
        (K == kBinaryOperation && slot_kind == FeedbackSlotKind::kBinaryOp) ||
        (K == kTypeOf && slot_kind == FeedbackSlotKind::kTypeOf) ||
        (K == kCompareOperation && slot_kind == FeedbackSlotKind::kCompareOp) ||
        (K == kForIn && slot_kind == FeedbackSlotKind::kForIn) ||
        (K == kInstanceOf && slot_kind == FeedbackSlotKind::kInstanceOf) ||
        ((K == kLiteral || K == kRegExpLiteral || K == kTemplateObject) &&
         slot_kind == FeedbackSlotKind::kLiteral));
  }

  T value() const { return value_; }

 private:
  T const value_;
};

class InstanceOfFeedback
    : public SingleValueFeedback<OptionalJSObjectRef,
                                 ProcessedFeedback::kInstanceOf> {
  using SingleValueFeedback::SingleValueFeedback;
};

class TypeOfOpFeedback
    : public SingleValueFeedback<TypeOfFeedback::Result,
                                 ProcessedFeedback::kTypeOf> {
  using SingleValueFeedback::SingleValueFeedback;
};

class LiteralFeedback
    : public SingleValueFeedback<AllocationSiteRef,
                                 ProcessedFeedback::kLiteral> {
  using SingleValueFeedback::SingleValueFeedback;
};

class RegExpLiteralFeedback
    : public SingleValueFeedback<RegExpBoilerplateDescriptionRef,
                                 ProcessedFeedback::kRegExpLiteral> {
  using SingleValueFeedback::SingleValueFeedback;
};

class TemplateObjectFeedback
    : public SingleValueFeedback<JSArrayRef,
                                 ProcessedFeedback::kTemplateObject> {
  using SingleValueFeedback::SingleValueFeedback;
};

class BinaryOperationFeedback
    : public SingleValueFeedback<BinaryOperationHint,
                                 ProcessedFeedback::kBinaryOperation> {
  using SingleValueFeedback::SingleValueFeedback;
};

class CompareOperationFeedback
    : public SingleValueFeedback<CompareOperationHint,
                                 ProcessedFeedback::kCompareOperation> {
  using SingleValueFeedback::SingleValueFeedback;
};

class ForInFeedback
    : public SingleValueFeedback<ForInHint, ProcessedFeedback::kForIn> {
  using SingleValueFeedback::SingleValueFeedback;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PROCESSED_FEEDBACK_H_
