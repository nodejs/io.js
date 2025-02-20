// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-operator.h"

#include <limits>

#include "src/base/lazy-instance.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/operator.h"
#include "src/handles/handles-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/template-objects.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Returns properties for the given binary op.
constexpr Operator::Properties BinopProperties(Operator::Opcode opcode) {
  DCHECK(JSOperator::IsBinaryWithFeedback(opcode));
  return opcode == IrOpcode::kJSStrictEqual ? Operator::kPure
                                            : Operator::kNoProperties;
}

template <class T>
Address AddressOrNull(OptionalRef<T> ref) {
  if (!ref.has_value()) return kNullAddress;
  return ref->object().address();
}

}  // namespace

namespace js_node_wrapper_utils {

TNode<Oddball> UndefinedConstant(JSGraph* jsgraph) {
  return TNode<Oddball>::UncheckedCast(jsgraph->UndefinedConstant());
}

}  // namespace js_node_wrapper_utils

FeedbackCellRef JSCreateClosureNode::GetFeedbackCellRefChecked(
    JSHeapBroker* broker) const {
  HeapObjectMatcher m(feedback_cell());
  CHECK(m.HasResolvedValue());
  return MakeRef(broker, Cast<FeedbackCell>(m.ResolvedValue()));
}

std::ostream& operator<<(std::ostream& os, CallFrequency const& f) {
  if (f.IsUnknown()) return os << "unknown";
  return os << f.value();
}

std::ostream& operator<<(std::ostream& os,
                         ConstructForwardVarargsParameters const& p) {
  return os << p.arity() << ", " << p.start_index();
}

ConstructForwardVarargsParameters const& ConstructForwardVarargsParametersOf(
    Operator const* op) {
  DCHECK_EQ(IrOpcode::kJSConstructForwardVarargs, op->opcode());
  return OpParameter<ConstructForwardVarargsParameters>(op);
}

bool operator==(ConstructParameters const& lhs,
                ConstructParameters const& rhs) {
  return lhs.arity() == rhs.arity() && lhs.frequency() == rhs.frequency() &&
         lhs.feedback() == rhs.feedback();
}

bool operator!=(ConstructParameters const& lhs,
                ConstructParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(ConstructParameters const& p) {
  return base::hash_combine(p.arity(), p.frequency(),
                            FeedbackSource::Hash()(p.feedback()));
}

std::ostream& operator<<(std::ostream& os, ConstructParameters const& p) {
  return os << p.arity() << ", " << p.frequency();
}

ConstructParameters const& ConstructParametersOf(Operator const* op) {
  DCHECK(op->opcode() == IrOpcode::kJSConstruct ||
         op->opcode() == IrOpcode::kJSConstructWithArrayLike ||
         op->opcode() == IrOpcode::kJSConstructWithSpread ||
         op->opcode() == IrOpcode::kJSConstructForwardAllArgs);
  return OpParameter<ConstructParameters>(op);
}

std::ostream& operator<<(std::ostream& os, CallParameters const& p) {
  return os << p.arity() << ", " << p.frequency() << ", " << p.convert_mode()
            << ", " << p.speculation_mode() << ", " << p.feedback_relation();
}

const CallParameters& CallParametersOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSCall ||
         op->opcode() == IrOpcode::kJSCallWithArrayLike ||
         op->opcode() == IrOpcode::kJSCallWithSpread);
  return OpParameter<CallParameters>(op);
}

std::ostream& operator<<(std::ostream& os,
                         CallForwardVarargsParameters const& p) {
  return os << p.arity() << ", " << p.start_index();
}

CallForwardVarargsParameters const& CallForwardVarargsParametersOf(
    Operator const* op) {
  DCHECK_EQ(IrOpcode::kJSCallForwardVarargs, op->opcode());
  return OpParameter<CallForwardVarargsParameters>(op);
}


bool operator==(CallRuntimeParameters const& lhs,
                CallRuntimeParameters const& rhs) {
  return lhs.id() == rhs.id() && lhs.arity() == rhs.arity();
}


bool operator!=(CallRuntimeParameters const& lhs,
                CallRuntimeParameters const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(CallRuntimeParameters const& p) {
  return base::hash_combine(p.id(), p.arity());
}


std::ostream& operator<<(std::ostream& os, CallRuntimeParameters const& p) {
  return os << p.id() << ", " << p.arity();
}


const CallRuntimeParameters& CallRuntimeParametersOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSCallRuntime, op->opcode());
  return OpParameter<CallRuntimeParameters>(op);
}


ContextAccess::ContextAccess(size_t depth, size_t index, bool immutable)
    : immutable_(immutable),
      depth_(static_cast<uint16_t>(depth)),
      index_(static_cast<uint32_t>(index)) {
  DCHECK(depth <= std::numeric_limits<uint16_t>::max());
  DCHECK(index <= std::numeric_limits<uint32_t>::max());
}


bool operator==(ContextAccess const& lhs, ContextAccess const& rhs) {
  return lhs.depth() == rhs.depth() && lhs.index() == rhs.index() &&
         lhs.immutable() == rhs.immutable();
}


bool operator!=(ContextAccess const& lhs, ContextAccess const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(ContextAccess const& access) {
  return base::hash_combine(access.depth(), access.index(), access.immutable());
}


std::ostream& operator<<(std::ostream& os, ContextAccess const& access) {
  return os << access.depth() << ", " << access.index() << ", "
            << access.immutable();
}


ContextAccess const& ContextAccessOf(Operator const* op) {
  DCHECK(op->opcode() == IrOpcode::kJSLoadContext ||
         op->opcode() == IrOpcode::kJSLoadScriptContext ||
         op->opcode() == IrOpcode::kJSStoreContext ||
         op->opcode() == IrOpcode::kJSStoreScriptContext);
  return OpParameter<ContextAccess>(op);
}

bool operator==(CreateFunctionContextParameters const& lhs,
                CreateFunctionContextParameters const& rhs) {
  return lhs.scope_info_.object().location() ==
             rhs.scope_info_.object().location() &&
         lhs.slot_count() == rhs.slot_count() &&
         lhs.scope_type() == rhs.scope_type();
}

bool operator!=(CreateFunctionContextParameters const& lhs,
                CreateFunctionContextParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(CreateFunctionContextParameters const& parameters) {
  return base::hash_combine(parameters.scope_info_.object().location(),
                            parameters.slot_count(),
                            static_cast<int>(parameters.scope_type()));
}

std::ostream& operator<<(std::ostream& os,
                         CreateFunctionContextParameters const& parameters) {
  return os << parameters.slot_count() << ", " << parameters.scope_type();
}

CreateFunctionContextParameters const& CreateFunctionContextParametersOf(
    Operator const* op) {
  DCHECK_EQ(IrOpcode::kJSCreateFunctionContext, op->opcode());
  return OpParameter<CreateFunctionContextParameters>(op);
}

bool operator==(DefineNamedOwnPropertyParameters const& lhs,
                DefineNamedOwnPropertyParameters const& rhs) {
  return lhs.name_.object().location() == rhs.name_.object().location() &&
         lhs.feedback() == rhs.feedback();
}

bool operator!=(DefineNamedOwnPropertyParameters const& lhs,
                DefineNamedOwnPropertyParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(DefineNamedOwnPropertyParameters const& p) {
  return base::hash_combine(p.name_.object().location(),
                            FeedbackSource::Hash()(p.feedback()));
}

std::ostream& operator<<(std::ostream& os,
                         DefineNamedOwnPropertyParameters const& p) {
  return os << Brief(*p.name_.object());
}

DefineNamedOwnPropertyParameters const& DefineNamedOwnPropertyParametersOf(
    const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSDefineNamedOwnProperty, op->opcode());
  return OpParameter<DefineNamedOwnPropertyParameters>(op);
}

bool operator==(FeedbackParameter const& lhs, FeedbackParameter const& rhs) {
  return lhs.feedback() == rhs.feedback();
}

bool operator!=(FeedbackParameter const& lhs, FeedbackParameter const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(FeedbackParameter const& p) {
  return FeedbackSource::Hash()(p.feedback());
}

std::ostream& operator<<(std::ostream& os, FeedbackParameter const& p) {
  return os << p.feedback();
}

FeedbackParameter const& FeedbackParameterOf(const Operator* op) {
  DCHECK(JSOperator::IsUnaryWithFeedback(op->opcode()) ||
         JSOperator::IsBinaryWithFeedback(op->opcode()) ||
         op->opcode() == IrOpcode::kJSCreateEmptyLiteralArray ||
         op->opcode() == IrOpcode::kJSInstanceOf ||
         op->opcode() == IrOpcode::kJSDefineKeyedOwnPropertyInLiteral ||
         op->opcode() == IrOpcode::kJSStoreInArrayLiteral);
  return OpParameter<FeedbackParameter>(op);
}

bool operator==(NamedAccess const& lhs, NamedAccess const& rhs) {
  return lhs.name_.object().location() == rhs.name_.object().location() &&
         lhs.language_mode() == rhs.language_mode() &&
         lhs.feedback() == rhs.feedback();
}


bool operator!=(NamedAccess const& lhs, NamedAccess const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(NamedAccess const& p) {
  return base::hash_combine(p.name_.object().location(), p.language_mode(),
                            FeedbackSource::Hash()(p.feedback()));
}


std::ostream& operator<<(std::ostream& os, NamedAccess const& p) {
  return os << Brief(*p.name_.object()) << ", " << p.language_mode();
}


NamedAccess const& NamedAccessOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSLoadNamed ||
         op->opcode() == IrOpcode::kJSLoadNamedFromSuper ||
         op->opcode() == IrOpcode::kJSSetNamedProperty);
  return OpParameter<NamedAccess>(op);
}


std::ostream& operator<<(std::ostream& os, PropertyAccess const& p) {
  return os << p.language_mode() << ", " << p.feedback();
}


bool operator==(PropertyAccess const& lhs, PropertyAccess const& rhs) {
  return lhs.language_mode() == rhs.language_mode() &&
         lhs.feedback() == rhs.feedback();
}


bool operator!=(PropertyAccess const& lhs, PropertyAccess const& rhs) {
  return !(lhs == rhs);
}


PropertyAccess const& PropertyAccessOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSHasProperty ||
         op->opcode() == IrOpcode::kJSLoadProperty ||
         op->opcode() == IrOpcode::kJSSetKeyedProperty ||
         op->opcode() == IrOpcode::kJSDefineKeyedOwnProperty);
  return OpParameter<PropertyAccess>(op);
}


size_t hash_value(PropertyAccess const& p) {
  return base::hash_combine(p.language_mode(),
                            FeedbackSource::Hash()(p.feedback()));
}


bool operator==(LoadGlobalParameters const& lhs,
                LoadGlobalParameters const& rhs) {
  return lhs.name_.object().location() == rhs.name_.object().location() &&
         lhs.feedback() == rhs.feedback() &&
         lhs.typeof_mode() == rhs.typeof_mode();
}


bool operator!=(LoadGlobalParameters const& lhs,
                LoadGlobalParameters const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(LoadGlobalParameters const& p) {
  return base::hash_combine(p.name_.object().location(),
                            static_cast<int>(p.typeof_mode()));
}


std::ostream& operator<<(std::ostream& os, LoadGlobalParameters const& p) {
  return os << Brief(*p.name_.object()) << ", "
            << static_cast<int>(p.typeof_mode());
}


const LoadGlobalParameters& LoadGlobalParametersOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSLoadGlobal, op->opcode());
  return OpParameter<LoadGlobalParameters>(op);
}


bool operator==(StoreGlobalParameters const& lhs,
                StoreGlobalParameters const& rhs) {
  return lhs.language_mode() == rhs.language_mode() &&
         lhs.name_.object().location() == rhs.name_.object().location() &&
         lhs.feedback() == rhs.feedback();
}


bool operator!=(StoreGlobalParameters const& lhs,
                StoreGlobalParameters const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(StoreGlobalParameters const& p) {
  return base::hash_combine(p.language_mode(), p.name_.object().location(),
                            FeedbackSource::Hash()(p.feedback()));
}


std::ostream& operator<<(std::ostream& os, StoreGlobalParameters const& p) {
  return os << p.language_mode() << ", " << Brief(*p.name_.object());
}


const StoreGlobalParameters& StoreGlobalParametersOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSStoreGlobal, op->opcode());
  return OpParameter<StoreGlobalParameters>(op);
}


CreateArgumentsType const& CreateArgumentsTypeOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSCreateArguments, op->opcode());
  return OpParameter<CreateArgumentsType>(op);
}

bool operator==(CreateArrayParameters const& lhs,
                CreateArrayParameters const& rhs) {
  return lhs.arity() == rhs.arity() &&
         AddressOrNull(lhs.site_) == AddressOrNull(rhs.site_);
}


bool operator!=(CreateArrayParameters const& lhs,
                CreateArrayParameters const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(CreateArrayParameters const& p) {
  return base::hash_combine(p.arity(), AddressOrNull(p.site_));
}


std::ostream& operator<<(std::ostream& os, CreateArrayParameters const& p) {
  os << p.arity();
  if (p.site_.has_value()) {
    os << ", " << Brief(*p.site_->object());
  }
  return os;
}

const CreateArrayParameters& CreateArrayParametersOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSCreateArray, op->opcode());
  return OpParameter<CreateArrayParameters>(op);
}

bool operator==(CreateArrayIteratorParameters const& lhs,
                CreateArrayIteratorParameters const& rhs) {
  return lhs.kind() == rhs.kind();
}

bool operator!=(CreateArrayIteratorParameters const& lhs,
                CreateArrayIteratorParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(CreateArrayIteratorParameters const& p) {
  return static_cast<size_t>(p.kind());
}

std::ostream& operator<<(std::ostream& os,
                         CreateArrayIteratorParameters const& p) {
  return os << p.kind();
}

const CreateArrayIteratorParameters& CreateArrayIteratorParametersOf(
    const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSCreateArrayIterator, op->opcode());
  return OpParameter<CreateArrayIteratorParameters>(op);
}

bool operator==(CreateCollectionIteratorParameters const& lhs,
                CreateCollectionIteratorParameters const& rhs) {
  return lhs.collection_kind() == rhs.collection_kind() &&
         lhs.iteration_kind() == rhs.iteration_kind();
}

bool operator!=(CreateCollectionIteratorParameters const& lhs,
                CreateCollectionIteratorParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(CreateCollectionIteratorParameters const& p) {
  return base::hash_combine(static_cast<size_t>(p.collection_kind()),
                            static_cast<size_t>(p.iteration_kind()));
}

std::ostream& operator<<(std::ostream& os,
                         CreateCollectionIteratorParameters const& p) {
  return os << p.collection_kind() << ", " << p.iteration_kind();
}

const CreateCollectionIteratorParameters& CreateCollectionIteratorParametersOf(
    const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSCreateCollectionIterator, op->opcode());
  return OpParameter<CreateCollectionIteratorParameters>(op);
}

bool operator==(CreateBoundFunctionParameters const& lhs,
                CreateBoundFunctionParameters const& rhs) {
  return lhs.arity() == rhs.arity() &&
         lhs.map_.object().location() == rhs.map_.object().location();
}

bool operator!=(CreateBoundFunctionParameters const& lhs,
                CreateBoundFunctionParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(CreateBoundFunctionParameters const& p) {
  return base::hash_combine(p.arity(), p.map_.object().location());
}

std::ostream& operator<<(std::ostream& os,
                         CreateBoundFunctionParameters const& p) {
  os << p.arity();
  if (!p.map_.object().is_null()) os << ", " << Brief(*p.map_.object());
  return os;
}

const CreateBoundFunctionParameters& CreateBoundFunctionParametersOf(
    const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSCreateBoundFunction, op->opcode());
  return OpParameter<CreateBoundFunctionParameters>(op);
}

bool operator==(GetTemplateObjectParameters const& lhs,
                GetTemplateObjectParameters const& rhs) {
  return lhs.description_.object().location() ==
             rhs.description_.object().location() &&
         lhs.shared_.object().location() == rhs.shared_.object().location() &&
         lhs.feedback() == rhs.feedback();
}

bool operator!=(GetTemplateObjectParameters const& lhs,
                GetTemplateObjectParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(GetTemplateObjectParameters const& p) {
  return base::hash_combine(p.description_.object().location(),
                            p.shared_.object().location(),
                            FeedbackSource::Hash()(p.feedback()));
}

std::ostream& operator<<(std::ostream& os,
                         GetTemplateObjectParameters const& p) {
  return os << Brief(*p.description_.object()) << ", "
            << Brief(*p.shared_.object());
}

const GetTemplateObjectParameters& GetTemplateObjectParametersOf(
    const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSGetTemplateObject);
  return OpParameter<GetTemplateObjectParameters>(op);
}

bool operator==(CreateClosureParameters const& lhs,
                CreateClosureParameters const& rhs) {
  return lhs.allocation() == rhs.allocation() &&
         lhs.code_.object().location() == rhs.code_.object().location() &&
         lhs.shared_info_.object().location() ==
             rhs.shared_info_.object().location();
}


bool operator!=(CreateClosureParameters const& lhs,
                CreateClosureParameters const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(CreateClosureParameters const& p) {
  return base::hash_combine(p.allocation(), p.code_.object().location(),
                            p.shared_info_.object().location());
}


std::ostream& operator<<(std::ostream& os, CreateClosureParameters const& p) {
  return os << p.allocation() << ", " << Brief(*p.shared_info_.object()) << ", "
            << Brief(*p.code_.object());
}


const CreateClosureParameters& CreateClosureParametersOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSCreateClosure, op->opcode());
  return OpParameter<CreateClosureParameters>(op);
}


bool operator==(CreateLiteralParameters const& lhs,
                CreateLiteralParameters const& rhs) {
  return lhs.constant_.object().location() ==
             rhs.constant_.object().location() &&
         lhs.feedback() == rhs.feedback() && lhs.length() == rhs.length() &&
         lhs.flags() == rhs.flags();
}


bool operator!=(CreateLiteralParameters const& lhs,
                CreateLiteralParameters const& rhs) {
  return !(lhs == rhs);
}


size_t hash_value(CreateLiteralParameters const& p) {
  return base::hash_combine(p.constant_.object().location(),
                            FeedbackSource::Hash()(p.feedback()), p.length(),
                            p.flags());
}


std::ostream& operator<<(std::ostream& os, CreateLiteralParameters const& p) {
  return os << Brief(*p.constant_.object()) << ", " << p.length() << ", "
            << p.flags();
}


const CreateLiteralParameters& CreateLiteralParametersOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSCreateLiteralArray ||
         op->opcode() == IrOpcode::kJSCreateLiteralObject ||
         op->opcode() == IrOpcode::kJSCreateLiteralRegExp);
  return OpParameter<CreateLiteralParameters>(op);
}

bool operator==(CloneObjectParameters const& lhs,
                CloneObjectParameters const& rhs) {
  return lhs.feedback() == rhs.feedback() && lhs.flags() == rhs.flags();
}

bool operator!=(CloneObjectParameters const& lhs,
                CloneObjectParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(CloneObjectParameters const& p) {
  return base::hash_combine(FeedbackSource::Hash()(p.feedback()), p.flags());
}

std::ostream& operator<<(std::ostream& os, CloneObjectParameters const& p) {
  return os << p.flags();
}

const CloneObjectParameters& CloneObjectParametersOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSCloneObject);
  return OpParameter<CloneObjectParameters>(op);
}

std::ostream& operator<<(std::ostream& os, GetIteratorParameters const& p) {
  return os << p.loadFeedback() << ", " << p.callFeedback();
}

bool operator==(GetIteratorParameters const& lhs,
                GetIteratorParameters const& rhs) {
  return lhs.loadFeedback() == rhs.loadFeedback() &&
         lhs.callFeedback() == rhs.callFeedback();
}

bool operator!=(GetIteratorParameters const& lhs,
                GetIteratorParameters const& rhs) {
  return !(lhs == rhs);
}

GetIteratorParameters const& GetIteratorParametersOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSGetIterator);
  return OpParameter<GetIteratorParameters>(op);
}

size_t hash_value(GetIteratorParameters const& p) {
  return base::hash_combine(FeedbackSource::Hash()(p.loadFeedback()),
                            FeedbackSource::Hash()(p.callFeedback()));
}

size_t hash_value(ForInMode const& mode) { return static_cast<uint8_t>(mode); }

std::ostream& operator<<(std::ostream& os, ForInMode const& mode) {
  switch (mode) {
    case ForInMode::kUseEnumCacheKeysAndIndices:
      return os << "UseEnumCacheKeysAndIndices";
    case ForInMode::kUseEnumCacheKeys:
      return os << "UseEnumCacheKeys";
    case ForInMode::kGeneric:
      return os << "Generic";
  }
  UNREACHABLE();
}

bool operator==(ForInParameters const& lhs, ForInParameters const& rhs) {
  return lhs.feedback() == rhs.feedback() && lhs.mode() == rhs.mode();
}

bool operator!=(ForInParameters const& lhs, ForInParameters const& rhs) {
  return !(lhs == rhs);
}

size_t hash_value(ForInParameters const& p) {
  return base::hash_combine(FeedbackSource::Hash()(p.feedback()), p.mode());
}

std::ostream& operator<<(std::ostream& os, ForInParameters const& p) {
  return os << p.feedback() << ", " << p.mode();
}

ForInParameters const& ForInParametersOf(const Operator* op) {
  DCHECK(op->opcode() == IrOpcode::kJSForInNext ||
         op->opcode() == IrOpcode::kJSForInPrepare);
  return OpParameter<ForInParameters>(op);
}

#if V8_ENABLE_WEBASSEMBLY
JSWasmCallParameters const& JSWasmCallParametersOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSWasmCall, op->opcode());
  return OpParameter<JSWasmCallParameters>(op);
}

std::ostream& operator<<(std::ostream& os, JSWasmCallParameters const& p) {
  return os << p.module() << ", " << p.signature() << ", " << p.feedback();
}

size_t hash_value(JSWasmCallParameters const& p) {
  return base::hash_combine(p.module(), p.signature(),
                            FeedbackSource::Hash()(p.feedback()));
}

bool operator==(JSWasmCallParameters const& lhs,
                JSWasmCallParameters const& rhs) {
  return lhs.module() == rhs.module() && lhs.signature() == rhs.signature() &&
         lhs.feedback() == rhs.feedback();
}

int JSWasmCallParameters::arity_without_implicit_args() const {
  return static_cast<int>(signature_->parameter_count());
}

int JSWasmCallParameters::input_count() const {
  return static_cast<int>(signature_->parameter_count()) +
         JSWasmCallNode::kExtraInputCount;
}

// static
Type JSWasmCallNode::TypeForWasmReturnType(wasm::CanonicalValueType type) {
  switch (type.kind()) {
    case wasm::kI32:
      return Type::Signed32();
    case wasm::kI64:
      return Type::SignedBigInt64();
    case wasm::kF32:
    case wasm::kF64:
      return Type::Number();
    case wasm::kRef:
    case wasm::kRefNull:
      CHECK(type.is_reference_to(wasm::HeapType::kExtern));
      return Type::Any();
    default:
      UNREACHABLE();
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

#define CACHED_OP_LIST(V)                                                \
  V(ToLength, Operator::kNoProperties, 1, 1)                             \
  V(ToName, Operator::kNoProperties, 1, 1)                               \
  V(ToNumber, Operator::kNoProperties, 1, 1)                             \
  V(ToNumberConvertBigInt, Operator::kNoProperties, 1, 1)                \
  V(ToBigInt, Operator::kNoProperties, 1, 1)                             \
  V(ToBigIntConvertNumber, Operator::kNoProperties, 1, 1)                \
  V(ToNumeric, Operator::kNoProperties, 1, 1)                            \
  V(ToObject, Operator::kFoldable, 1, 1)                                 \
  V(ToString, Operator::kNoProperties, 1, 1)                             \
  V(Create, Operator::kNoProperties, 2, 1)                               \
  V(CreateIterResultObject, Operator::kEliminatable, 2, 1)               \
  V(CreateStringIterator, Operator::kEliminatable, 1, 1)                 \
  V(CreateKeyValueArray, Operator::kEliminatable, 2, 1)                  \
  V(CreatePromise, Operator::kEliminatable, 0, 1)                        \
  V(CreateTypedArray, Operator::kNoProperties, 5, 1)                     \
  V(CreateObject, Operator::kNoProperties, 1, 1)                         \
  V(CreateStringWrapper, Operator::kEliminatable, 1, 1)                  \
  V(ObjectIsArray, Operator::kNoProperties, 1, 1)                        \
  V(HasInPrototypeChain, Operator::kNoProperties, 2, 1)                  \
  V(OrdinaryHasInstance, Operator::kNoProperties, 2, 1)                  \
  V(ForInEnumerate, Operator::kNoProperties, 1, 1)                       \
  V(AsyncFunctionEnter, Operator::kNoProperties, 2, 1)                   \
  V(AsyncFunctionReject, Operator::kNoDeopt | Operator::kNoThrow, 2, 1)  \
  V(AsyncFunctionResolve, Operator::kNoDeopt | Operator::kNoThrow, 2, 1) \
  V(LoadMessage, Operator::kNoThrow | Operator::kNoWrite, 0, 1)          \
  V(StoreMessage, Operator::kNoRead | Operator::kNoThrow, 1, 0)          \
  V(GeneratorRestoreContinuation, Operator::kNoThrow, 1, 1)              \
  V(GeneratorRestoreContext, Operator::kNoThrow, 1, 1)                   \
  V(GeneratorRestoreInputOrDebugPos, Operator::kNoThrow, 1, 1)           \
  V(Debugger, Operator::kNoProperties, 0, 0)                             \
  V(FulfillPromise, Operator::kNoDeopt | Operator::kNoThrow, 2, 1)       \
  V(PerformPromiseThen, Operator::kNoDeopt | Operator::kNoThrow, 4, 1)   \
  V(PromiseResolve, Operator::kNoProperties, 2, 1)                       \
  V(RejectPromise, Operator::kNoDeopt | Operator::kNoThrow, 3, 1)        \
  V(ResolvePromise, Operator::kNoDeopt | Operator::kNoThrow, 2, 1)       \
  V(GetSuperConstructor, Operator::kNoWrite | Operator::kNoThrow, 1, 1)  \
  V(FindNonDefaultConstructorOrConstruct, Operator::kNoProperties, 2, 2) \
  V(ParseInt, Operator::kNoProperties, 2, 1)                             \
  V(RegExpTest, Operator::kNoProperties, 2, 1)

struct JSOperatorGlobalCache final {
#define CACHED_OP(Name, properties, value_input_count, value_output_count) \
  struct Name##Operator final : public Operator {                          \
    Name##Operator()                                                       \
        : Operator(IrOpcode::kJS##Name, properties, "JS" #Name,            \
                   value_input_count, Operator::ZeroIfPure(properties),    \
                   Operator::ZeroIfEliminatable(properties),               \
                   value_output_count, Operator::ZeroIfPure(properties),   \
                   Operator::ZeroIfNoThrow(properties)) {}                 \
  };                                                                       \
  Name##Operator k##Name##Operator;
  CACHED_OP_LIST(CACHED_OP)
#undef CACHED_OP
};

namespace {
DEFINE_LAZY_LEAKY_OBJECT_GETTER(JSOperatorGlobalCache, GetJSOperatorGlobalCache)
}  // namespace

JSOperatorBuilder::JSOperatorBuilder(Zone* zone)
    : cache_(*GetJSOperatorGlobalCache()), zone_(zone) {}

#define CACHED_OP(Name, properties, value_input_count, value_output_count) \
  const Operator* JSOperatorBuilder::Name() {                              \
    return &cache_.k##Name##Operator;                                      \
  }
CACHED_OP_LIST(CACHED_OP)
#undef CACHED_OP

#define UNARY_OP(JSName, Name)                                                \
  const Operator* JSOperatorBuilder::Name(FeedbackSource const& feedback) {   \
    FeedbackParameter parameters(feedback);                                   \
    return zone()->New<Operator1<FeedbackParameter>>(                         \
        IrOpcode::k##JSName, Operator::kNoProperties, #JSName, 2, 1, 1, 1, 1, \
        2, parameters);                                                       \
  }
JS_UNOP_WITH_FEEDBACK(UNARY_OP)
#undef UNARY_OP

#define BINARY_OP(JSName, Name)                                               \
  const Operator* JSOperatorBuilder::Name(FeedbackSource const& feedback) {   \
    static constexpr auto kProperties = BinopProperties(IrOpcode::k##JSName); \
    FeedbackParameter parameters(feedback);                                   \
    return zone()->New<Operator1<FeedbackParameter>>(                         \
        IrOpcode::k##JSName, kProperties, #JSName, 3, 1, 1, 1, 1,             \
        Operator::ZeroIfNoThrow(kProperties), parameters);                    \
  }
JS_BINOP_WITH_FEEDBACK(BINARY_OP)
#undef BINARY_OP

const Operator* JSOperatorBuilder::DefineKeyedOwnPropertyInLiteral(
    const FeedbackSource& feedback) {
  static constexpr int kObject = 1;
  static constexpr int kName = 1;
  static constexpr int kValue = 1;
  static constexpr int kFlags = 1;
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity =
      kObject + kName + kValue + kFlags + kFeedbackVector;
  FeedbackParameter parameters(feedback);
  return zone()->New<Operator1<FeedbackParameter>>(  // --
      IrOpcode::kJSDefineKeyedOwnPropertyInLiteral,
      Operator::kNoThrow,                   // opcode
      "JSDefineKeyedOwnPropertyInLiteral",  // name
      kArity, 1, 1, 0, 1, 1,                // counts
      parameters);                          // parameter
}

const Operator* JSOperatorBuilder::StoreInArrayLiteral(
    const FeedbackSource& feedback) {
  static constexpr int kArray = 1;
  static constexpr int kIndex = 1;
  static constexpr int kValue = 1;
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kArray + kIndex + kValue + kFeedbackVector;
  FeedbackParameter parameters(feedback);
  return zone()->New<Operator1<FeedbackParameter>>(  // --
      IrOpcode::kJSStoreInArrayLiteral,
      Operator::kNoThrow,       // opcode
      "JSStoreInArrayLiteral",  // name
      kArity, 1, 1, 0, 1, 1,    // counts
      parameters);              // parameter
}

const Operator* JSOperatorBuilder::CallForwardVarargs(size_t arity,
                                                      uint32_t start_index) {
  CallForwardVarargsParameters parameters(arity, start_index);
  return zone()->New<Operator1<CallForwardVarargsParameters>>(   // --
      IrOpcode::kJSCallForwardVarargs, Operator::kNoProperties,  // opcode
      "JSCallForwardVarargs",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,                         // counts
      parameters);                                               // parameter
}

const Operator* JSOperatorBuilder::Call(
    size_t arity, CallFrequency const& frequency,
    FeedbackSource const& feedback, ConvertReceiverMode convert_mode,
    SpeculationMode speculation_mode, CallFeedbackRelation feedback_relation) {
  CallParameters parameters(arity, frequency, feedback, convert_mode,
                            speculation_mode, feedback_relation);
  return zone()->New<Operator1<CallParameters>>(   // --
      IrOpcode::kJSCall, Operator::kNoProperties,  // opcode
      "JSCall",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,           // inputs/outputs
      parameters);                                 // parameter
}

const Operator* JSOperatorBuilder::CallWithArrayLike(
    const CallFrequency& frequency, const FeedbackSource& feedback,
    SpeculationMode speculation_mode, CallFeedbackRelation feedback_relation) {
  static constexpr int kTheArrayLikeObject = 1;
  CallParameters parameters(
      JSCallWithArrayLikeNode::ArityForArgc(kTheArrayLikeObject), frequency,
      feedback, ConvertReceiverMode::kAny, speculation_mode, feedback_relation);
  return zone()->New<Operator1<CallParameters>>(                // --
      IrOpcode::kJSCallWithArrayLike, Operator::kNoProperties,  // opcode
      "JSCallWithArrayLike",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,                        // counts
      parameters);                                              // parameter
}

const Operator* JSOperatorBuilder::CallWithSpread(
    uint32_t arity, CallFrequency const& frequency,
    FeedbackSource const& feedback, SpeculationMode speculation_mode,
    CallFeedbackRelation feedback_relation) {
  DCHECK_IMPLIES(speculation_mode == SpeculationMode::kAllowSpeculation,
                 feedback.IsValid());
  CallParameters parameters(arity, frequency, feedback,
                            ConvertReceiverMode::kAny, speculation_mode,
                            feedback_relation);
  return zone()->New<Operator1<CallParameters>>(             // --
      IrOpcode::kJSCallWithSpread, Operator::kNoProperties,  // opcode
      "JSCallWithSpread",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,                     // counts
      parameters);                                           // parameter
}

const Operator* JSOperatorBuilder::CallRuntime(Runtime::FunctionId id) {
  const Runtime::Function* f = Runtime::FunctionForId(id);
  return CallRuntime(f, f->nargs);
}

const Operator* JSOperatorBuilder::CallRuntime(
    Runtime::FunctionId id, size_t arity, Operator::Properties properties) {
  const Runtime::Function* f = Runtime::FunctionForId(id);
  return CallRuntime(f, arity, properties);
}

const Operator* JSOperatorBuilder::CallRuntime(
    const Runtime::Function* f, size_t arity, Operator::Properties properties) {
  CallRuntimeParameters parameters(f->function_id, arity);
  DCHECK(f->nargs == -1 || f->nargs == static_cast<int>(parameters.arity()));
  return zone()->New<Operator1<CallRuntimeParameters>>(  // --
      IrOpcode::kJSCallRuntime, properties,              // opcode
      "JSCallRuntime",                                   // name
      parameters.arity(), 1, 1, f->result_size, 1, 2,    // inputs/outputs
      parameters);                                       // parameter
}

#if V8_ENABLE_WEBASSEMBLY
const Operator* JSOperatorBuilder::CallWasm(
    const wasm::WasmModule* wasm_module,
    const wasm::CanonicalSig* wasm_signature, int wasm_function_index,
    SharedFunctionInfoRef shared_fct_info, wasm::NativeModule* native_module,
    FeedbackSource const& feedback) {
  // TODO(clemensb): Drop wasm_module.
  DCHECK_EQ(wasm_module, native_module->module());
  JSWasmCallParameters parameters(wasm_module, wasm_signature,
                                  wasm_function_index, shared_fct_info,
                                  native_module, feedback);
  return zone()->New<Operator1<JSWasmCallParameters>>(
      IrOpcode::kJSWasmCall, Operator::kNoProperties,  // opcode
      "JSWasmCall",                                    // name
      parameters.input_count(), 1, 1, 1, 1, 2,         // inputs/outputs
      parameters);                                     // parameter
}
#endif  // V8_ENABLE_WEBASSEMBLY

const Operator* JSOperatorBuilder::ConstructForwardVarargs(
    size_t arity, uint32_t start_index) {
  ConstructForwardVarargsParameters parameters(arity, start_index);
  return zone()->New<Operator1<ConstructForwardVarargsParameters>>(   // --
      IrOpcode::kJSConstructForwardVarargs, Operator::kNoProperties,  // opcode
      "JSConstructForwardVarargs",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,                              // counts
      parameters);  // parameter
}

// Note: frequency is taken by reference to work around a GCC bug
// on AIX (v8:8193).
const Operator* JSOperatorBuilder::Construct(uint32_t arity,
                                             CallFrequency const& frequency,
                                             FeedbackSource const& feedback) {
  ConstructParameters parameters(arity, frequency, feedback);
  return zone()->New<Operator1<ConstructParameters>>(   // --
      IrOpcode::kJSConstruct, Operator::kNoProperties,  // opcode
      "JSConstruct",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,                // counts
      parameters);                                      // parameter
}

const Operator* JSOperatorBuilder::ConstructWithArrayLike(
    CallFrequency const& frequency, FeedbackSource const& feedback) {
  static constexpr int kTheArrayLikeObject = 1;
  ConstructParameters parameters(
      JSConstructWithArrayLikeNode::ArityForArgc(kTheArrayLikeObject),
      frequency, feedback);
  return zone()->New<Operator1<ConstructParameters>>(  // --
      IrOpcode::kJSConstructWithArrayLike,             // opcode
      Operator::kNoProperties,                         // properties
      "JSConstructWithArrayLike",                      // name
      parameters.arity(), 1, 1, 1, 1, 2,               // counts
      parameters);                                     // parameter
}

const Operator* JSOperatorBuilder::ConstructWithSpread(
    uint32_t arity, CallFrequency const& frequency,
    FeedbackSource const& feedback) {
  ConstructParameters parameters(arity, frequency, feedback);
  return zone()->New<Operator1<ConstructParameters>>(             // --
      IrOpcode::kJSConstructWithSpread, Operator::kNoProperties,  // opcode
      "JSConstructWithSpread",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,                          // counts
      parameters);                                                // parameter
}

const Operator* JSOperatorBuilder::ConstructForwardAllArgs(
    CallFrequency const& frequency, FeedbackSource const& feedback) {
  // Use 0 as a fake arity. This operator will be reduced away to either a call
  // to Builtin::kConstructForwardAllArgs or an ordinary
  // JSConstruct.
  ConstructParameters parameters(JSConstructForwardAllArgsNode::ArityForArgc(0),
                                 frequency, feedback);
  return zone()->New<Operator1<ConstructParameters>>(                 // --
      IrOpcode::kJSConstructForwardAllArgs, Operator::kNoProperties,  // opcode
      "JSConstructForwardAllArgs",                                    // name
      parameters.arity(), 1, 1, 1, 1, 2,                              // counts
      parameters);  // parameter
}

const Operator* JSOperatorBuilder::LoadNamed(NameRef name,
                                             const FeedbackSource& feedback) {
  static constexpr int kObject = 1;
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kObject + kFeedbackVector;
  NamedAccess access(LanguageMode::kSloppy, name, feedback);
  return zone()->New<Operator1<NamedAccess>>(           // --
      IrOpcode::kJSLoadNamed, Operator::kNoProperties,  // opcode
      "JSLoadNamed",                                    // name
      kArity, 1, 1, 1, 1, 2,                            // counts
      access);                                          // parameter
}

const Operator* JSOperatorBuilder::LoadNamedFromSuper(
    NameRef name, const FeedbackSource& feedback) {
  static constexpr int kReceiver = 1;
  static constexpr int kHomeObject = 1;
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kReceiver + kHomeObject + kFeedbackVector;
  NamedAccess access(LanguageMode::kSloppy, name, feedback);
  return zone()->New<Operator1<NamedAccess>>(                    // --
      IrOpcode::kJSLoadNamedFromSuper, Operator::kNoProperties,  // opcode
      "JSLoadNamedFromSuper",                                    // name
      kArity, 1, 1, 1, 1, 2,                                     // counts
      access);                                                   // parameter
}

const Operator* JSOperatorBuilder::LoadProperty(
    FeedbackSource const& feedback) {
  PropertyAccess access(LanguageMode::kSloppy, feedback);
  return zone()->New<Operator1<PropertyAccess>>(           // --
      IrOpcode::kJSLoadProperty, Operator::kNoProperties,  // opcode
      "JSLoadProperty",                                    // name
      3, 1, 1, 1, 1, 2,                                    // counts
      access);                                             // parameter
}

const Operator* JSOperatorBuilder::GetIterator(
    FeedbackSource const& load_feedback, FeedbackSource const& call_feedback) {
  GetIteratorParameters access(load_feedback, call_feedback);
  return zone()->New<Operator1<GetIteratorParameters>>(   // --
      IrOpcode::kJSGetIterator, Operator::kNoProperties,  // opcode
      "JSGetIterator",                                    // name
      2, 1, 1, 1, 1, 2,                                   // counts
      access);                                            // parameter
}

const Operator* JSOperatorBuilder::HasProperty(FeedbackSource const& feedback) {
  PropertyAccess access(LanguageMode::kSloppy, feedback);
  return zone()->New<Operator1<PropertyAccess>>(          // --
      IrOpcode::kJSHasProperty, Operator::kNoProperties,  // opcode
      "JSHasProperty",                                    // name
      3, 1, 1, 1, 1, 2,                                   // counts
      access);                                            // parameter
}

const Operator* JSOperatorBuilder::ForInNext(ForInMode mode,
                                             const FeedbackSource& feedback) {
  return zone()->New<Operator1<ForInParameters>>(       // --
      IrOpcode::kJSForInNext, Operator::kNoProperties,  // opcode
      "JSForInNext",                                    // name
      5, 1, 1, 1, 1, 2,                                 // counts
      ForInParameters{feedback, mode});                 // parameter
}

const Operator* JSOperatorBuilder::ForInPrepare(
    ForInMode mode, const FeedbackSource& feedback) {
  return zone()->New<Operator1<ForInParameters>>(  // --
      IrOpcode::kJSForInPrepare,                   // opcode
      Operator::kNoWrite | Operator::kNoThrow,     // flags
      "JSForInPrepare",                            // name
      2, 1, 1, 3, 1, 1,                            // counts
      ForInParameters{feedback, mode});            // parameter
}

const Operator* JSOperatorBuilder::GeneratorStore(int register_count) {
  return zone()->New<Operator1<int>>(                   // --
      IrOpcode::kJSGeneratorStore, Operator::kNoThrow,  // opcode
      "JSGeneratorStore",                               // name
      3 + register_count, 1, 1, 0, 1, 0,                // counts
      register_count);                                  // parameter
}

int RegisterCountOf(Operator const* op) {
  DCHECK_EQ(IrOpcode::kJSCreateAsyncFunctionObject, op->opcode());
  return OpParameter<int>(op);
}

int GeneratorStoreValueCountOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSGeneratorStore, op->opcode());
  return OpParameter<int>(op);
}

const Operator* JSOperatorBuilder::GeneratorRestoreRegister(int index) {
  return zone()->New<Operator1<int>>(                             // --
      IrOpcode::kJSGeneratorRestoreRegister, Operator::kNoThrow,  // opcode
      "JSGeneratorRestoreRegister",                               // name
      1, 1, 1, 1, 1, 0,                                           // counts
      index);                                                     // parameter
}

int RestoreRegisterIndexOf(const Operator* op) {
  DCHECK_EQ(IrOpcode::kJSGeneratorRestoreRegister, op->opcode());
  return OpParameter<int>(op);
}

const Operator* JSOperatorBuilder::SetNamedProperty(
    LanguageMode language_mode, NameRef name, FeedbackSource const& feedback) {
  static constexpr int kObject = 1;
  static constexpr int kValue = 1;
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kObject + kValue + kFeedbackVector;
  NamedAccess access(language_mode, name, feedback);
  return zone()->New<Operator1<NamedAccess>>(                  // --
      IrOpcode::kJSSetNamedProperty, Operator::kNoProperties,  // opcode
      "JSSetNamedProperty",                                    // name
      kArity, 1, 1, 0, 1, 2,                                   // counts
      access);                                                 // parameter
}

const Operator* JSOperatorBuilder::SetKeyedProperty(
    LanguageMode language_mode, FeedbackSource const& feedback) {
  PropertyAccess access(language_mode, feedback);
  return zone()->New<Operator1<PropertyAccess>>(               // --
      IrOpcode::kJSSetKeyedProperty, Operator::kNoProperties,  // opcode
      "JSSetKeyedProperty",                                    // name
      4, 1, 1, 0, 1, 2,                                        // counts
      access);                                                 // parameter
}

const Operator* JSOperatorBuilder::DefineKeyedOwnProperty(
    LanguageMode language_mode, FeedbackSource const& feedback) {
  PropertyAccess access(language_mode, feedback);
  return zone()->New<Operator1<PropertyAccess>>(                     // --
      IrOpcode::kJSDefineKeyedOwnProperty, Operator::kNoProperties,  // opcode
      "JSDefineKeyedOwnProperty",                                    // name
      5, 1, 1, 0, 1, 2,                                              // counts
      access);  // parameter
}

const Operator* JSOperatorBuilder::DefineNamedOwnProperty(
    NameRef name, FeedbackSource const& feedback) {
  static constexpr int kObject = 1;
  static constexpr int kValue = 1;
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kObject + kValue + kFeedbackVector;
  DefineNamedOwnPropertyParameters parameters(name, feedback);
  return zone()->New<Operator1<DefineNamedOwnPropertyParameters>>(   // --
      IrOpcode::kJSDefineNamedOwnProperty, Operator::kNoProperties,  // opcode
      "JSDefineNamedOwnProperty",                                    // name
      kArity, 1, 1, 0, 1, 2,                                         // counts
      parameters);  // parameter
}

const Operator* JSOperatorBuilder::DeleteProperty() {
  return zone()->New<Operator>(                              // --
      IrOpcode::kJSDeleteProperty, Operator::kNoProperties,  // opcode
      "JSDeleteProperty",                                    // name
      3, 1, 1, 1, 1, 2);                                     // counts
}

const Operator* JSOperatorBuilder::CreateGeneratorObject() {
  return zone()->New<Operator>(                                     // --
      IrOpcode::kJSCreateGeneratorObject, Operator::kEliminatable,  // opcode
      "JSCreateGeneratorObject",                                    // name
      2, 1, 1, 1, 1, 0);                                            // counts
}

const Operator* JSOperatorBuilder::LoadGlobal(NameRef name,
                                              const FeedbackSource& feedback,
                                              TypeofMode typeof_mode) {
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kFeedbackVector;
  LoadGlobalParameters parameters(name, feedback, typeof_mode);
  return zone()->New<Operator1<LoadGlobalParameters>>(   // --
      IrOpcode::kJSLoadGlobal, Operator::kNoProperties,  // opcode
      "JSLoadGlobal",                                    // name
      kArity, 1, 1, 1, 1, 2,                             // counts
      parameters);                                       // parameter
}

const Operator* JSOperatorBuilder::StoreGlobal(LanguageMode language_mode,
                                               NameRef name,
                                               const FeedbackSource& feedback) {
  static constexpr int kValue = 1;
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kValue + kFeedbackVector;
  StoreGlobalParameters parameters(language_mode, feedback, name);
  return zone()->New<Operator1<StoreGlobalParameters>>(   // --
      IrOpcode::kJSStoreGlobal, Operator::kNoProperties,  // opcode
      "JSStoreGlobal",                                    // name
      kArity, 1, 1, 0, 1, 2,                              // counts
      parameters);                                        // parameter
}

const Operator* JSOperatorBuilder::HasContextExtension(size_t depth) {
  return zone()->New<Operator1<size_t>>(        // --
      IrOpcode::kJSHasContextExtension,         // opcode
      Operator::kNoWrite | Operator::kNoThrow,  // flags
      "JSHasContextExtension",                  // name
      0, 1, 0, 1, 1, 0,                         // counts
      depth);                                   // parameter
}

const Operator* JSOperatorBuilder::LoadContext(size_t depth, size_t index,
                                               bool immutable) {
  ContextAccess access(depth, index, immutable);
  return zone()->New<Operator1<ContextAccess>>(  // --
      IrOpcode::kJSLoadContext,                  // opcode
      Operator::kNoWrite | Operator::kNoThrow,   // flags
      "JSLoadContext",                           // name
      0, 1, 0, 1, 1, 0,                          // counts
      access);                                   // parameter
}

const Operator* JSOperatorBuilder::LoadScriptContext(size_t depth,
                                                     size_t index) {
  ContextAccess access(depth, index, false);
  return zone()->New<Operator1<ContextAccess>>(  // --
      IrOpcode::kJSLoadScriptContext,            // opcode
      Operator::kNoWrite | Operator::kNoThrow,   // flags
      "JSLoadScriptContext",                     // name
      0, 1, 1, 1, 1, 1,                          // counts
      access);                                   // parameter
}

const Operator* JSOperatorBuilder::StoreContext(size_t depth, size_t index) {
  ContextAccess access(depth, index, false);
  return zone()->New<Operator1<ContextAccess>>(  // --
      IrOpcode::kJSStoreContext,                 // opcode
      Operator::kNoRead | Operator::kNoThrow,    // flags
      "JSStoreContext",                          // name
      1, 1, 1, 0, 1, 0,                          // counts
      access);                                   // parameter
}

const Operator* JSOperatorBuilder::StoreScriptContext(size_t depth,
                                                      size_t index) {
  ContextAccess access(depth, index, false);
  return zone()->New<Operator1<ContextAccess>>(  // --
      IrOpcode::kJSStoreScriptContext,           // opcode
      Operator::kNoRead | Operator::kNoThrow,    // flags
      "JSStoreScriptContext",                    // name
      1, 1, 1, 0, 1, 1,                          // counts
      access);                                   // parameter
}

const Operator* JSOperatorBuilder::LoadModule(int32_t cell_index) {
  return zone()->New<Operator1<int32_t>>(       // --
      IrOpcode::kJSLoadModule,                  // opcode
      Operator::kNoWrite | Operator::kNoThrow,  // flags
      "JSLoadModule",                           // name
      1, 1, 1, 1, 1, 0,                         // counts
      cell_index);                              // parameter
}

const Operator* JSOperatorBuilder::GetImportMeta() {
  return zone()->New<Operator>(    // --
      IrOpcode::kJSGetImportMeta,  // opcode
      Operator::kNoProperties,     // flags
      "JSGetImportMeta",           // name
      0, 1, 1, 1, 1, 2);           // counts
}

const Operator* JSOperatorBuilder::StoreModule(int32_t cell_index) {
  return zone()->New<Operator1<int32_t>>(      // --
      IrOpcode::kJSStoreModule,                // opcode
      Operator::kNoRead | Operator::kNoThrow,  // flags
      "JSStoreModule",                         // name
      2, 1, 1, 0, 1, 0,                        // counts
      cell_index);                             // parameter
}

const Operator* JSOperatorBuilder::CreateArguments(CreateArgumentsType type) {
  return zone()->New<Operator1<CreateArgumentsType>>(         // --
      IrOpcode::kJSCreateArguments, Operator::kEliminatable,  // opcode
      "JSCreateArguments",                                    // name
      1, 1, 0, 1, 1, 0,                                       // counts
      type);                                                  // parameter
}

const Operator* JSOperatorBuilder::CreateArray(size_t arity,
                                               OptionalAllocationSiteRef site) {
  // constructor, new_target, arg1, ..., argN
  int const value_input_count = static_cast<int>(arity) + 2;
  CreateArrayParameters parameters(arity, site);
  return zone()->New<Operator1<CreateArrayParameters>>(   // --
      IrOpcode::kJSCreateArray, Operator::kNoProperties,  // opcode
      "JSCreateArray",                                    // name
      value_input_count, 1, 1, 1, 1, 2,                   // counts
      parameters);                                        // parameter
}

const Operator* JSOperatorBuilder::CreateArrayIterator(IterationKind kind) {
  CreateArrayIteratorParameters parameters(kind);
  return zone()->New<Operator1<CreateArrayIteratorParameters>>(   // --
      IrOpcode::kJSCreateArrayIterator, Operator::kEliminatable,  // opcode
      "JSCreateArrayIterator",                                    // name
      1, 1, 1, 1, 1, 0,                                           // counts
      parameters);                                                // parameter
}

const Operator* JSOperatorBuilder::CreateAsyncFunctionObject(
    int register_count) {
  return zone()->New<Operator1<int>>(          // --
      IrOpcode::kJSCreateAsyncFunctionObject,  // opcode
      Operator::kEliminatable,                 // flags
      "JSCreateAsyncFunctionObject",           // name
      3, 1, 1, 1, 1, 0,                        // counts
      register_count);                         // parameter
}

const Operator* JSOperatorBuilder::CreateCollectionIterator(
    CollectionKind collection_kind, IterationKind iteration_kind) {
  CreateCollectionIteratorParameters parameters(collection_kind,
                                                iteration_kind);
  return zone()->New<Operator1<CreateCollectionIteratorParameters>>(
      IrOpcode::kJSCreateCollectionIterator, Operator::kEliminatable,
      "JSCreateCollectionIterator", 1, 1, 1, 1, 1, 0, parameters);
}

const Operator* JSOperatorBuilder::CreateBoundFunction(size_t arity,
                                                       MapRef map) {
  // bound_target_function, bound_this, arg1, ..., argN
  int const value_input_count = static_cast<int>(arity) + 2;
  CreateBoundFunctionParameters parameters(arity, map);
  return zone()->New<Operator1<CreateBoundFunctionParameters>>(   // --
      IrOpcode::kJSCreateBoundFunction, Operator::kEliminatable,  // opcode
      "JSCreateBoundFunction",                                    // name
      value_input_count, 1, 1, 1, 1, 0,                           // counts
      parameters);                                                // parameter
}

const Operator* JSOperatorBuilder::CreateClosure(
    SharedFunctionInfoRef shared_info, CodeRef code,
    AllocationType allocation) {
  static constexpr int kFeedbackCell = 1;
  static constexpr int kArity = kFeedbackCell;
  CreateClosureParameters parameters(shared_info, code, allocation);
  return zone()->New<Operator1<CreateClosureParameters>>(   // --
      IrOpcode::kJSCreateClosure, Operator::kEliminatable,  // opcode
      "JSCreateClosure",                                    // name
      kArity, 1, 1, 1, 1, 0,                                // counts
      parameters);                                          // parameter
}

const Operator* JSOperatorBuilder::CreateLiteralArray(
    ArrayBoilerplateDescriptionRef description, FeedbackSource const& feedback,
    int literal_flags, int number_of_elements) {
  CreateLiteralParameters parameters(description, feedback, number_of_elements,
                                     literal_flags);
  return zone()->New<Operator1<CreateLiteralParameters>>(  // --
      IrOpcode::kJSCreateLiteralArray,                     // opcode
      Operator::kNoProperties,                             // properties
      "JSCreateLiteralArray",                              // name
      1, 1, 1, 1, 1, 2,                                    // counts
      parameters);                                         // parameter
}

const Operator* JSOperatorBuilder::CreateEmptyLiteralArray(
    FeedbackSource const& feedback) {
  static constexpr int kFeedbackVector = 1;
  static constexpr int kArity = kFeedbackVector;
  FeedbackParameter parameters(feedback);
  return zone()->New<Operator1<FeedbackParameter>>(  // --
      IrOpcode::kJSCreateEmptyLiteralArray,          // opcode
      Operator::kEliminatable,                       // properties
      "JSCreateEmptyLiteralArray",                   // name
      kArity, 1, 1, 1, 1, 0,                         // counts
      parameters);                                   // parameter
}

const Operator* JSOperatorBuilder::CreateArrayFromIterable() {
  return zone()->New<Operator>(              // --
      IrOpcode::kJSCreateArrayFromIterable,  // opcode
      Operator::kNoProperties,               // properties
      "JSCreateArrayFromIterable",           // name
      1, 1, 1, 1, 1, 2);                     // counts
}

const Operator* JSOperatorBuilder::CreateLiteralObject(
    ObjectBoilerplateDescriptionRef constant_properties,
    FeedbackSource const& feedback, int literal_flags,
    int number_of_properties) {
  CreateLiteralParameters parameters(constant_properties, feedback,
                                     number_of_properties, literal_flags);
  return zone()->New<Operator1<CreateLiteralParameters>>(  // --
      IrOpcode::kJSCreateLiteralObject,                    // opcode
      Operator::kNoProperties,                             // properties
      "JSCreateLiteralObject",                             // name
      1, 1, 1, 1, 1, 2,                                    // counts
      parameters);                                         // parameter
}

const Operator* JSOperatorBuilder::GetTemplateObject(
    TemplateObjectDescriptionRef description, SharedFunctionInfoRef shared,
    FeedbackSource const& feedback) {
  GetTemplateObjectParameters parameters(description, shared, feedback);
  return zone()->New<Operator1<GetTemplateObjectParameters>>(  // --
      IrOpcode::kJSGetTemplateObject,                          // opcode
      Operator::kEliminatable,                                 // properties
      "JSGetTemplateObject",                                   // name
      1, 1, 1, 1, 1, 0,                                        // counts
      parameters);                                             // parameter
}

const Operator* JSOperatorBuilder::CloneObject(FeedbackSource const& feedback,
                                               int literal_flags) {
  CloneObjectParameters parameters(feedback, literal_flags);
  return zone()->New<Operator1<CloneObjectParameters>>(  // --
      IrOpcode::kJSCloneObject,                          // opcode
      Operator::kNoProperties,                           // properties
      "JSCloneObject",                                   // name
      2, 1, 1, 1, 1, 2,                                  // counts
      parameters);                                       // parameter
}

const Operator* JSOperatorBuilder::StackCheck(StackCheckKind kind) {
  Operator::Properties properties;
  switch (kind) {
    case StackCheckKind::kJSFunctionEntry:
    case StackCheckKind::kCodeStubAssembler:
    case StackCheckKind::kWasm:
      properties = Operator::kNoProperties;
      break;
    case StackCheckKind::kJSIterationBody:
      properties = Operator::kNoWrite;
      break;
  }
  return zone()->New<Operator1<StackCheckKind>>(  // --
      IrOpcode::kJSStackCheck,                    // opcode
      properties,                                 // properties
      "JSStackCheck",                             // name
      0, 1, 1, 0, 1, 2,                           // counts
      kind);                                      // parameter
}

const Operator* JSOperatorBuilder::CreateEmptyLiteralObject() {
  return zone()->New<Operator>(               // --
      IrOpcode::kJSCreateEmptyLiteralObject,  // opcode
      Operator::kNoProperties,                // properties
      "JSCreateEmptyLiteralObject",           // name
      0, 1, 1, 1, 1, 2);                      // counts
}

const Operator* JSOperatorBuilder::CreateLiteralRegExp(
    StringRef constant_pattern, FeedbackSource const& feedback,
    int literal_flags) {
  CreateLiteralParameters parameters(constant_pattern, feedback, -1,
                                     literal_flags);
  return zone()->New<Operator1<CreateLiteralParameters>>(  // --
      IrOpcode::kJSCreateLiteralRegExp,                    // opcode
      Operator::kNoProperties,                             // properties
      "JSCreateLiteralRegExp",                             // name
      1, 1, 1, 1, 1, 2,                                    // counts
      parameters);                                         // parameter
}

const Operator* JSOperatorBuilder::CreateFunctionContext(
    ScopeInfoRef scope_info, int slot_count, ScopeType scope_type) {
  CreateFunctionContextParameters parameters(scope_info, slot_count,
                                             scope_type);
  return zone()->New<Operator1<CreateFunctionContextParameters>>(   // --
      IrOpcode::kJSCreateFunctionContext, Operator::kNoProperties,  // opcode
      "JSCreateFunctionContext",                                    // name
      0, 1, 1, 1, 1, 2,                                             // counts
      parameters);                                                  // parameter
}

const Operator* JSOperatorBuilder::CreateCatchContext(ScopeInfoRef scope_info) {
  return zone()->New<Operator1<ScopeInfoRef>>(
      IrOpcode::kJSCreateCatchContext, Operator::kNoProperties,  // opcode
      "JSCreateCatchContext",                                    // name
      1, 1, 1, 1, 1, 2,                                          // counts
      ScopeInfoRef{scope_info});                                 // parameter
}

const Operator* JSOperatorBuilder::CreateWithContext(ScopeInfoRef scope_info) {
  return zone()->New<Operator1<ScopeInfoRef>>(
      IrOpcode::kJSCreateWithContext, Operator::kNoProperties,  // opcode
      "JSCreateWithContext",                                    // name
      1, 1, 1, 1, 1, 2,                                         // counts
      ScopeInfoRef{scope_info});                                // parameter
}

const Operator* JSOperatorBuilder::CreateBlockContext(ScopeInfoRef scope_info) {
  return zone()->New<Operator1<ScopeInfoRef>>(                   // --
      IrOpcode::kJSCreateBlockContext, Operator::kNoProperties,  // opcode
      "JSCreateBlockContext",                                    // name
      0, 1, 1, 1, 1, 2,                                          // counts
      ScopeInfoRef{scope_info});                                 // parameter
}

ScopeInfoRef ScopeInfoOf(const Operator* op) {
  DCHECK(IrOpcode::kJSCreateBlockContext == op->opcode() ||
         IrOpcode::kJSCreateWithContext == op->opcode() ||
         IrOpcode::kJSCreateCatchContext == op->opcode());
  return OpParameter<ScopeInfoRef>(op);
}

bool operator==(ScopeInfoRef lhs, ScopeInfoRef rhs) {
  return lhs.object().location() == rhs.object().location();
}

bool operator!=(ScopeInfoRef lhs, ScopeInfoRef rhs) { return !(lhs == rhs); }

size_t hash_value(ScopeInfoRef ref) {
  return reinterpret_cast<size_t>(ref.object().location());
}

std::ostream& operator<<(std::ostream& os, ScopeInfoRef ref) {
  return os << Brief(*ref.object());
}

#undef CACHED_OP_LIST

}  // namespace compiler
}  // namespace internal
}  // namespace v8
