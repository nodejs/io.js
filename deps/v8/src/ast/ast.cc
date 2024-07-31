// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/ast.h"

#include <cmath>  // For isfinite.
#include <vector>

#include "src/ast/prettyprinter.h"
#include "src/ast/scopes.h"
#include "src/base/hashmap.h"
#include "src/base/logging.h"
#include "src/base/numbers/double.h"
#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins.h"
#include "src/common/assert-scope.h"
#include "src/heap/local-factory-inl.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/contexts.h"
#include "src/objects/elements-kind.h"
#include "src/objects/elements.h"
#include "src/objects/fixed-array.h"
#include "src/objects/literal-objects-inl.h"
#include "src/objects/literal-objects.h"
#include "src/objects/map.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-details.h"
#include "src/objects/property.h"
#include "src/strings/string-stream.h"
#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// Implementation of other node functionality.

#ifdef DEBUG

static const char* NameForNativeContextIntrinsicIndex(uint32_t idx) {
  switch (idx) {
#define NATIVE_CONTEXT_FIELDS_IDX(NAME, Type, name) \
  case Context::NAME:                               \
    return #name;

    NATIVE_CONTEXT_FIELDS(NATIVE_CONTEXT_FIELDS_IDX)
#undef NATIVE_CONTEXT_FIELDS_IDX

    default:
      break;
  }

  return "UnknownIntrinsicIndex";
}

void AstNode::Print(Isolate* isolate) {
  AstPrinter::PrintOut(isolate, this);
}

#endif  // DEBUG

#define RETURN_NODE(Node) \
  case k##Node:           \
    return static_cast<Node*>(this);

IterationStatement* AstNode::AsIterationStatement() {
  switch (node_type()) {
    ITERATION_NODE_LIST(RETURN_NODE);
    default:
      return nullptr;
  }
}

MaterializedLiteral* AstNode::AsMaterializedLiteral() {
  switch (node_type()) {
    LITERAL_NODE_LIST(RETURN_NODE);
    default:
      return nullptr;
  }
}

#undef RETURN_NODE

bool Expression::IsSmiLiteral() const {
  return IsLiteral() && AsLiteral()->type() == Literal::kSmi;
}

bool Expression::IsNumberLiteral() const {
  return IsLiteral() && AsLiteral()->IsNumber();
}

bool Expression::IsStringLiteral() const {
  return IsLiteral() && AsLiteral()->type() == Literal::kString;
}

bool Expression::IsPropertyName() const {
  return IsLiteral() && AsLiteral()->IsPropertyName();
}

bool Expression::IsNullLiteral() const {
  return IsLiteral() && AsLiteral()->type() == Literal::kNull;
}

bool Expression::IsBooleanLiteral() const {
  return IsLiteral() && AsLiteral()->type() == Literal::kBoolean;
}

bool Expression::IsTheHoleLiteral() const {
  return IsLiteral() && AsLiteral()->type() == Literal::kTheHole;
}

bool Expression::IsCompileTimeValue() {
  if (IsLiteral()) return true;
  MaterializedLiteral* literal = AsMaterializedLiteral();
  if (literal == nullptr) return false;
  return literal->IsSimple();
}

bool Expression::IsUndefinedLiteral() const {
  if (IsLiteral() && AsLiteral()->type() == Literal::kUndefined) return true;

  const VariableProxy* var_proxy = AsVariableProxy();
  if (var_proxy == nullptr) return false;
  Variable* var = var_proxy->var();
  // The global identifier "undefined" is immutable. Everything
  // else could be reassigned.
  return var != nullptr && var->IsUnallocated() &&
         var_proxy->raw_name()->IsOneByteEqualTo("undefined");
}

bool Expression::IsLiteralButNotNullOrUndefined() const {
  return IsLiteral() && !IsNullOrUndefinedLiteral();
}

bool Expression::ToBooleanIsTrue() const {
  return IsLiteral() && AsLiteral()->ToBooleanIsTrue();
}

bool Expression::ToBooleanIsFalse() const {
  return IsLiteral() && AsLiteral()->ToBooleanIsFalse();
}

bool Expression::IsPrivateName() const {
  return IsVariableProxy() && AsVariableProxy()->IsPrivateName();
}

bool Expression::IsValidReferenceExpression() const {
  return IsProperty() ||
         (IsVariableProxy() && AsVariableProxy()->IsValidReferenceExpression());
}

bool Expression::IsAnonymousFunctionDefinition() const {
  return (IsFunctionLiteral() &&
          AsFunctionLiteral()->IsAnonymousFunctionDefinition()) ||
         (IsClassLiteral() &&
          AsClassLiteral()->IsAnonymousFunctionDefinition());
}

bool Expression::IsConciseMethodDefinition() const {
  return IsFunctionLiteral() && IsConciseMethod(AsFunctionLiteral()->kind());
}

bool Expression::IsAccessorFunctionDefinition() const {
  return IsFunctionLiteral() && IsAccessorFunction(AsFunctionLiteral()->kind());
}

VariableProxy::VariableProxy(Variable* var, int start_position)
    : Expression(start_position, kVariableProxy),
      raw_name_(var->raw_name()),
      next_unresolved_(nullptr) {
  DCHECK(!var->is_this());
  bit_field_ |= IsAssignedField::encode(false) |
                IsResolvedField::encode(false) |
                HoleCheckModeField::encode(HoleCheckMode::kElided);
  BindTo(var);
}

VariableProxy::VariableProxy(const VariableProxy* copy_from)
    : Expression(copy_from->position(), kVariableProxy),
      next_unresolved_(nullptr) {
  bit_field_ = copy_from->bit_field_;
  DCHECK(!copy_from->is_resolved());
  raw_name_ = copy_from->raw_name_;
}

void VariableProxy::BindTo(Variable* var) {
  DCHECK_EQ(raw_name(), var->raw_name());
  set_var(var);
  set_is_resolved();
  var->set_is_used();
  if (is_assigned()) var->SetMaybeAssigned();
}

Assignment::Assignment(NodeType node_type, Token::Value op, Expression* target,
                       Expression* value, int pos)
    : Expression(pos, node_type), target_(target), value_(value) {
  bit_field_ |= TokenField::encode(op);
}

void FunctionLiteral::set_inferred_name(Handle<String> inferred_name) {
  DCHECK(!inferred_name.is_null());
  inferred_name_ = inferred_name;
  DCHECK(raw_inferred_name_ == nullptr || raw_inferred_name_->IsEmpty());
  raw_inferred_name_ = nullptr;
  scope()->set_has_inferred_function_name(true);
}

void FunctionLiteral::set_raw_inferred_name(AstConsString* raw_inferred_name) {
  DCHECK_NOT_NULL(raw_inferred_name);
  raw_inferred_name_ = raw_inferred_name;
  DCHECK(inferred_name_.is_null());
  inferred_name_ = Handle<String>();
  scope()->set_has_inferred_function_name(true);
}

bool FunctionLiteral::ShouldEagerCompile() const {
  return scope()->ShouldEagerCompile();
}

void FunctionLiteral::SetShouldEagerCompile() {
  scope()->set_should_eager_compile();
}

bool FunctionLiteral::AllowsLazyCompilation() {
  return scope()->AllowsLazyCompilation();
}

int FunctionLiteral::start_position() const {
  return scope()->start_position();
}

int FunctionLiteral::end_position() const { return scope()->end_position(); }

LanguageMode FunctionLiteral::language_mode() const {
  return scope()->language_mode();
}

FunctionKind FunctionLiteral::kind() const { return scope()->function_kind(); }

std::unique_ptr<char[]> FunctionLiteral::GetDebugName() const {
  const AstConsString* cons_string;
  if (raw_name_ != nullptr && !raw_name_->IsEmpty()) {
    cons_string = raw_name_;
  } else if (raw_inferred_name_ != nullptr && !raw_inferred_name_->IsEmpty()) {
    cons_string = raw_inferred_name_;
  } else if (!inferred_name_.is_null()) {
    return inferred_name_->ToCString();
  } else {
    char* empty_str = new char[1];
    empty_str[0] = 0;
    return std::unique_ptr<char[]>(empty_str);
  }

  // TODO(rmcilroy): Deal with two-character strings.
  std::vector<char> result_vec;
  std::forward_list<const AstRawString*> strings = cons_string->ToRawStrings();
  for (const AstRawString* string : strings) {
    if (!string->is_one_byte()) break;
    for (int i = 0; i < string->length(); i++) {
      result_vec.push_back(string->raw_data()[i]);
    }
  }
  std::unique_ptr<char[]> result(new char[result_vec.size() + 1]);
  memcpy(result.get(), result_vec.data(), result_vec.size());
  result[result_vec.size()] = '\0';
  return result;
}

bool FunctionLiteral::private_name_lookup_skips_outer_class() const {
  return scope()->private_name_lookup_skips_outer_class();
}

bool FunctionLiteral::class_scope_has_private_brand() const {
  return scope()->class_scope_has_private_brand();
}

void FunctionLiteral::set_class_scope_has_private_brand(bool value) {
  return scope()->set_class_scope_has_private_brand(value);
}

ObjectLiteralProperty::ObjectLiteralProperty(Expression* key, Expression* value,
                                             Kind kind, bool is_computed_name)
    : LiteralProperty(key, value, is_computed_name),
      kind_(kind),
      emit_store_(true) {}

ObjectLiteralProperty::ObjectLiteralProperty(AstValueFactory* ast_value_factory,
                                             Expression* key, Expression* value,
                                             bool is_computed_name)
    : LiteralProperty(key, value, is_computed_name), emit_store_(true) {
  if (!is_computed_name && key->AsLiteral()->IsString() &&
      key->AsLiteral()->AsRawString() == ast_value_factory->proto_string()) {
    kind_ = PROTOTYPE;
  } else if (value_->AsMaterializedLiteral() != nullptr) {
    kind_ = MATERIALIZED_LITERAL;
  } else if (value_->IsLiteral()) {
    kind_ = CONSTANT;
  } else {
    kind_ = COMPUTED;
  }
}

bool LiteralProperty::NeedsSetFunctionName() const {
  return is_computed_name() && (value_->IsAnonymousFunctionDefinition() ||
                                value_->IsConciseMethodDefinition() ||
                                value_->IsAccessorFunctionDefinition());
}

ClassLiteralProperty::ClassLiteralProperty(Expression* key, Expression* value,
                                           Kind kind, bool is_static,
                                           bool is_computed_name,
                                           bool is_private)
    : LiteralProperty(key, value, is_computed_name),
      kind_(kind),
      is_static_(is_static),
      is_private_(is_private),
      private_or_computed_name_var_(nullptr) {}

bool ObjectLiteral::Property::IsCompileTimeValue() const {
  return kind_ == CONSTANT ||
         (kind_ == MATERIALIZED_LITERAL && value_->IsCompileTimeValue());
}

void ObjectLiteral::Property::set_emit_store(bool emit_store) {
  emit_store_ = emit_store;
}

bool ObjectLiteral::Property::emit_store() const { return emit_store_; }

void ObjectLiteral::CalculateEmitStore(Zone* zone) {
  const auto GETTER = ObjectLiteral::Property::GETTER;
  const auto SETTER = ObjectLiteral::Property::SETTER;

  CustomMatcherZoneHashMap table(Literal::Match,
                                 ZoneHashMap::kDefaultHashMapCapacity,
                                 ZoneAllocationPolicy(zone));
  for (int i = properties()->length() - 1; i >= 0; i--) {
    ObjectLiteral::Property* property = properties()->at(i);
    if (property->is_computed_name()) continue;
    if (property->IsPrototype()) continue;
    Literal* literal = property->key()->AsLiteral();
    DCHECK(!literal->IsNullLiteral());

    uint32_t hash = literal->Hash();
    ZoneHashMap::Entry* entry = table.LookupOrInsert(literal, hash);
    if (entry->value == nullptr) {
      entry->value = property;
    } else {
      // We already have a later definition of this property, so we don't need
      // to emit a store for the current one.
      //
      // There are two subtleties here.
      //
      // (1) Emitting a store might actually be incorrect. For example, in {get
      // foo() {}, foo: 42}, the getter store would override the data property
      // (which, being a non-computed compile-time valued property, is already
      // part of the initial literal object.
      //
      // (2) If the later definition is an accessor (say, a getter), and the
      // current definition is a complementary accessor (here, a setter), then
      // we still must emit a store for the current definition.

      auto later_kind =
          static_cast<ObjectLiteral::Property*>(entry->value)->kind();
      bool complementary_accessors =
          (property->kind() == GETTER && later_kind == SETTER) ||
          (property->kind() == SETTER && later_kind == GETTER);
      if (!complementary_accessors) {
        property->set_emit_store(false);
        if (later_kind == GETTER || later_kind == SETTER) {
          entry->value = property;
        }
      }
    }
  }
}

int ObjectLiteralBoilerplateBuilder::ComputeFlags(bool disable_mementos) const {
  int flags = LiteralBoilerplateBuilder::ComputeFlags(disable_mementos);
  if (fast_elements()) flags |= ObjectLiteral::kFastElements;
  if (has_null_prototype()) flags |= ObjectLiteral::kHasNullPrototype;
  return flags;
}

void ObjectLiteralBoilerplateBuilder::InitFlagsForPendingNullPrototype(int i) {
  // We still check for __proto__:null after computed property names.
  for (; i < properties()->length(); i++) {
    if (properties()->at(i)->IsNullPrototype()) {
      set_has_null_protoype(true);
      break;
    }
  }
}

int ObjectLiteralBoilerplateBuilder::EncodeLiteralType() {
  int flags = AggregateLiteral::kNoFlags;
  if (fast_elements()) flags |= ObjectLiteral::kFastElements;
  if (has_null_prototype()) flags |= ObjectLiteral::kHasNullPrototype;
  return flags;
}

void ObjectLiteralBoilerplateBuilder::InitDepthAndFlags() {
  if (is_initialized()) return;
  bool is_simple = true;
  bool has_seen_prototype = false;
  bool needs_initial_allocation_site = false;
  DepthKind depth_acc = kShallow;
  uint32_t nof_properties = 0;
  uint32_t elements = 0;
  uint32_t max_element_index = 0;
  for (int i = 0; i < properties()->length(); i++) {
    ObjectLiteral::Property* property = properties()->at(i);
    if (property->IsPrototype()) {
      has_seen_prototype = true;
      // __proto__:null has no side-effects and is set directly on the
      // boilerplate.
      if (property->IsNullPrototype()) {
        set_has_null_protoype(true);
        continue;
      }
      DCHECK(!has_null_prototype());
      is_simple = false;
      continue;
    }
    if (nof_properties == boilerplate_properties_) {
      DCHECK(property->is_computed_name());
      is_simple = false;
      if (!has_seen_prototype) InitFlagsForPendingNullPrototype(i);
      break;
    }
    DCHECK(!property->is_computed_name());

    MaterializedLiteral* literal = property->value()->AsMaterializedLiteral();
    if (literal != nullptr) {
      LiteralBoilerplateBuilder::InitDepthAndFlags(literal);
      depth_acc = kNotShallow;
      needs_initial_allocation_site |= literal->NeedsInitialAllocationSite();
    }

    Literal* key = property->key()->AsLiteral();
    Expression* value = property->value();

    bool is_compile_time_value = value->IsCompileTimeValue();
    is_simple = is_simple && is_compile_time_value;

    // Keep track of the number of elements in the object literal and
    // the largest element index.  If the largest element index is
    // much larger than the number of elements, creating an object
    // literal with fast elements will be a waste of space.
    uint32_t element_index = 0;
    if (key->AsArrayIndex(&element_index)) {
      max_element_index = std::max(element_index, max_element_index);
      elements++;
    } else {
      DCHECK(key->IsPropertyName());
    }

    nof_properties++;
  }

  set_depth(depth_acc);
  set_is_simple(is_simple);
  set_needs_initial_allocation_site(needs_initial_allocation_site);
  set_has_elements(elements > 0);
  set_fast_elements((max_element_index <= 32) ||
                    ((2 * elements) >= max_element_index));
}

template <typename IsolateT>
void ObjectLiteralBoilerplateBuilder::BuildBoilerplateDescription(
    IsolateT* isolate) {
  if (!boilerplate_description_.is_null()) return;

  int index_keys = 0;
  bool has_seen_proto = false;
  for (int i = 0; i < properties()->length(); i++) {
    ObjectLiteral::Property* property = properties()->at(i);
    if (property->IsPrototype()) {
      has_seen_proto = true;
      continue;
    }
    if (property->is_computed_name()) continue;

    Literal* key = property->key()->AsLiteral();
    if (!key->IsPropertyName()) index_keys++;
  }

  Handle<ObjectBoilerplateDescription> boilerplate_description =
      isolate->factory()->NewObjectBoilerplateDescription(
          boilerplate_properties_, properties()->length(), index_keys,
          has_seen_proto);

  int position = 0;
  for (int i = 0; i < properties()->length(); i++) {
    ObjectLiteral::Property* property = properties()->at(i);
    if (property->IsPrototype()) continue;

    if (static_cast<uint32_t>(position) == boilerplate_properties_) {
      DCHECK(property->is_computed_name());
      break;
    }
    DCHECK(!property->is_computed_name());

    MaterializedLiteral* m_literal = property->value()->AsMaterializedLiteral();
    if (m_literal != nullptr) {
      BuildConstants(isolate, m_literal);
    }

    // Add CONSTANT and COMPUTED properties to boilerplate. Use the
    // 'uninitialized' Oddball for COMPUTED properties, the real value is filled
    // in at runtime. The enumeration order is maintained.
    Literal* key_literal = property->key()->AsLiteral();
    uint32_t element_index = 0;
    Handle<Object> key =
        key_literal->AsArrayIndex(&element_index)
            ? isolate->factory()
                  ->template NewNumberFromUint<AllocationType::kOld>(
                      element_index)
            : Handle<Object>::cast(key_literal->AsRawPropertyName()->string());
    Handle<Object> value = GetBoilerplateValue(property->value(), isolate);
    boilerplate_description->set_key_value(position++, *key, *value);
  }

  boilerplate_description->set_flags(EncodeLiteralType());

  boilerplate_description_ = boilerplate_description;
}
template EXPORT_TEMPLATE_DEFINE(
    V8_BASE_EXPORT) void ObjectLiteralBoilerplateBuilder::
    BuildBoilerplateDescription(Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(
    V8_BASE_EXPORT) void ObjectLiteralBoilerplateBuilder::
    BuildBoilerplateDescription(LocalIsolate* isolate);

bool ObjectLiteralBoilerplateBuilder::IsFastCloningSupported() const {
  // The CreateShallowObjectLiteratal builtin doesn't copy elements, and object
  // literals don't support copy-on-write (COW) elements for now.
  // TODO(mvstanton): make object literals support COW elements.
  return fast_elements() && is_shallow() &&
         properties_count() <=
             ConstructorBuiltins::kMaximumClonedShallowObjectProperties;
}

// static
template <typename IsolateT>
Handle<Object> LiteralBoilerplateBuilder::GetBoilerplateValue(
    Expression* expression, IsolateT* isolate) {
  if (expression->IsLiteral()) {
    return expression->AsLiteral()->BuildValue(isolate);
  }
  if (expression->IsCompileTimeValue()) {
    if (expression->IsObjectLiteral()) {
      ObjectLiteral* object_literal = expression->AsObjectLiteral();
      DCHECK(object_literal->builder()->is_simple());
      return object_literal->builder()->boilerplate_description();
    } else {
      DCHECK(expression->IsArrayLiteral());
      ArrayLiteral* array_literal = expression->AsArrayLiteral();
      DCHECK(array_literal->builder()->is_simple());
      return array_literal->builder()->boilerplate_description();
    }
  }
  return isolate->factory()->uninitialized_value();
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<Object> LiteralBoilerplateBuilder::GetBoilerplateValue(
        Expression* expression, Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<Object> LiteralBoilerplateBuilder::GetBoilerplateValue(
        Expression* expression, LocalIsolate* isolate);

void ArrayLiteralBoilerplateBuilder::InitDepthAndFlags() {
  if (is_initialized()) return;

  int constants_length =
      first_spread_index_ >= 0 ? first_spread_index_ : values_->length();

  // Fill in the literals.
  bool is_simple = first_spread_index_ < 0;
  bool is_holey = false;
  ElementsKind kind = FIRST_FAST_ELEMENTS_KIND;
  DepthKind depth_acc = kShallow;
  int array_index = 0;
  for (; array_index < constants_length; array_index++) {
    Expression* element = values_->at(array_index);
    MaterializedLiteral* materialized_literal =
        element->AsMaterializedLiteral();
    if (materialized_literal != nullptr) {
      LiteralBoilerplateBuilder::InitDepthAndFlags(materialized_literal);
      depth_acc = kNotShallow;
    }

    if (!element->IsCompileTimeValue()) {
      is_simple = false;

      // Don't change kind here: non-compile time values resolve to an unknown
      // elements kind, so we allow them to be considered as any one of them.

      // TODO(leszeks): It would be nice to DCHECK here that GetBoilerplateValue
      // will return IsUninitialized, but that would require being on the main
      // thread which we may not be.
    } else {
      Literal* literal = element->AsLiteral();

      if (!literal) {
        // Only arrays and objects are compile-time values but not (primitive)
        // literals.
        DCHECK(element->IsObjectLiteral() || element->IsArrayLiteral());
        kind = PACKED_ELEMENTS;
      } else {
        switch (literal->type()) {
          case Literal::kTheHole:
            is_holey = true;
            // The hole is allowed in holey double arrays (and holey Smi
            // arrays), so ignore it as far as is_all_number is concerned.
            break;
          case Literal::kHeapNumber:
            if (kind == PACKED_SMI_ELEMENTS) kind = PACKED_DOUBLE_ELEMENTS;
            DCHECK_EQ(kind,
                      GetMoreGeneralElementsKind(kind, PACKED_DOUBLE_ELEMENTS));
            break;
          case Literal::kSmi:
            DCHECK_EQ(kind,
                      GetMoreGeneralElementsKind(kind, PACKED_SMI_ELEMENTS));
            break;
          case Literal::kBigInt:
          case Literal::kString:
          case Literal::kBoolean:
          case Literal::kUndefined:
          case Literal::kNull:
            kind = PACKED_ELEMENTS;
            break;
        }
      }
    }
  }

  if (is_holey) {
    kind = GetHoleyElementsKind(kind);
  }

  set_depth(depth_acc);
  set_is_simple(is_simple);
  set_boilerplate_descriptor_kind(kind);

  // Array literals always need an initial allocation site to properly track
  // elements transitions.
  set_needs_initial_allocation_site(true);
}

template <typename IsolateT>
void ArrayLiteralBoilerplateBuilder::BuildBoilerplateDescription(
    IsolateT* isolate) {
  if (!boilerplate_description_.is_null()) return;

  int constants_length =
      first_spread_index_ >= 0 ? first_spread_index_ : values_->length();
  ElementsKind kind = boilerplate_descriptor_kind();
  bool use_doubles = IsDoubleElementsKind(kind);

  Handle<FixedArrayBase> elements;
  if (use_doubles) {
    elements = isolate->factory()->NewFixedDoubleArray(constants_length,
                                                       AllocationType::kOld);
  } else {
    elements = isolate->factory()->NewFixedArrayWithHoles(constants_length,
                                                          AllocationType::kOld);
  }

  // Fill in the literals.
  int array_index = 0;
  for (; array_index < constants_length; array_index++) {
    Expression* element = values_->at(array_index);
    DCHECK(!element->IsSpread());
    if (use_doubles) {
      Literal* literal = element->AsLiteral();

      if (literal && literal->type() == Literal::kTheHole) {
        DCHECK(IsHoleyElementsKind(kind));
        DCHECK(IsTheHole(*GetBoilerplateValue(element, isolate), isolate));
        FixedDoubleArray::cast(*elements)->set_the_hole(array_index);
        continue;
      } else if (literal && literal->IsNumber()) {
        FixedDoubleArray::cast(*elements)->set(array_index,
                                               literal->AsNumber());
      } else {
        DCHECK(
            IsUninitialized(*GetBoilerplateValue(element, isolate), isolate));
        FixedDoubleArray::cast(*elements)->set(array_index, 0);
      }

    } else {
      MaterializedLiteral* m_literal = element->AsMaterializedLiteral();
      if (m_literal != nullptr) {
        BuildConstants(isolate, m_literal);
      }

      // New handle scope here, needs to be after BuildContants().
      typename IsolateT::HandleScopeType scope(isolate);

      Tagged<Object> boilerplate_value = *GetBoilerplateValue(element, isolate);
      // We shouldn't allocate after creating the boilerplate value.
      DisallowGarbageCollection no_gc;

      if (IsTheHole(boilerplate_value, isolate)) {
        DCHECK(IsHoleyElementsKind(kind));
        continue;
      }

      if (IsUninitialized(boilerplate_value, isolate)) {
        boilerplate_value = Smi::zero();
      }

      DCHECK_EQ(kind, GetMoreGeneralElementsKind(
                          kind, Object::OptimalElementsKind(
                                    boilerplate_value,
                                    GetPtrComprCageBase(*elements))));

      FixedArray::cast(*elements)->set(array_index, boilerplate_value);
    }
  }  // namespace internal

  // Simple and shallow arrays can be lazily copied, we transform the
  // elements array to a copy-on-write array.
  if (is_simple() && depth() == kShallow && array_index > 0 &&
      IsSmiOrObjectElementsKind(kind)) {
    elements->set_map_safe_transition(
        ReadOnlyRoots(isolate).fixed_cow_array_map());
  }

  boilerplate_description_ =
      isolate->factory()->NewArrayBoilerplateDescription(kind, elements);
}
template EXPORT_TEMPLATE_DEFINE(
    V8_BASE_EXPORT) void ArrayLiteralBoilerplateBuilder::
    BuildBoilerplateDescription(Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(
    V8_BASE_EXPORT) void ArrayLiteralBoilerplateBuilder::
    BuildBoilerplateDescription(LocalIsolate*

                                    isolate);

bool ArrayLiteralBoilerplateBuilder::IsFastCloningSupported() const {
  return depth() <= kShallow &&
         values_->length() <=
             ConstructorBuiltins::kMaximumClonedShallowArrayElements;
}

bool MaterializedLiteral::IsSimple() const {
  if (IsArrayLiteral()) return AsArrayLiteral()->builder()->is_simple();
  if (IsObjectLiteral()) return AsObjectLiteral()->builder()->is_simple();
  DCHECK(IsRegExpLiteral());
  return false;
}

// static
void LiteralBoilerplateBuilder::InitDepthAndFlags(MaterializedLiteral* expr) {
  if (expr->IsArrayLiteral()) {
    return expr->AsArrayLiteral()->builder()->InitDepthAndFlags();
  }
  if (expr->IsObjectLiteral()) {
    return expr->AsObjectLiteral()->builder()->InitDepthAndFlags();
  }
  DCHECK(expr->IsRegExpLiteral());
}

bool MaterializedLiteral::NeedsInitialAllocationSite(

) {
  if (IsArrayLiteral()) {
    return AsArrayLiteral()->builder()->needs_initial_allocation_site();
  }
  if (IsObjectLiteral()) {
    return AsObjectLiteral()->builder()->needs_initial_allocation_site();
  }
  DCHECK(IsRegExpLiteral());
  return false;
}

template <typename IsolateT>
void LiteralBoilerplateBuilder::BuildConstants(IsolateT* isolate,
                                               MaterializedLiteral* expr) {
  if (expr->IsArrayLiteral()) {
    expr->AsArrayLiteral()->builder()->BuildBoilerplateDescription(isolate);
    return;
  }
  if (expr->IsObjectLiteral()) {
    expr->AsObjectLiteral()->builder()->BuildBoilerplateDescription(isolate);
    return;
  }
  DCHECK(expr->IsRegExpLiteral());
}
template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT) void LiteralBoilerplateBuilder::
    BuildConstants(Isolate* isolate, MaterializedLiteral* expr);
template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT) void LiteralBoilerplateBuilder::
    BuildConstants(LocalIsolate* isolate, MaterializedLiteral* expr);

template <typename IsolateT>
Handle<TemplateObjectDescription> GetTemplateObject::GetOrBuildDescription(
    IsolateT* isolate) {
  Handle<FixedArray> raw_strings_handle = isolate->factory()->NewFixedArray(
      this->raw_strings()->length(), AllocationType::kOld);
  bool raw_and_cooked_match = true;
  {
    DisallowGarbageCollection no_gc;
    Tagged<FixedArray> raw_strings = *raw_strings_handle;

    for (int i = 0; i < raw_strings->length(); ++i) {
      if (this->raw_strings()->at(i) != this->cooked_strings()->at(i)) {
        // If the AstRawStrings don't match, then neither should the allocated
        // Strings, since the AstValueFactory should have deduplicated them
        // already.
        DCHECK_IMPLIES(this->cooked_strings()->at(i) != nullptr,
                       *this->cooked_strings()->at(i)->string() !=
                           *this->raw_strings()->at(i)->string());

        raw_and_cooked_match = false;
      }
      raw_strings->set(i, *this->raw_strings()->at(i)->string());
    }
  }
  Handle<FixedArray> cooked_strings_handle = raw_strings_handle;
  if (!raw_and_cooked_match) {
    cooked_strings_handle = isolate->factory()->NewFixedArray(
        this->cooked_strings()->length(), AllocationType::kOld);
    DisallowGarbageCollection no_gc;
    Tagged<FixedArray> cooked_strings = *cooked_strings_handle;
    ReadOnlyRoots roots(isolate);
    for (int i = 0; i < cooked_strings->length(); ++i) {
      if (this->cooked_strings()->at(i) != nullptr) {
        cooked_strings->set(i, *this->cooked_strings()->at(i)->string());
      } else {
        cooked_strings->set(i, roots.undefined_value(), SKIP_WRITE_BARRIER);
      }
    }
  }
  return isolate->factory()->NewTemplateObjectDescription(
      raw_strings_handle, cooked_strings_handle);
}
template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    Handle<TemplateObjectDescription> GetTemplateObject::GetOrBuildDescription(
        Isolate* isolate);
template EXPORT_TEMPLATE_DEFINE(V8_BASE_EXPORT)
    Handle<TemplateObjectDescription> GetTemplateObject::GetOrBuildDescription(
        LocalIsolate* isolate);

static bool IsCommutativeOperationWithSmiLiteral(Token::Value op) {
  // Add is not commutative due to potential for string addition.
  return op == Token::kMul || op == Token::kBitAnd || op == Token::kBitOr ||
         op == Token::kBitXor;
}

// Check for the pattern: x + 1.
static bool MatchSmiLiteralOperation(Expression* left, Expression* right,
                                     Expression** expr, Tagged<Smi>* literal) {
  if (right->IsSmiLiteral()) {
    *expr = left;
    *literal = right->AsLiteral()->AsSmiLiteral();
    return true;
  }
  return false;
}

bool BinaryOperation::IsSmiLiteralOperation(Expression** subexpr,
                                            Tagged<Smi>* literal) {
  return MatchSmiLiteralOperation(left_, right_, subexpr, literal) ||
         (IsCommutativeOperationWithSmiLiteral(op()) &&
          MatchSmiLiteralOperation(right_, left_, subexpr, literal));
}

static bool IsVoidOfLiteral(Expression* expr) {
  UnaryOperation* maybe_unary = expr->AsUnaryOperation();
  return maybe_unary != nullptr && maybe_unary->op() == Token::kVoid &&
         maybe_unary->expression()->IsLiteral();
}

static bool MatchLiteralStrictCompareBoolean(Expression* left, Token::Value op,
                                             Expression* right,
                                             Expression** expr,
                                             Literal** literal) {
  if (left->IsBooleanLiteral() && op == Token::kEqStrict) {
    *expr = right;
    *literal = left->AsLiteral();
    return true;
  }
  return false;
}

bool CompareOperation::IsLiteralStrictCompareBoolean(Expression** expr,
                                                     Literal** literal) {
  return MatchLiteralStrictCompareBoolean(left_, op(), right_, expr, literal) ||
         MatchLiteralStrictCompareBoolean(right_, op(), left_, expr, literal);
}

// Check for the pattern: void <literal> equals <expression> or
// undefined equals <expression>
static bool MatchLiteralCompareUndefined(Expression* left, Token::Value op,
                                         Expression* right, Expression** expr) {
  if (IsVoidOfLiteral(left) && Token::IsEqualityOp(op)) {
    *expr = right;
    return true;
  }
  if (left->IsUndefinedLiteral() && Token::IsEqualityOp(op)) {
    *expr = right;
    return true;
  }
  return false;
}

bool CompareOperation::IsLiteralCompareUndefined(Expression** expr) {
  return MatchLiteralCompareUndefined(left_, op(), right_, expr) ||
         MatchLiteralCompareUndefined(right_, op(), left_, expr);
}

// Check for the pattern: null equals <expression>
static bool MatchLiteralCompareNull(Expression* left, Token::Value op,
                                    Expression* right, Expression** expr) {
  if (left->IsNullLiteral() && Token::IsEqualityOp(op)) {
    *expr = right;
    return true;
  }
  return false;
}

bool CompareOperation::IsLiteralCompareNull(Expression** expr) {
  return MatchLiteralCompareNull(left_, op(), right_, expr) ||
         MatchLiteralCompareNull(right_, op(), left_, expr);
}

static bool MatchLiteralCompareEqualVariable(Expression* left, Token::Value op,
                                             Expression* right,
                                             Expression** expr,
                                             Literal** literal) {
  if (Token::IsEqualityOp(op) && left->AsVariableProxy() &&
      right->IsStringLiteral()) {
    *expr = left->AsVariableProxy();
    *literal = right->AsLiteral();
    return true;
  }
  return false;
}

bool CompareOperation::IsLiteralCompareEqualVariable(Expression** expr,
                                                     Literal** literal) {
  return (
      MatchLiteralCompareEqualVariable(left_, op(), right_, expr, literal) ||
      MatchLiteralCompareEqualVariable(right_, op(), left_, expr, literal));
}

void CallBase::ComputeSpreadPosition() {
  int arguments_length = arguments_.length();
  int first_spread_index = 0;
  for (; first_spread_index < arguments_length; first_spread_index++) {
    if (arguments_.at(first_spread_index)->IsSpread()) break;
  }
  SpreadPosition position;
  if (first_spread_index == arguments_length - 1) {
    position = kHasFinalSpread;
  } else {
    DCHECK_LT(first_spread_index, arguments_length - 1);
    position = kHasNonFinalSpread;
  }
  bit_field_ |= SpreadPositionField::encode(position);
}

Call::CallType Call::GetCallType() const {
  VariableProxy* proxy = expression()->AsVariableProxy();
  if (proxy != nullptr) {
    if (proxy->var()->IsUnallocated()) {
      return GLOBAL_CALL;
    } else if (proxy->var()->IsLookupSlot()) {
      // Calls going through 'with' always use VariableMode::kDynamic rather
      // than VariableMode::kDynamicLocal or VariableMode::kDynamicGlobal.
      return proxy->var()->mode() == VariableMode::kDynamic ? WITH_CALL
                                                            : OTHER_CALL;
    }
  }

  if (expression()->IsSuperCallReference()) return SUPER_CALL;

  Property* property = expression()->AsProperty();
  bool is_optional_chain = false;
  if (V8_UNLIKELY(property == nullptr && expression()->IsOptionalChain())) {
    is_optional_chain = true;
    property = expression()->AsOptionalChain()->expression()->AsProperty();
  }
  if (property != nullptr) {
    if (property->IsPrivateReference()) {
      if (is_optional_chain) return PRIVATE_OPTIONAL_CHAIN_CALL;
      return PRIVATE_CALL;
    }
    bool is_super = property->IsSuperAccess();
    // `super?.` is not syntactically valid, so a property load cannot be both
    // super and an optional chain.
    DCHECK(!is_super || !is_optional_chain);
    if (property->key()->IsPropertyName()) {
      if (is_super) return NAMED_SUPER_PROPERTY_CALL;
      if (is_optional_chain) return NAMED_OPTIONAL_CHAIN_PROPERTY_CALL;
      return NAMED_PROPERTY_CALL;
    } else {
      if (is_super) return KEYED_SUPER_PROPERTY_CALL;
      if (is_optional_chain) return KEYED_OPTIONAL_CHAIN_PROPERTY_CALL;
      return KEYED_PROPERTY_CALL;
    }
  }

  return OTHER_CALL;
}

CaseClause::CaseClause(Zone* zone, Expression* label,
                       const ScopedPtrList<Statement>& statements)
    : label_(label), statements_(statements.ToConstVector(), zone) {}

bool Literal::IsPropertyName() const {
  if (type() != kString) return false;
  uint32_t index;
  return !string_->AsArrayIndex(&index);
}

bool Literal::ToUint32(uint32_t* value) const {
  switch (type()) {
    case kString:
      return string_->AsArrayIndex(value);
    case kSmi:
      if (smi_ < 0) return false;
      *value = static_cast<uint32_t>(smi_);
      return true;
    case kHeapNumber:
      return DoubleToUint32IfEqualToSelf(AsNumber(), value);
    default:
      return false;
  }
}

bool Literal::AsArrayIndex(uint32_t* value) const {
  return ToUint32(value) && *value != kMaxUInt32;
}

template <typename IsolateT>
Handle<Object> Literal::BuildValue(IsolateT* isolate) const {
  switch (type()) {
    case kSmi:
      return handle(Smi::FromInt(smi_), isolate);
    case kHeapNumber:
      return isolate->factory()->template NewNumber<AllocationType::kOld>(
          number_);
    case kString:
      return string_->string();
    case kBoolean:
      return isolate->factory()->ToBoolean(boolean_);
    case kNull:
      return isolate->factory()->null_value();
    case kUndefined:
      return isolate->factory()->undefined_value();
    case kTheHole:
      return isolate->factory()->the_hole_value();
    case kBigInt:
      // This should never fail: the parser will never create a BigInt
      // literal that cannot be allocated.
      return BigIntLiteral(isolate, bigint_.c_str()).ToHandleChecked();
  }
  UNREACHABLE();
}
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<Object> Literal::BuildValue(Isolate* isolate) const;
template EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    Handle<Object> Literal::BuildValue(LocalIsolate* isolate) const;

bool Literal::ToBooleanIsTrue() const {
  switch (type()) {
    case kSmi:
      return smi_ != 0;
    case kHeapNumber:
      return DoubleToBoolean(number_);
    case kString:
      return !string_->IsEmpty();
    case kNull:
    case kUndefined:
      return false;
    case kBoolean:
      return boolean_;
    case kBigInt: {
      const char* bigint_str = bigint_.c_str();
      size_t length = strlen(bigint_str);
      DCHECK_GT(length, 0);
      if (length == 1 && bigint_str[0] == '0') return false;
      // Skip over any radix prefix; BigInts with length > 1 only
      // begin with zero if they include a radix.
      for (size_t i = (bigint_str[0] == '0') ? 2 : 0; i < length; ++i) {
        if (bigint_str[i] != '0') return true;
      }
      return false;
    }
    case kTheHole:
      UNREACHABLE();
  }
  UNREACHABLE();
}

uint32_t Literal::Hash() {
  uint32_t index;
  if (AsArrayIndex(&index)) {
    // Treat array indices as numbers, so that array indices are de-duped
    // correctly even if one of them is a string and the other is a number.
    return ComputeLongHash(index);
  }
  return IsString() ? AsRawString()->Hash()
                    : ComputeLongHash(base::double_to_uint64(AsNumber()));
}

// static
bool Literal::Match(void* a, void* b) {
  Literal* x = static_cast<Literal*>(a);
  Literal* y = static_cast<Literal*>(b);
  uint32_t index_x;
  uint32_t index_y;
  if (x->AsArrayIndex(&index_x)) {
    return y->AsArrayIndex(&index_y) && index_x == index_y;
  }
  return (x->IsString() && y->IsString() &&
          x->AsRawString() == y->AsRawString()) ||
         (x->IsNumber() && y->IsNumber() && x->AsNumber() == y->AsNumber());
}

Literal* AstNodeFactory::NewNumberLiteral(double number, int pos) {
  int int_value;
  if (DoubleToSmiInteger(number, &int_value)) {
    return NewSmiLiteral(int_value, pos);
  }
  return zone_->New<Literal>(number, pos);
}

const char* CallRuntime::debug_name() {
#ifdef DEBUG
  return is_jsruntime() ? NameForNativeContextIntrinsicIndex(context_index_)
                        : function_->name;
#else
  return is_jsruntime() ? "(context function)" : function_->name;
#endif  // DEBUG
}

}  // namespace internal
}  // namespace v8
