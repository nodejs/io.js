// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/accessor-assembler.h"

#include <optional>

#include "src/ast/ast.h"
#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/ic/handler-configuration.h"
#include "src/ic/ic.h"
#include "src/ic/keyed-store-generic.h"
#include "src/ic/stub-cache.h"
#include "src/logging/counters.h"
#include "src/objects/cell.h"
#include "src/objects/dictionary.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/megadom-handler.h"
#include "src/objects/module.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-details.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

//////////////////// Private helpers.

#define LOAD_KIND(kind) \
  Int32Constant(static_cast<intptr_t>(LoadHandler::Kind::kind))
#define STORE_KIND(kind) \
  Int32Constant(static_cast<intptr_t>(StoreHandler::Kind::kind))

// Loads dataX field from the DataHandler object.
TNode<MaybeObject> AccessorAssembler::LoadHandlerDataField(
    TNode<DataHandler> handler, int data_index) {
#ifdef DEBUG
  TNode<Map> handler_map = LoadMap(handler);
  TNode<Uint16T> instance_type = LoadMapInstanceType(handler_map);
#endif
  CSA_DCHECK(this,
             Word32Or(InstanceTypeEqual(instance_type, LOAD_HANDLER_TYPE),
                      InstanceTypeEqual(instance_type, STORE_HANDLER_TYPE)));
  int offset = 0;
  int minimum_size = 0;
  switch (data_index) {
    case 1:
      offset = DataHandler::kData1Offset;
      minimum_size = DataHandler::kSizeWithData1;
      break;
    case 2:
      offset = DataHandler::kData2Offset;
      minimum_size = DataHandler::kSizeWithData2;
      break;
    case 3:
      offset = DataHandler::kData3Offset;
      minimum_size = DataHandler::kSizeWithData3;
      break;
    default:
      UNREACHABLE();
  }
  USE(minimum_size);
  CSA_DCHECK(this, UintPtrGreaterThanOrEqual(
                       LoadMapInstanceSizeInWords(handler_map),
                       IntPtrConstant(minimum_size / kTaggedSize)));
  return LoadMaybeWeakObjectField(handler, offset);
}

TNode<HeapObjectReference> AccessorAssembler::TryMonomorphicCase(
    TNode<TaggedIndex> slot, TNode<FeedbackVector> vector,
    TNode<HeapObjectReference> weak_lookup_start_object_map, Label* if_handler,
    TVariable<MaybeObject>* var_handler, Label* if_miss) {
  Comment("TryMonomorphicCase");
  DCHECK_EQ(MachineRepresentation::kTagged, var_handler->rep());

  // TODO(ishell): add helper class that hides offset computations for a series
  // of loads.
  int32_t header_size =
      FeedbackVector::kRawFeedbackSlotsOffset - kHeapObjectTag;
  // Adding |header_size| with a separate IntPtrAdd rather than passing it
  // into ElementOffsetFromIndex() allows it to be folded into a single
  // [base, index, offset] indirect memory access on x64.
  TNode<IntPtrT> offset = ElementOffsetFromIndex(slot, HOLEY_ELEMENTS);
  TNode<HeapObjectReference> feedback = CAST(Load<MaybeObject>(
      vector, IntPtrAdd(offset, IntPtrConstant(header_size))));

  // Try to quickly handle the monomorphic case without knowing for sure
  // if we have a weak reference in feedback.
  CSA_DCHECK(this,
             IsMap(GetHeapObjectAssumeWeak(weak_lookup_start_object_map)));
  GotoIfNot(TaggedEqual(feedback, weak_lookup_start_object_map), if_miss);

  TNode<MaybeObject> handler = UncheckedCast<MaybeObject>(
      Load(MachineType::AnyTagged(), vector,
           IntPtrAdd(offset, IntPtrConstant(header_size + kTaggedSize))));

  *var_handler = handler;
  Goto(if_handler);
  return feedback;
}

void AccessorAssembler::HandlePolymorphicCase(
    TNode<HeapObjectReference> weak_lookup_start_object_map,
    TNode<WeakFixedArray> feedback, Label* if_handler,
    TVariable<MaybeObject>* var_handler, Label* if_miss) {
  Comment("HandlePolymorphicCase");
  DCHECK_EQ(MachineRepresentation::kTagged, var_handler->rep());

  // Iterate {feedback} array.
  const int kEntrySize = 2;

  // Load the {feedback} array length.
  TNode<Int32T> length =
      Signed(LoadAndUntagWeakFixedArrayLengthAsUint32(feedback));
  CSA_DCHECK(this, Int32LessThanOrEqual(Int32Constant(kEntrySize), length));

  // This is a hand-crafted loop that iterates backwards and only compares
  // against zero at the end, since we already know that we will have at least a
  // single entry in the {feedback} array anyways.
  TVARIABLE(Int32T, var_index, Int32Sub(length, Int32Constant(kEntrySize)));
  Label loop(this, &var_index), loop_next(this);
  Goto(&loop);
  BIND(&loop);
  {
    TNode<IntPtrT> index = ChangePositiveInt32ToIntPtr(var_index.value());
    TNode<MaybeObject> maybe_cached_map =
        LoadWeakFixedArrayElement(feedback, index);
    CSA_DCHECK(this,
               IsMap(GetHeapObjectAssumeWeak(weak_lookup_start_object_map)));
    GotoIfNot(TaggedEqual(maybe_cached_map, weak_lookup_start_object_map),
              &loop_next);

    // Found, now call handler.
    TNode<MaybeObject> handler =
        LoadWeakFixedArrayElement(feedback, index, kTaggedSize);
    *var_handler = handler;
    Goto(if_handler);

    BIND(&loop_next);
    var_index = Int32Sub(var_index.value(), Int32Constant(kEntrySize));
    Branch(Int32GreaterThanOrEqual(var_index.value(), Int32Constant(0)), &loop,
           if_miss);
  }
}

void AccessorAssembler::TryMegaDOMCase(TNode<Object> lookup_start_object,
                                       TNode<Map> lookup_start_object_map,
                                       TVariable<MaybeObject>* var_handler,
                                       TNode<Object> vector,
                                       TNode<TaggedIndex> slot, Label* miss,
                                       ExitPoint* exit_point) {
  // Check if the receiver is a JS_API_OBJECT
  GotoIfNot(IsJSApiObjectMap(lookup_start_object_map), miss);

  // Check if receiver requires access check
  GotoIf(IsSetWord32<Map::Bits1::IsAccessCheckNeededBit>(
             LoadMapBitField(lookup_start_object_map)),
         miss);

  CSA_DCHECK(this, TaggedEqual(LoadFeedbackVectorSlot(CAST(vector), slot),
                               MegaDOMSymbolConstant()));

  // In some cases, we load the
  TNode<MegaDomHandler> handler;
  if (var_handler->IsBound()) {
    handler = CAST(var_handler->value());
  } else {
    TNode<MaybeObject> maybe_handler =
        LoadFeedbackVectorSlot(CAST(vector), slot, kTaggedSize);
    CSA_DCHECK(this, IsStrong(maybe_handler));
    handler = CAST(maybe_handler);
  }

  // Check if dom protector cell is still valid
  GotoIf(IsMegaDOMProtectorCellInvalid(), miss);

  // Load the getter
  TNode<MaybeObject> maybe_getter = LoadMegaDomHandlerAccessor(handler);
  CSA_DCHECK(this, IsWeakOrCleared(maybe_getter));
  TNode<FunctionTemplateInfo> getter =
      CAST(GetHeapObjectAssumeWeak(maybe_getter, miss));

  // Load the accessor context
  TNode<MaybeObject> maybe_context = LoadMegaDomHandlerContext(handler);
  CSA_DCHECK(this, IsWeakOrCleared(maybe_context));
  TNode<Context> context = CAST(GetHeapObjectAssumeWeak(maybe_context, miss));

  // TODO(gsathya): This builtin throws an exception on interface check fail but
  // we should miss to the runtime.
  TNode<Context> caller_context = context;
  exit_point->Return(CallBuiltin(Builtin::kCallFunctionTemplate_Generic,
                                 context, getter, Int32Constant(1),
                                 caller_context, lookup_start_object));
}

void AccessorAssembler::TryEnumeratedKeyedLoad(
    const LoadICParameters* p, TNode<Map> lookup_start_object_map,
    ExitPoint* exit_point) {
  if (!p->IsEnumeratedKeyedLoad()) return;
  Label no_enum_cache(this);
  // |p->cache_type()| comes from the outer loop's ForIn state.
  GotoIf(TaggedNotEqual(p->cache_type(), lookup_start_object_map),
         &no_enum_cache);

  // Use field index in EnumCache.
  TNode<DescriptorArray> descriptors =
      LoadMapDescriptors(lookup_start_object_map);
  TNode<EnumCache> enum_cache = LoadObjectField<EnumCache>(
      descriptors, DescriptorArray::kEnumCacheOffset);
  TNode<FixedArray> enum_keys =
      LoadObjectField<FixedArray>(enum_cache, EnumCache::kKeysOffset);
  // |p->enum_index()| comes from the outer loop's ForIn state.
  TNode<Object> key = LoadFixedArrayElement(enum_keys, p->enum_index());
  // Check if |p->name()| matches the key in enum cache. |p->name()| is the
  // "each" variable of a for-in loop, but it can be modified by debugger or
  // other bytecodes.
  GotoIf(TaggedNotEqual(key, p->name()), &no_enum_cache);
  TNode<FixedArray> enum_indices =
      LoadObjectField<FixedArray>(enum_cache, EnumCache::kIndicesOffset);
  // Check if we have enum indices available.
  GotoIf(IsEmptyFixedArray(enum_indices), &no_enum_cache);
  TNode<Int32T> field_index =
      SmiToInt32(CAST(LoadFixedArrayElement(enum_indices, p->enum_index())));

  TVARIABLE(Object, result);
  Label if_double(this, Label::kDeferred), done(this, &result);
  // Check if field is a mutable double field.
  uint32_t kIsMutableDoubleFieldMask = 1;
  GotoIf(IsSetWord32(field_index, kIsMutableDoubleFieldMask), &if_double);

  TNode<Int32T> zero = Int32Constant(0);
  {
    Label if_outofobject(this);
    // Check if field is in-object or out-of-object.
    GotoIf(Int32LessThan(field_index, zero), &if_outofobject);

    // The field is located in the {object} itself.
    {
      TNode<IntPtrT> offset = Signed(ChangeUint32ToWord(
          Int32Add(Word32Shl(field_index, Int32Constant(kTaggedSizeLog2 - 1)),
                   Int32Constant(JSObject::kHeaderSize))));
      result =
          LoadObjectField(CAST(p->receiver_and_lookup_start_object()), offset);
      Goto(&done);
    }

    // The field is located in the properties backing store of {object}.
    // The {index} is equal to the negated out of property index plus 1.
    BIND(&if_outofobject);
    {
      TNode<PropertyArray> properties = CAST(LoadFastProperties(
          CAST(p->receiver_and_lookup_start_object()), true));
      TNode<IntPtrT> offset = Signed(ChangeUint32ToWord(
          Int32Add(Word32Shl(Int32Sub(zero, field_index),
                             Int32Constant(kTaggedSizeLog2 - 1)),
                   Int32Constant(FixedArray::kHeaderSize - kTaggedSize))));
      result = LoadObjectField(properties, offset);
      Goto(&done);
    }
  }

  // The field is a Double field, either unboxed in the object on 64-bit
  // architectures, or a mutable HeapNumber.
  BIND(&if_double);
  {
    TVARIABLE(Object, field);
    Label loaded_field(this, &field), if_outofobject(this);
    field_index = Word32Sar(field_index, Int32Constant(1));
    // Check if field is in-object or out-of-object.
    GotoIf(Int32LessThan(field_index, zero), &if_outofobject);

    // The field is located in the {object} itself.
    {
      TNode<IntPtrT> offset = Signed(ChangeUint32ToWord(
          Int32Add(Word32Shl(field_index, Int32Constant(kTaggedSizeLog2)),
                   Int32Constant(JSObject::kHeaderSize))));
      field =
          LoadObjectField(CAST(p->receiver_and_lookup_start_object()), offset);
      Goto(&loaded_field);
    }

    BIND(&if_outofobject);
    {
      TNode<PropertyArray> properties = CAST(LoadFastProperties(
          CAST(p->receiver_and_lookup_start_object()), true));
      TNode<IntPtrT> offset = Signed(ChangeUint32ToWord(
          Int32Add(Word32Shl(Int32Sub(zero, field_index),
                             Int32Constant(kTaggedSizeLog2)),
                   Int32Constant(FixedArray::kHeaderSize - kTaggedSize))));
      field = LoadObjectField(properties, offset);
      Goto(&loaded_field);
    }

    BIND(&loaded_field);
    {
      // We may have transitioned in-place away from double, so check that
      // this is a HeapNumber -- otherwise the load is fine and we don't need
      // to copy anything anyway.
      Label if_not_double(this);
      GotoIf(TaggedIsSmi(field.value()), &if_not_double);

      TNode<HeapObject> double_field = CAST(field.value());
      TNode<Map> field_map = LoadMap(double_field);
      GotoIfNot(TaggedEqual(field_map, HeapNumberMapConstant()),
                &if_not_double);

      TNode<Float64T> value = LoadHeapNumberValue(double_field);
      result = AllocateHeapNumberWithValue(value);
      Goto(&done);

      BIND(&if_not_double);
      {
        result = field.value();
        Goto(&done);
      }
    }
  }

  BIND(&done);
  { exit_point->Return(result.value()); }

  BIND(&no_enum_cache);
}

void AccessorAssembler::HandleLoadICHandlerCase(
    const LazyLoadICParameters* p, TNode<MaybeObject> handler, Label* miss,
    ExitPoint* exit_point, ICMode ic_mode, OnNonExistent on_nonexistent,
    ElementSupport support_elements, LoadAccessMode access_mode) {
  Comment("have_handler");

  TVARIABLE(Object, var_holder, p->lookup_start_object());
  TVARIABLE(MaybeObject, var_smi_handler, handler);

  Label if_smi_handler(this, {&var_holder, &var_smi_handler});
  Label try_proto_handler(this, Label::kDeferred),
      call_code_handler(this, Label::kDeferred),
      call_getter(this, Label::kDeferred);

  Branch(TaggedIsSmi(handler), &if_smi_handler, &try_proto_handler);

  BIND(&try_proto_handler);
  {
    GotoIf(IsWeakOrCleared(handler), &call_getter);
    GotoIf(IsCode(CAST(handler)), &call_code_handler);
    HandleLoadICProtoHandler(p, CAST(handler), &var_holder, &var_smi_handler,
                             &if_smi_handler, miss, exit_point, ic_mode,
                             access_mode);
  }

  // |handler| is a Smi, encoding what to do. See SmiHandler methods
  // for the encoding format.
  BIND(&if_smi_handler);
  {
    HandleLoadICSmiHandlerCase(
        p, var_holder.value(), CAST(var_smi_handler.value()), handler, miss,
        exit_point, ic_mode, on_nonexistent, support_elements, access_mode);
  }

  BIND(&call_getter);
  {
    if (access_mode == LoadAccessMode::kHas) {
      exit_point->Return(TrueConstant());
    } else {
      TNode<HeapObject> strong_handler = GetHeapObjectAssumeWeak(handler, miss);
      TNode<JSFunction> getter =
          CAST(LoadAccessorPairGetter(CAST(strong_handler)));

      ConvertReceiverMode mode =
          p->lookup_start_object() == p->receiver()
              // LoadIC case: the receiver is definitely not null or undefined.
              ? ConvertReceiverMode::kNotNullOrUndefined
              // LoadSuperIC case: the receiver might be anything.
              : ConvertReceiverMode::kAny;
      exit_point->Return(
          CallFunction(p->context(), getter, mode, p->receiver()));
    }
  }

  BIND(&call_code_handler);
  {
    TNode<Code> code_handler = CAST(handler);
    exit_point->ReturnCallStub(LoadWithVectorDescriptor{}, code_handler,
                               p->context(), p->lookup_start_object(),
                               p->name(), p->slot(), p->vector());
  }
}

void AccessorAssembler::HandleLoadCallbackProperty(
    const LazyLoadICParameters* p, TNode<JSObject> holder,
    TNode<Word32T> handler_word, ExitPoint* exit_point) {
  Comment("native_data_property_load");
  TNode<IntPtrT> descriptor =
      Signed(DecodeWordFromWord32<LoadHandler::DescriptorBits>(handler_word));

  TNode<AccessorInfo> accessor_info =
      CAST(LoadDescriptorValue(LoadMap(holder), descriptor));

  exit_point->ReturnCallBuiltin(Builtin::kCallApiGetter, p->context(),
                                p->receiver(), holder, accessor_info);
}

void AccessorAssembler::HandleLoadAccessor(
    const LazyLoadICParameters* p,
    TNode<FunctionTemplateInfo> function_template_info,
    TNode<Word32T> handler_word, TNode<DataHandler> handler,
    TNode<Uint32T> handler_kind, ExitPoint* exit_point) {
  Comment("api_getter");
  // Context is stored either in data2 or data3 field depending on whether
  // the access check is enabled for this handler or not.
  TNode<MaybeObject> maybe_context = Select<MaybeObject>(
      IsSetWord32<LoadHandler::DoAccessCheckOnLookupStartObjectBits>(
          handler_word),
      [=, this] { return LoadHandlerDataField(handler, 3); },
      [=, this] { return LoadHandlerDataField(handler, 2); });

  CSA_DCHECK(this, IsWeakOrCleared(maybe_context));
  CSA_CHECK(this, IsNotCleared(maybe_context));
  TNode<HeapObject> context = GetHeapObjectAssumeWeak(maybe_context);

  TVARIABLE(HeapObject, api_holder, CAST(p->lookup_start_object()));
  Label load(this);
  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kApiGetter)), &load);

  CSA_DCHECK(this,
             Word32Equal(handler_kind, LOAD_KIND(kApiGetterHolderIsPrototype)));

  api_holder = LoadMapPrototype(LoadMap(CAST(p->lookup_start_object())));
  Goto(&load);

  BIND(&load);
  {
    TNode<Int32T> argc = Int32Constant(0);
    TNode<Context> caller_context = p->context();
    exit_point->Return(CallBuiltin(Builtin::kCallApiCallbackGeneric, context,
                                   argc, caller_context, function_template_info,
                                   api_holder.value(), p->receiver()));
  }
}

void AccessorAssembler::HandleLoadField(TNode<JSObject> holder,
                                        TNode<Word32T> handler_word,
                                        TVariable<Float64T>* var_double_value,
                                        Label* rebox_double, Label* miss,
                                        ExitPoint* exit_point) {
  Comment("LoadField");
  TNode<IntPtrT> index =
      Signed(DecodeWordFromWord32<LoadHandler::FieldIndexBits>(handler_word));
  TNode<IntPtrT> offset = IntPtrMul(index, IntPtrConstant(kTaggedSize));

  TNode<BoolT> is_inobject =
      IsSetWord32<LoadHandler::IsInobjectBits>(handler_word);
  TNode<HeapObject> property_storage = Select<HeapObject>(
      is_inobject, [&]() { return holder; },
      [&]() { return LoadFastProperties(holder, true); });

  Label is_double(this);
  TNode<Object> value = LoadObjectField(property_storage, offset);
  GotoIf(IsSetWord32<LoadHandler::IsDoubleBits>(handler_word), &is_double);
  exit_point->Return(value);

  BIND(&is_double);
  // This is not an "old" Smi value from before a Smi->Double transition.
  // Rather, it's possible that since the last update of this IC, the Double
  // field transitioned to a Tagged field, and was then assigned a Smi.
  GotoIf(TaggedIsSmi(value), miss);
  GotoIfNot(IsHeapNumber(CAST(value)), miss);
  *var_double_value = LoadHeapNumberValue(CAST(value));
  Goto(rebox_double);
}

#if V8_ENABLE_WEBASSEMBLY

void AccessorAssembler::HandleLoadWasmField(
    TNode<WasmObject> holder, TNode<Int32T> wasm_value_type,
    TNode<IntPtrT> field_offset, TVariable<Float64T>* var_double_value,
    Label* rebox_double, ExitPoint* exit_point) {
  Label type_I8(this), type_I16(this), type_I32(this), type_U32(this),
      type_I64(this), type_U64(this), type_F32(this), type_F64(this),
      type_Ref(this), unsupported_type(this, Label::kDeferred),
      unexpected_type(this, Label::kDeferred);
  Label* wasm_value_type_labels[] = {
      &type_I8,  &type_I16, &type_I32, &type_U32, &type_I64,
      &type_F32, &type_F64, &type_Ref, &type_Ref, &unsupported_type};
  int32_t wasm_value_types[] = {
      static_cast<int32_t>(WasmValueType::kI8),
      static_cast<int32_t>(WasmValueType::kI16),
      static_cast<int32_t>(WasmValueType::kI32),
      static_cast<int32_t>(WasmValueType::kU32),
      static_cast<int32_t>(WasmValueType::kI64),
      static_cast<int32_t>(WasmValueType::kF32),
      static_cast<int32_t>(WasmValueType::kF64),
      static_cast<int32_t>(WasmValueType::kRef),
      static_cast<int32_t>(WasmValueType::kRefNull),
      // TODO(v8:11804): support the following value types.
      static_cast<int32_t>(WasmValueType::kS128)};
  const size_t kWasmValueTypeCount =
      static_cast<size_t>(WasmValueType::kNumTypes);
  DCHECK_EQ(kWasmValueTypeCount, arraysize(wasm_value_types));
  DCHECK_EQ(kWasmValueTypeCount, arraysize(wasm_value_type_labels));

  Switch(wasm_value_type, &unexpected_type, wasm_value_types,
         wasm_value_type_labels, kWasmValueTypeCount);
  BIND(&type_I8);
  {
    Comment("type_I8");
    TNode<Int32T> value = LoadObjectField<Int8T>(holder, field_offset);
    exit_point->Return(SmiFromInt32(value));
  }
  BIND(&type_I16);
  {
    Comment("type_I16");
    TNode<Int32T> value = LoadObjectField<Int16T>(holder, field_offset);
    exit_point->Return(SmiFromInt32(value));
  }
  BIND(&type_I32);
  {
    Comment("type_I32");
    TNode<Int32T> value = LoadObjectField<Int32T>(holder, field_offset);
    exit_point->Return(ChangeInt32ToTagged(value));
  }
  BIND(&type_U32);
  {
    Comment("type_U32");
    TNode<Uint32T> value = LoadObjectField<Uint32T>(holder, field_offset);
    exit_point->Return(ChangeUint32ToTagged(value));
  }
  BIND(&type_I64);
  {
    Comment("type_I64");
    TNode<RawPtrT> data_pointer =
        ReinterpretCast<RawPtrT>(BitcastTaggedToWord(holder));
    TNode<BigInt> value = LoadFixedBigInt64ArrayElementAsTagged(
        data_pointer,
        Signed(IntPtrSub(field_offset, IntPtrConstant(kHeapObjectTag))));
    exit_point->Return(value);
  }
  BIND(&type_F32);
  {
    Comment("type_F32");
    TNode<Float32T> value = LoadObjectField<Float32T>(holder, field_offset);
    *var_double_value = ChangeFloat32ToFloat64(value);
    Goto(rebox_double);
  }
  BIND(&type_F64);
  {
    Comment("type_F64");
    TNode<Float64T> value = LoadObjectField<Float64T>(holder, field_offset);
    *var_double_value = value;
    Goto(rebox_double);
  }
  BIND(&type_Ref);
  {
    Comment("type_Ref");
    TNode<Object> value = LoadObjectField(holder, field_offset);
    exit_point->Return(value);
  }
  BIND(&unsupported_type);
  {
    Print("Not supported Wasm field type");
    Unreachable();
  }
  BIND(&unexpected_type);
  { Unreachable(); }
}

void AccessorAssembler::HandleLoadWasmField(
    TNode<WasmObject> holder, TNode<Word32T> handler_word,
    TVariable<Float64T>* var_double_value, Label* rebox_double,
    ExitPoint* exit_point) {
  Comment("LoadWasmField");
  TNode<Int32T> wasm_value_type =
      Signed(DecodeWord32<LoadHandler::WasmFieldTypeBits>(handler_word));
  TNode<IntPtrT> field_offset = Signed(
      DecodeWordFromWord32<LoadHandler::WasmFieldOffsetBits>(handler_word));

  HandleLoadWasmField(holder, wasm_value_type, field_offset, var_double_value,
                      rebox_double, exit_point);
}

#endif  // V8_ENABLE_WEBASSEMBLY

TNode<Object> AccessorAssembler::LoadDescriptorValue(
    TNode<Map> map, TNode<IntPtrT> descriptor_entry) {
  return CAST(LoadDescriptorValueOrFieldType(map, descriptor_entry));
}

TNode<MaybeObject> AccessorAssembler::LoadDescriptorValueOrFieldType(
    TNode<Map> map, TNode<IntPtrT> descriptor_entry) {
  TNode<DescriptorArray> descriptors = LoadMapDescriptors(map);
  return LoadFieldTypeByDescriptorEntry(descriptors, descriptor_entry);
}

void AccessorAssembler::HandleLoadICSmiHandlerCase(
    const LazyLoadICParameters* p, TNode<Object> holder, TNode<Smi> smi_handler,
    TNode<MaybeObject> handler, Label* miss, ExitPoint* exit_point,
    ICMode ic_mode, OnNonExistent on_nonexistent,
    ElementSupport support_elements, LoadAccessMode access_mode) {
  TVARIABLE(Float64T, var_double_value);
  Label rebox_double(this, &var_double_value);

  TNode<Int32T> handler_word = SmiToInt32(smi_handler);
  TNode<Uint32T> handler_kind =
      DecodeWord32<LoadHandler::KindBits>(handler_word);

  if (support_elements == kSupportElements) {
    Label if_element(this), if_indexed_string(this), if_property(this),
        if_hole(this), unimplemented_elements_kind(this),
        if_oob(this, Label::kDeferred), try_string_to_array_index(this),
        emit_element_load(this);
    TVARIABLE(IntPtrT, var_intptr_index);
    GotoIf(Word32Equal(handler_kind, LOAD_KIND(kElement)), &if_element);

    if (access_mode == LoadAccessMode::kHas) {
      CSA_DCHECK(this, Word32NotEqual(handler_kind, LOAD_KIND(kIndexedString)));
      Goto(&if_property);
    } else {
      Branch(Word32Equal(handler_kind, LOAD_KIND(kIndexedString)),
             &if_indexed_string, &if_property);
    }

    BIND(&if_element);
    {
      Comment("element_load");
      // TODO(ishell): implement
      CSA_DCHECK(this,
                 IsClearWord32<LoadHandler::IsWasmArrayBits>(handler_word));
      TVARIABLE(Int32T, var_instance_type);
      TNode<IntPtrT> intptr_index = TryToIntptr(
          p->name(), &try_string_to_array_index, &var_instance_type);
      var_intptr_index = intptr_index;
      Goto(&emit_element_load);

      BIND(&try_string_to_array_index);
      {
        GotoIfNot(IsStringInstanceType(var_instance_type.value()), miss);

        TNode<ExternalReference> function = ExternalConstant(
            ExternalReference::string_to_array_index_function());
        TNode<Int32T> result = UncheckedCast<Int32T>(
            CallCFunction(function, MachineType::Int32(),
                          std::make_pair(MachineType::AnyTagged(), p->name())));
        GotoIf(Word32Equal(Int32Constant(-1), result), miss);
        CSA_DCHECK(this, Int32GreaterThanOrEqual(result, Int32Constant(0)));
        var_intptr_index = ChangeInt32ToIntPtr(result);

        Goto(&emit_element_load);
      }

      BIND(&emit_element_load);
      {
        TNode<BoolT> is_jsarray_condition =
            IsSetWord32<LoadHandler::IsJsArrayBits>(handler_word);
        TNode<Uint32T> elements_kind =
            DecodeWord32<LoadHandler::ElementsKindBits>(handler_word);
        EmitElementLoad(CAST(holder), elements_kind, var_intptr_index.value(),
                        is_jsarray_condition, &if_hole, &rebox_double,
                        &var_double_value, &unimplemented_elements_kind,
                        &if_oob, miss, exit_point, access_mode);
      }
    }

    BIND(&unimplemented_elements_kind);
    {
      // Smi handlers should only be installed for supported elements kinds.
      // Crash if we get here.
      DebugBreak();
      Goto(miss);
    }

    BIND(&if_oob);
    {
      Comment("out of bounds elements access");
      Label return_undefined(this);

      // Check if we're allowed to handle OOB accesses.
      TNode<BoolT> allow_out_of_bounds =
          IsSetWord32<LoadHandler::AllowOutOfBoundsBits>(handler_word);
      GotoIfNot(allow_out_of_bounds, miss);

      // Negative indices aren't valid array indices (according to
      // the ECMAScript specification), and are stored as properties
      // in V8, not elements. So we cannot handle them here, except
      // in case of typed arrays, where integer indexed properties
      // aren't looked up in the prototype chain.
      GotoIf(IsJSTypedArray(CAST(holder)), &return_undefined);
      if (Is64()) {
        GotoIfNot(
            UintPtrLessThanOrEqual(var_intptr_index.value(),
                                   IntPtrConstant(JSObject::kMaxElementIndex)),
            miss);
      } else {
        GotoIf(IntPtrLessThan(var_intptr_index.value(), IntPtrConstant(0)),
               miss);
      }

      // For all other receivers we need to check that the prototype chain
      // doesn't contain any elements.
      BranchIfPrototypesHaveNoElements(LoadMap(CAST(holder)), &return_undefined,
                                       miss);

      BIND(&return_undefined);
      exit_point->Return(access_mode == LoadAccessMode::kHas
                             ? TNode<Object>(FalseConstant())
                             : TNode<Object>(UndefinedConstant()));
    }

    BIND(&if_hole);
    {
      Comment("read hole and convert to undefined");

      GotoIfNot(IsSetWord32<LoadHandler::AllowHandlingHole>(handler_word),
                miss);
      GotoIf(IsNoElementsProtectorCellInvalid(), miss);
      exit_point->Return(access_mode == LoadAccessMode::kHas
                             ? TNode<Object>(FalseConstant())
                             : TNode<Object>(UndefinedConstant()));
    }

    if (access_mode != LoadAccessMode::kHas) {
      BIND(&if_indexed_string);
      {
        Label if_oob_string(this, Label::kDeferred);

        Comment("indexed string");
        TNode<String> string_holder = CAST(holder);
        TNode<IntPtrT> index = TryToIntptr(p->name(), miss);
        TNode<UintPtrT> length =
            Unsigned(LoadStringLengthAsWord(string_holder));
        GotoIf(UintPtrGreaterThanOrEqual(index, length), &if_oob_string);
        TNode<Int32T> code = StringCharCodeAt(string_holder, Unsigned(index));
        TNode<String> result = StringFromSingleCharCode(code);
        Return(result);

        BIND(&if_oob_string);
        if (Is64()) {
          // Indices >= 4294967295 are stored as named properties; handle them
          // in the runtime.
          GotoIfNot(UintPtrLessThanOrEqual(
                        index, IntPtrConstant(JSObject::kMaxElementIndex)),
                    miss);
        } else {
          GotoIf(IntPtrLessThan(index, IntPtrConstant(0)), miss);
        }
        TNode<BoolT> allow_out_of_bounds =
            IsSetWord32<LoadHandler::AllowOutOfBoundsBits>(handler_word);
        GotoIfNot(allow_out_of_bounds, miss);
        GotoIf(IsNoElementsProtectorCellInvalid(), miss);
        Return(UndefinedConstant());
      }
    }

    BIND(&if_property);
    Comment("property_load");
  }

  if (access_mode == LoadAccessMode::kHas) {
    HandleLoadICSmiHandlerHasNamedCase(p, holder, handler_kind, miss,
                                       exit_point, ic_mode);
  } else {
    HandleLoadICSmiHandlerLoadNamedCase(
        p, holder, handler_kind, handler_word, &rebox_double, &var_double_value,
        handler, miss, exit_point, ic_mode, on_nonexistent, support_elements);
  }
}

void AccessorAssembler::HandleLoadICSmiHandlerLoadNamedCase(
    const LazyLoadICParameters* p, TNode<Object> holder,
    TNode<Uint32T> handler_kind, TNode<Word32T> handler_word,
    Label* rebox_double, TVariable<Float64T>* var_double_value,
    TNode<MaybeObject> handler, Label* miss, ExitPoint* exit_point,
    ICMode ic_mode, OnNonExistent on_nonexistent,
    ElementSupport support_elements) {
  Label constant(this), field(this), normal(this, Label::kDeferred),
      slow(this, Label::kDeferred), interceptor(this, Label::kDeferred),
      nonexistent(this), accessor(this, Label::kDeferred),
      global(this, Label::kDeferred), module_export(this, Label::kDeferred),
      proxy(this, Label::kDeferred),
      native_data_property(this, Label::kDeferred),
      api_getter(this, Label::kDeferred);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kField)), &field);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kConstantFromPrototype)),
         &constant);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kNonExistent)), &nonexistent);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kNormal)), &normal);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kAccessorFromPrototype)),
         &accessor);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kNativeDataProperty)),
         &native_data_property);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kApiGetter)), &api_getter);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kApiGetterHolderIsPrototype)),
         &api_getter);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kGlobal)), &global);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kSlow)), &slow);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kProxy)), &proxy);

  Branch(Word32Equal(handler_kind, LOAD_KIND(kModuleExport)), &module_export,
         &interceptor);

  BIND(&field);
  {
#if V8_ENABLE_WEBASSEMBLY
    Label is_wasm_field(this);
    GotoIf(IsSetWord32<LoadHandler::IsWasmStructBits>(handler_word),
           &is_wasm_field);
#else
    CSA_DCHECK(this,
               IsClearWord32<LoadHandler::IsWasmStructBits>(handler_word));
#endif  // V8_ENABLE_WEBASSEMBLY

    HandleLoadField(CAST(holder), handler_word, var_double_value, rebox_double,
                    miss, exit_point);

#if V8_ENABLE_WEBASSEMBLY
    BIND(&is_wasm_field);
    HandleLoadWasmField(CAST(holder), handler_word, var_double_value,
                        rebox_double, exit_point);
#endif  // V8_ENABLE_WEBASSEMBLY
  }

  BIND(&nonexistent);
  // This is a handler for a load of a non-existent value.
  if (on_nonexistent == OnNonExistent::kThrowReferenceError) {
    exit_point->ReturnCallRuntime(Runtime::kThrowReferenceError, p->context(),
                                  p->name());
  } else {
    DCHECK_EQ(OnNonExistent::kReturnUndefined, on_nonexistent);
    exit_point->Return(UndefinedConstant());
  }

  BIND(&constant);
  {
    Comment("constant_load");
    exit_point->Return(holder);
  }

  BIND(&normal);
  {
    Comment("load_normal");
    TNode<PropertyDictionary> properties =
        CAST(LoadSlowProperties(CAST(holder)));
    TVARIABLE(IntPtrT, var_name_index);
    Label found(this, &var_name_index);
    NameDictionaryLookup<PropertyDictionary>(properties, CAST(p->name()),
                                             &found, &var_name_index, miss);
    BIND(&found);
    {
      TVARIABLE(Uint32T, var_details);
      TVARIABLE(Object, var_value);
      LoadPropertyFromDictionary<PropertyDictionary>(
          properties, var_name_index.value(), &var_details, &var_value);
      TNode<Object> value = CallGetterIfAccessor(
          var_value.value(), CAST(holder), var_details.value(), p->context(),
          p->receiver(), p->name(), miss);
      exit_point->Return(value);
    }
  }

  BIND(&accessor);
  {
    Comment("accessor_load");
    // The "holder" slot (data1) in the from-prototype LoadHandler is instead
    // directly the getter function.
    TNode<HeapObject> getter = CAST(holder);
    CSA_DCHECK(this, IsCallable(getter));

    exit_point->Return(Call(p->context(), getter, p->receiver()));
  }

  BIND(&native_data_property);
  HandleLoadCallbackProperty(p, CAST(holder), handler_word, exit_point);

  BIND(&api_getter);
  {
    if (p->receiver() != p->lookup_start_object()) {
      // Force super ICs using API getters into the slow path, so that we get
      // the correct receiver checks.
      Goto(&slow);
    } else {
      HandleLoadAccessor(p, CAST(holder), handler_word, CAST(handler),
                         handler_kind, exit_point);
    }
  }

  BIND(&proxy);
  {
    // TODO(mythria): LoadGlobals don't use this path. LoadGlobals need special
    // handling with proxies which is currently not supported by builtins. So
    // for such cases, we should install a slow path and never reach here. Fix
    // it to not generate this for LoadGlobals.
    CSA_DCHECK(this,
               WordNotEqual(IntPtrConstant(static_cast<int>(on_nonexistent)),
                            IntPtrConstant(static_cast<int>(
                                OnNonExistent::kThrowReferenceError))));
    TVARIABLE(IntPtrT, var_index);
    TVARIABLE(Name, var_unique);

    Label if_index(this), if_unique_name(this),
        to_name_failed(this, Label::kDeferred);

    if (support_elements == kSupportElements) {
      DCHECK_NE(on_nonexistent, OnNonExistent::kThrowReferenceError);

      TryToName(p->name(), &if_index, &var_index, &if_unique_name, &var_unique,
                &to_name_failed);

      BIND(&if_unique_name);
      exit_point->ReturnCallBuiltin(Builtin::kProxyGetProperty, p->context(),
                                    holder, var_unique.value(), p->receiver(),
                                    SmiConstant(on_nonexistent));

      BIND(&if_index);
      // TODO(mslekova): introduce TryToName that doesn't try to compute
      // the intptr index value
      Goto(&to_name_failed);

      BIND(&to_name_failed);
      // TODO(duongn): use GetPropertyWithReceiver builtin once
      // |lookup_element_in_holder| supports elements.
      exit_point->ReturnCallRuntime(Runtime::kGetPropertyWithReceiver,
                                    p->context(), holder, p->name(),
                                    p->receiver(), SmiConstant(on_nonexistent));
    } else {
      exit_point->ReturnCallBuiltin(Builtin::kProxyGetProperty, p->context(),
                                    holder, p->name(), p->receiver(),
                                    SmiConstant(on_nonexistent));
    }
  }

  BIND(&global);
  {
    CSA_DCHECK(this, IsPropertyCell(CAST(holder)));
    // Ensure the property cell doesn't contain the hole.
    TNode<Object> value =
        LoadObjectField(CAST(holder), PropertyCell::kValueOffset);
    TNode<Uint32T> details = Unsigned(LoadAndUntagToWord32ObjectField(
        CAST(holder), PropertyCell::kPropertyDetailsRawOffset));
    GotoIf(IsPropertyCellHole(value), miss);

    exit_point->Return(CallGetterIfAccessor(value, CAST(holder), details,
                                            p->context(), p->receiver(),
                                            p->name(), miss));
  }

  BIND(&interceptor);
  {
    Comment("load_interceptor");
    exit_point->ReturnCallRuntime(Runtime::kLoadPropertyWithInterceptor,
                                  p->context(), p->name(), p->receiver(),
                                  holder, p->slot(), p->vector());
  }
  BIND(&slow);
  {
    Comment("load_slow");
    if (ic_mode == ICMode::kGlobalIC) {
      exit_point->ReturnCallRuntime(Runtime::kLoadGlobalIC_Slow, p->context(),
                                    p->name(), p->slot(), p->vector());

    } else {
      exit_point->ReturnCallRuntime(Runtime::kGetProperty, p->context(),
                                    p->lookup_start_object(), p->name(),
                                    p->receiver());
    }
  }

  BIND(&module_export);
  {
    Comment("module export");
    TNode<UintPtrT> index =
        DecodeWordFromWord32<LoadHandler::ExportsIndexBits>(handler_word);
    TNode<Module> module =
        LoadObjectField<Module>(CAST(holder), JSModuleNamespace::kModuleOffset);
    TNode<ObjectHashTable> exports =
        LoadObjectField<ObjectHashTable>(module, Module::kExportsOffset);
    TNode<Cell> cell = CAST(LoadFixedArrayElement(exports, index));
    // The handler is only installed for exports that exist.
    TNode<Object> value = LoadCellValue(cell);
    Label is_the_hole(this, Label::kDeferred);
    GotoIf(IsTheHole(value), &is_the_hole);
    exit_point->Return(value);

    BIND(&is_the_hole);
    {
      TNode<Smi> message = SmiConstant(MessageTemplate::kNotDefined);
      exit_point->ReturnCallRuntime(Runtime::kThrowReferenceError, p->context(),
                                    message, p->name());
    }
  }

  BIND(rebox_double);
  exit_point->Return(AllocateHeapNumberWithValue(var_double_value->value()));
}

void AccessorAssembler::HandleLoadICSmiHandlerHasNamedCase(
    const LazyLoadICParameters* p, TNode<Object> holder,
    TNode<Uint32T> handler_kind, Label* miss, ExitPoint* exit_point,
    ICMode ic_mode) {
  Label return_true(this), return_false(this), return_lookup(this),
      normal(this), global(this), slow(this);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kField)), &return_true);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kConstantFromPrototype)),
         &return_true);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kNonExistent)), &return_false);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kNormal)), &normal);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kAccessorFromPrototype)),
         &return_true);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kNativeDataProperty)),
         &return_true);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kApiGetter)), &return_true);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kApiGetterHolderIsPrototype)),
         &return_true);

  GotoIf(Word32Equal(handler_kind, LOAD_KIND(kSlow)), &slow);

  Branch(Word32Equal(handler_kind, LOAD_KIND(kGlobal)), &global,
         &return_lookup);

  BIND(&return_true);
  exit_point->Return(TrueConstant());

  BIND(&return_false);
  exit_point->Return(FalseConstant());

  BIND(&return_lookup);
  {
    CSA_DCHECK(this,
               Word32Or(Word32Equal(handler_kind, LOAD_KIND(kInterceptor)),
                        Word32Or(Word32Equal(handler_kind, LOAD_KIND(kProxy)),
                                 Word32Equal(handler_kind,
                                             LOAD_KIND(kModuleExport)))));
    exit_point->ReturnCallBuiltin(Builtin::kHasProperty, p->context(),
                                  p->receiver(), p->name());
  }

  BIND(&normal);
  {
    Comment("has_normal");
    TNode<PropertyDictionary> properties =
        CAST(LoadSlowProperties(CAST(holder)));
    TVARIABLE(IntPtrT, var_name_index);
    Label found(this);
    NameDictionaryLookup<PropertyDictionary>(properties, CAST(p->name()),
                                             &found, &var_name_index, miss);

    BIND(&found);
    exit_point->Return(TrueConstant());
  }

  BIND(&global);
  {
    CSA_DCHECK(this, IsPropertyCell(CAST(holder)));
    // Ensure the property cell doesn't contain the hole.
    TNode<Object> value =
        LoadObjectField(CAST(holder), PropertyCell::kValueOffset);
    GotoIf(IsPropertyCellHole(value), miss);

    exit_point->Return(TrueConstant());
  }

  BIND(&slow);
  {
    Comment("load_slow");
    if (ic_mode == ICMode::kGlobalIC) {
      exit_point->ReturnCallRuntime(Runtime::kLoadGlobalIC_Slow, p->context(),
                                    p->name(), p->slot(), p->vector());
    } else {
      exit_point->ReturnCallRuntime(Runtime::kHasProperty, p->context(),
                                    p->receiver(), p->name());
    }
  }
}

// Performs actions common to both load and store handlers:
// 1. Checks prototype validity cell.
// 2. If |on_code_handler| is provided, then it checks if the sub handler is
//    a smi or code and if it's a code then it calls |on_code_handler| to
//    generate a code that handles Code handlers.
//    If |on_code_handler| is not provided, then only smi sub handler are
//    expected.
// 3. Does access check on lookup start object if
//    ICHandler::DoAccessCheckOnLookupStartObjectBits bit is set in the smi
//    handler.
// 4. Does dictionary lookup on receiver if
//    ICHandler::LookupOnLookupStartObjectBits bit is set in the smi handler. If
//    |on_found_on_lookup_start_object| is provided then it calls it to
//    generate a code that handles the "found on receiver case" or just misses
//    if the |on_found_on_lookup_start_object| is not provided.
// 5. Falls through in a case of a smi handler which is returned from this
//    function (tagged!).
// TODO(ishell): Remove templatezation once we move common bits from
// Load/StoreHandler to the base class.
template <typename ICHandler, typename ICParameters>
TNode<Object> AccessorAssembler::HandleProtoHandler(
    const ICParameters* p, TNode<DataHandler> handler,
    const OnCodeHandler& on_code_handler,
    const OnFoundOnLookupStartObject& on_found_on_lookup_start_object,
    Label* miss, ICMode ic_mode) {
  //
  // Check prototype validity cell.
  //
  {
    TNode<Object> maybe_validity_cell =
        LoadObjectField(handler, ICHandler::kValidityCellOffset);
    CheckPrototypeValidityCell(maybe_validity_cell, miss);
  }

  //
  // Check smi handler bits.
  //
  {
    TNode<Object> smi_or_code_handler =
        LoadObjectField(handler, ICHandler::kSmiHandlerOffset);
    if (on_code_handler) {
      Label if_smi_handler(this);
      GotoIf(TaggedIsSmi(smi_or_code_handler), &if_smi_handler);
      TNode<Code> code = CAST(smi_or_code_handler);
      on_code_handler(code);

      BIND(&if_smi_handler);
    }
    TNode<IntPtrT> handler_flags = SmiUntag(CAST(smi_or_code_handler));

    // Lookup on receiver and access checks are not necessary for global ICs
    // because in the former case the validity cell check guards modifications
    // of the global object and the latter is not applicable to the global
    // object.
    int mask = ICHandler::LookupOnLookupStartObjectBits::kMask |
               ICHandler::DoAccessCheckOnLookupStartObjectBits::kMask;
    if (ic_mode == ICMode::kGlobalIC) {
      CSA_DCHECK(this, IsClearWord(handler_flags, mask));
    } else {
      DCHECK_EQ(ICMode::kNonGlobalIC, ic_mode);

      Label done(this), if_do_access_check(this),
          if_lookup_on_lookup_start_object(this);
      GotoIf(IsClearWord(handler_flags, mask), &done);
      // Only one of the bits can be set at a time.
      CSA_DCHECK(this,
                 WordNotEqual(WordAnd(handler_flags, IntPtrConstant(mask)),
                              IntPtrConstant(mask)));
      Branch(
          IsSetWord<typename ICHandler::DoAccessCheckOnLookupStartObjectBits>(
              handler_flags),
          &if_do_access_check, &if_lookup_on_lookup_start_object);

      BIND(&if_do_access_check);
      {
        TNode<MaybeObject> data2 = LoadHandlerDataField(handler, 2);
        CSA_DCHECK(this, IsWeakOrCleared(data2));
        TNode<Context> expected_native_context =
            CAST(GetHeapObjectAssumeWeak(data2, miss));
        EmitAccessCheck(expected_native_context, p->context(),
                        p->lookup_start_object(), &done, miss);
      }

      BIND(&if_lookup_on_lookup_start_object);
      {
        // Dictionary lookup on lookup start object is not necessary for
        // Load/StoreGlobalIC (which is the only case when the
        // lookup_start_object can be a JSGlobalObject) because prototype
        // validity cell check already guards modifications of the global
        // object.
        CSA_DCHECK(this,
                   Word32BinaryNot(HasInstanceType(
                       CAST(p->lookup_start_object()), JS_GLOBAL_OBJECT_TYPE)));

        TNode<PropertyDictionary> properties =
            CAST(LoadSlowProperties(CAST(p->lookup_start_object())));
        TVARIABLE(IntPtrT, var_name_index);
        Label found(this, &var_name_index);
        NameDictionaryLookup<PropertyDictionary>(
            properties, CAST(p->name()), &found, &var_name_index, &done);
        BIND(&found);
        {
          if (on_found_on_lookup_start_object) {
            on_found_on_lookup_start_object(properties, var_name_index.value());
          } else {
            Goto(miss);
          }
        }
      }

      BIND(&done);
    }
    return smi_or_code_handler;
  }
}

void AccessorAssembler::HandleLoadICProtoHandler(
    const LazyLoadICParameters* p, TNode<DataHandler> handler,
    TVariable<Object>* var_holder, TVariable<MaybeObject>* var_smi_handler,
    Label* if_smi_handler, Label* miss, ExitPoint* exit_point, ICMode ic_mode,
    LoadAccessMode access_mode) {
  TNode<Smi> smi_handler = CAST(HandleProtoHandler<LoadHandler>(
      p, handler,
      // Code sub-handlers are not expected in LoadICs, so no |on_code_handler|.
      nullptr,
      // on_found_on_lookup_start_object
      [=, this](TNode<PropertyDictionary> properties,
                TNode<IntPtrT> name_index) {
        if (access_mode == LoadAccessMode::kHas) {
          exit_point->Return(TrueConstant());
        } else {
          TVARIABLE(Uint32T, var_details);
          TVARIABLE(Object, var_value);
          LoadPropertyFromDictionary<PropertyDictionary>(
              properties, name_index, &var_details, &var_value);
          TNode<Object> value = CallGetterIfAccessor(
              var_value.value(), CAST(var_holder->value()), var_details.value(),
              p->context(), p->receiver(), p->name(), miss);
          exit_point->Return(value);
        }
      },
      miss, ic_mode));

  TNode<MaybeObject> maybe_holder_or_constant =
      LoadHandlerDataField(handler, 1);

  Label load_from_cached_holder(this), is_smi(this), done(this);

  GotoIf(TaggedIsSmi(maybe_holder_or_constant), &is_smi);
  Branch(TaggedEqual(maybe_holder_or_constant, NullConstant()), &done,
         &load_from_cached_holder);

  BIND(&is_smi);
  {
    // If the "maybe_holder_or_constant" in the handler is a smi, then it's
    // guaranteed that it's not a holder object, but a constant value.
    CSA_DCHECK(this, Word32Equal(DecodeWord32<LoadHandler::KindBits>(
                                     SmiToInt32(smi_handler)),
                                 LOAD_KIND(kConstantFromPrototype)));
    if (access_mode == LoadAccessMode::kHas) {
      exit_point->Return(TrueConstant());
    } else {
      exit_point->Return(CAST(maybe_holder_or_constant));
    }
  }

  BIND(&load_from_cached_holder);
  {
    // For regular holders, having passed the receiver map check and
    // the validity cell check implies that |holder| is
    // alive. However, for global object receivers, |maybe_holder| may
    // be cleared.
    CSA_DCHECK(this, IsWeakOrCleared(maybe_holder_or_constant));
    TNode<HeapObject> holder =
        GetHeapObjectAssumeWeak(maybe_holder_or_constant, miss);
    *var_holder = holder;
    Goto(&done);
  }

  BIND(&done);
  {
    *var_smi_handler = smi_handler;
    Goto(if_smi_handler);
  }
}

void AccessorAssembler::EmitAccessCheck(TNode<Context> expected_native_context,
                                        TNode<Context> context,
                                        TNode<Object> receiver,
                                        Label* can_access, Label* miss) {
  CSA_DCHECK(this, IsNativeContext(expected_native_context));

  TNode<NativeContext> native_context = LoadNativeContext(context);
  GotoIf(TaggedEqual(expected_native_context, native_context), can_access);
  // If the receiver is not a JSGlobalProxy then we miss.
  GotoIf(TaggedIsSmi(receiver), miss);
  GotoIfNot(IsJSGlobalProxy(CAST(receiver)), miss);
  // For JSGlobalProxy receiver try to compare security tokens of current
  // and expected native contexts.
  TNode<Object> expected_token = LoadContextElement(
      expected_native_context, Context::SECURITY_TOKEN_INDEX);
  TNode<Object> current_token =
      LoadContextElement(native_context, Context::SECURITY_TOKEN_INDEX);
  Branch(TaggedEqual(expected_token, current_token), can_access, miss);
}

void AccessorAssembler::JumpIfDataProperty(TNode<Uint32T> details,
                                           Label* writable, Label* readonly) {
  if (readonly) {
    // Accessor properties never have the READ_ONLY attribute set.
    GotoIf(IsSetWord32(details, PropertyDetails::kAttributesReadOnlyMask),
           readonly);
  } else {
    CSA_DCHECK(this, IsNotSetWord32(details,
                                    PropertyDetails::kAttributesReadOnlyMask));
  }
  TNode<Uint32T> kind = DecodeWord32<PropertyDetails::KindField>(details);
  GotoIf(
      Word32Equal(kind, Int32Constant(static_cast<int>(PropertyKind::kData))),
      writable);
  // Fall through if it's an accessor property.
}

void AccessorAssembler::HandleStoreICNativeDataProperty(
    const StoreICParameters* p, TNode<HeapObject> holder,
    TNode<Word32T> handler_word) {
  Comment("native_data_property_store");
  TNode<IntPtrT> descriptor =
      Signed(DecodeWordFromWord32<StoreHandler::DescriptorBits>(handler_word));
  TNode<AccessorInfo> accessor_info =
      CAST(LoadDescriptorValue(LoadMap(holder), descriptor));

  TailCallRuntime(Runtime::kStoreCallbackProperty, p->context(), p->receiver(),
                  holder, accessor_info, p->name(), p->value());
}

void AccessorAssembler::HandleStoreICSmiHandlerJSSharedStructFieldCase(
    TNode<Context> context, TNode<Word32T> handler_word, TNode<JSObject> holder,
    TNode<Object> value) {
  CSA_DCHECK(this,
             Word32Equal(DecodeWord32<StoreHandler::KindBits>(handler_word),
                         STORE_KIND(kSharedStructField)));
  CSA_DCHECK(
      this,
      Word32Equal(DecodeWord32<StoreHandler::RepresentationBits>(handler_word),
                  Int32Constant(Representation::kTagged)));

  TVARIABLE(Object, shared_value, value);
  SharedValueBarrier(context, &shared_value);

  TNode<BoolT> is_inobject =
      IsSetWord32<StoreHandler::IsInobjectBits>(handler_word);
  TNode<HeapObject> property_storage = Select<HeapObject>(
      is_inobject, [&]() { return holder; },
      [&]() { return LoadFastProperties(holder, true); });

  TNode<UintPtrT> index =
      DecodeWordFromWord32<StoreHandler::FieldIndexBits>(handler_word);
  TNode<IntPtrT> offset = Signed(TimesTaggedSize(index));

  StoreSharedObjectField(property_storage, offset, shared_value.value());

  // Return the original value.
  Return(value);
}

void AccessorAssembler::HandleStoreICHandlerCase(
    const StoreICParameters* p, TNode<MaybeObject> handler, Label* miss,
    ICMode ic_mode, ElementSupport support_elements) {
  Label if_smi_handler(this), if_nonsmi_handler(this);
  Label if_proto_handler(this), call_handler(this),
      store_transition_or_global_or_accessor(this);

  Branch(TaggedIsSmi(handler), &if_smi_handler, &if_nonsmi_handler);

  Label if_slow(this);

  // |handler| is a Smi, encoding what to do. See SmiHandler methods
  // for the encoding format.
  BIND(&if_smi_handler);
  {
    TNode<Object> holder = p->receiver();
    TNode<Int32T> handler_word = SmiToInt32(CAST(handler));

    Label if_fast_smi(this), if_proxy(this), if_interceptor(this);

#define ASSERT_CONSECUTIVE(a, b)                                    \
  static_assert(static_cast<intptr_t>(StoreHandler::Kind::a) + 1 == \
                static_cast<intptr_t>(StoreHandler::Kind::b));
    ASSERT_CONSECUTIVE(kGlobalProxy, kNormal)
    ASSERT_CONSECUTIVE(kNormal, kInterceptor)
    ASSERT_CONSECUTIVE(kInterceptor, kSlow)
    ASSERT_CONSECUTIVE(kSlow, kProxy)
    ASSERT_CONSECUTIVE(kProxy, kKindsNumber)
#undef ASSERT_CONSECUTIVE

    TNode<Uint32T> handler_kind =
        DecodeWord32<StoreHandler::KindBits>(handler_word);
    GotoIf(Int32LessThan(handler_kind, STORE_KIND(kGlobalProxy)), &if_fast_smi);
    GotoIf(Word32Equal(handler_kind, STORE_KIND(kProxy)), &if_proxy);
    GotoIf(Word32Equal(handler_kind, STORE_KIND(kInterceptor)),
           &if_interceptor);
    GotoIf(Word32Equal(handler_kind, STORE_KIND(kSlow)), &if_slow);
    CSA_DCHECK(this, Word32Equal(handler_kind, STORE_KIND(kNormal)));
    TNode<PropertyDictionary> properties =
        CAST(LoadSlowProperties(CAST(holder)));

    TVARIABLE(IntPtrT, var_name_index);
    Label dictionary_found(this, &var_name_index);
    if (p->IsAnyDefineOwn()) {
      NameDictionaryLookup<PropertyDictionary>(properties, CAST(p->name()),
                                               &if_slow, nullptr, miss);
    } else {
      NameDictionaryLookup<PropertyDictionary>(properties, CAST(p->name()),
                                               &dictionary_found,
                                               &var_name_index, miss);
    }

    // When dealing with class fields defined with DefineKeyedOwnIC or
    // DefineNamedOwnIC, use the slow path to check the existing property.
    if (!p->IsAnyDefineOwn()) {
      BIND(&dictionary_found);
      {
        Label if_constant(this), done(this);
        TNode<Uint32T> details =
            LoadDetailsByKeyIndex(properties, var_name_index.value());
        // Check that the property is a writable data property (no accessor).
        const int kTypeAndReadOnlyMask =
            PropertyDetails::KindField::kMask |
            PropertyDetails::kAttributesReadOnlyMask;
        static_assert(static_cast<int>(PropertyKind::kData) == 0);
        GotoIf(IsSetWord32(details, kTypeAndReadOnlyMask), miss);

        if (V8_DICT_PROPERTY_CONST_TRACKING_BOOL) {
          GotoIf(IsPropertyDetailsConst(details), miss);
        }

        StoreValueByKeyIndex<PropertyDictionary>(
            properties, var_name_index.value(), p->value());
        Return(p->value());
      }
    }
    BIND(&if_fast_smi);
    {
      Label data(this), shared_struct_field(this), native_data_property(this);
      GotoIf(Word32Equal(handler_kind, STORE_KIND(kNativeDataProperty)),
             &native_data_property);
      Branch(Word32Equal(handler_kind, STORE_KIND(kSharedStructField)),
             &shared_struct_field, &data);

      BIND(&native_data_property);
      HandleStoreICNativeDataProperty(p, CAST(holder), handler_word);

      BIND(&shared_struct_field);
      HandleStoreICSmiHandlerJSSharedStructFieldCase(p->context(), handler_word,
                                                     CAST(holder), p->value());

      BIND(&data);
      // Handle non-transitioning field stores.
      HandleStoreICSmiHandlerCase(handler_word, CAST(holder), p->value(), miss);
    }

    BIND(&if_proxy);
    {
      CSA_DCHECK(this, BoolConstant(!p->IsDefineKeyedOwn()));
      HandleStoreToProxy(p, CAST(holder), miss, support_elements);
    }

    BIND(&if_interceptor);
    {
      Comment("store_interceptor");
      TailCallRuntime(Runtime::kStorePropertyWithInterceptor, p->context(),
                      p->value(), p->receiver(), p->name());
    }

    BIND(&if_slow);
    {
      Comment("store_slow");
      // The slow case calls into the runtime to complete the store without
      // causing an IC miss that would otherwise cause a transition to the
      // generic stub.
      if (ic_mode == ICMode::kGlobalIC) {
        TailCallRuntime(Runtime::kStoreGlobalIC_Slow, p->context(), p->value(),
                        p->slot(), p->vector(), p->receiver(), p->name());
      } else {
        Runtime::FunctionId id;
        if (p->IsDefineNamedOwn()) {
          id = Runtime::kDefineNamedOwnIC_Slow;
        } else if (p->IsDefineKeyedOwn()) {
          id = Runtime::kDefineKeyedOwnIC_Slow;
        } else {
          id = Runtime::kKeyedStoreIC_Slow;
        }
        TailCallRuntime(id, p->context(), p->value(), p->receiver(), p->name());
      }
    }
  }

  BIND(&if_nonsmi_handler);
  {
    TNode<HeapObjectReference> ref_handler = CAST(handler);
    GotoIf(IsWeakOrCleared(ref_handler),
           &store_transition_or_global_or_accessor);
    TNode<HeapObject> strong_handler = CAST(handler);
    TNode<Map> handler_map = LoadMap(strong_handler);
    Branch(IsCodeMap(handler_map), &call_handler, &if_proto_handler);

    BIND(&if_proto_handler);
    {
      // Note, although DefineOwnICs don't reqiure checking for prototype
      // chain modifications the proto handlers shape is still used for
      // StoreHandler::StoreElementTransition in order to store both Code
      // handler and transition target map.
      HandleStoreICProtoHandler(p, CAST(strong_handler), &if_slow, miss,
                                ic_mode, support_elements);
    }

    // |handler| is a heap object. Must be code, call it.
    BIND(&call_handler);
    {
      TNode<Code> code_handler = CAST(strong_handler);
      TailCallStub(StoreWithVectorDescriptor{}, code_handler, p->context(),
                   p->receiver(), p->name(), p->value(), p->slot(),
                   p->vector());
    }
  }

  BIND(&store_transition_or_global_or_accessor);
  {
    // Load value or miss if the {handler} weak cell is cleared.
    CSA_DCHECK(this, IsWeakOrCleared(handler));
    TNode<HeapObject> strong_handler = GetHeapObjectAssumeWeak(handler, miss);

    Label store_global(this), store_transition(this), store_accessor(this);
    TNode<Map> strong_handler_map = LoadMap(strong_handler);
    GotoIf(IsPropertyCellMap(strong_handler_map), &store_global);
    Branch(IsAccessorPairMap(strong_handler_map), &store_accessor,
           &store_transition);

    BIND(&store_global);
    {
      if (p->IsDefineKeyedOwn()) {
        Label proceed_defining(this);
        // StoreGlobalIC_PropertyCellCase doesn't support definition
        // of private fields, so handle them in runtime.
        GotoIfNot(IsSymbol(CAST(p->name())), &proceed_defining);
        Branch(IsPrivateName(CAST(p->name())), &if_slow, &proceed_defining);
        BIND(&proceed_defining);
      }

      TNode<PropertyCell> property_cell = CAST(strong_handler);
      ExitPoint direct_exit(this);
      StoreGlobalIC_PropertyCellCase(property_cell, p->value(), &direct_exit,
                                     miss);
    }
    BIND(&store_accessor);
    {
      TNode<AccessorPair> pair = CAST(strong_handler);
      TNode<JSFunction> setter = CAST(LoadAccessorPairSetter(pair));
      // As long as this code path is not used for StoreSuperIC the receiver
      // is known to be neither undefined nor null.
      ConvertReceiverMode mode = ConvertReceiverMode::kNotNullOrUndefined;
      Return(
          CallFunction(p->context(), setter, mode, p->receiver(), p->value()));
    }
    BIND(&store_transition);
    {
      TNode<Map> map = CAST(strong_handler);
      HandleStoreICTransitionMapHandlerCase(p, map, miss,
                                            p->IsAnyDefineOwn()
                                                ? kDontCheckPrototypeValidity
                                                : kCheckPrototypeValidity);
      Return(p->value());
    }
  }
}

void AccessorAssembler::HandleStoreICTransitionMapHandlerCase(
    const StoreICParameters* p, TNode<Map> transition_map, Label* miss,
    StoreTransitionMapFlags flags) {
  DCHECK_EQ(0, flags & ~kStoreTransitionMapFlagsMask);
  if (flags & kCheckPrototypeValidity) {
    TNode<Object> maybe_validity_cell =
        LoadObjectField(transition_map, Map::kPrototypeValidityCellOffset);
    CheckPrototypeValidityCell(maybe_validity_cell, miss);
  }

  TNode<Uint32T> bitfield3 = LoadMapBitField3(transition_map);
  CSA_DCHECK(this, IsClearWord32<Map::Bits3::IsDictionaryMapBit>(bitfield3));
  GotoIf(IsSetWord32<Map::Bits3::IsDeprecatedBit>(bitfield3), miss);

  // Load last descriptor details.
  TNode<UintPtrT> nof =
      DecodeWordFromWord32<Map::Bits3::NumberOfOwnDescriptorsBits>(bitfield3);
  CSA_DCHECK(this, WordNotEqual(nof, IntPtrConstant(0)));
  TNode<DescriptorArray> descriptors = LoadMapDescriptors(transition_map);

  TNode<IntPtrT> factor = IntPtrConstant(DescriptorArray::kEntrySize);
  TNode<IntPtrT> last_key_index = UncheckedCast<IntPtrT>(IntPtrAdd(
      IntPtrConstant(DescriptorArray::ToKeyIndex(-1)), IntPtrMul(nof, factor)));
  if (flags & kValidateTransitionHandler) {
    TNode<Name> key = LoadKeyByKeyIndex(descriptors, last_key_index);
    GotoIf(TaggedNotEqual(key, p->name()), miss);
  } else {
    CSA_DCHECK(this, TaggedEqual(LoadKeyByKeyIndex(descriptors, last_key_index),
                                 p->name()));
  }
  TNode<Uint32T> details = LoadDetailsByKeyIndex(descriptors, last_key_index);
  if (flags & kValidateTransitionHandler) {
    // Follow transitions only in the following cases:
    // 1) name is a non-private symbol and attributes equal to NONE,
    // 2) name is a private symbol and attributes equal to DONT_ENUM.
    Label attributes_ok(this);
    const int kKindAndAttributesDontDeleteReadOnlyMask =
        PropertyDetails::KindField::kMask |
        PropertyDetails::kAttributesDontDeleteMask |
        PropertyDetails::kAttributesReadOnlyMask;
    static_assert(static_cast<int>(PropertyKind::kData) == 0);
    // Both DontDelete and ReadOnly attributes must not be set and it has to be
    // a kData property.
    GotoIf(IsSetWord32(details, kKindAndAttributesDontDeleteReadOnlyMask),
           miss);

    // DontEnum attribute is allowed only for private symbols and vice versa.
    Branch(Word32Equal(
               IsSetWord32(details, PropertyDetails::kAttributesDontEnumMask),
               IsPrivateSymbol(CAST(p->name()))),
           &attributes_ok, miss);

    BIND(&attributes_ok);
  }

  OverwriteExistingFastDataProperty(CAST(p->receiver()), transition_map,
                                    descriptors, last_key_index, details,
                                    p->value(), miss, true);
}

void AccessorAssembler::UpdateMayHaveInterestingProperty(
    TNode<PropertyDictionary> dict, TNode<Name> name) {
  Comment("UpdateMayHaveInterestingProperty");
  Label done(this);

  if constexpr (V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL) {
    // TODO(pthier): Add flags to swiss dictionaries.
    Goto(&done);
  } else {
    GotoIfNot(IsInterestingProperty(name), &done);
    TNode<Smi> flags = GetNameDictionaryFlags(dict);
    flags = SmiOr(
        flags,
        SmiConstant(
            NameDictionary::MayHaveInterestingPropertiesBit::encode(true)));
    SetNameDictionaryFlags(dict, flags);
    Goto(&done);
  }
  BIND(&done);
}

void AccessorAssembler::CheckFieldType(TNode<DescriptorArray> descriptors,
                                       TNode<IntPtrT> name_index,
                                       TNode<Word32T> representation,
                                       TNode<Object> value, Label* bailout) {
  Label r_smi(this), r_double(this), r_heapobject(this), all_fine(this);
  GotoIf(Word32Equal(representation, Int32Constant(Representation::kSmi)),
         &r_smi);
  GotoIf(Word32Equal(representation, Int32Constant(Representation::kDouble)),
         &r_double);
  GotoIf(
      Word32Equal(representation, Int32Constant(Representation::kHeapObject)),
      &r_heapobject);
  GotoIf(Word32Equal(representation, Int32Constant(Representation::kNone)),
         bailout);
  CSA_DCHECK(this, Word32Equal(representation,
                               Int32Constant(Representation::kTagged)));
  Goto(&all_fine);

  BIND(&r_smi);
  { Branch(TaggedIsSmi(value), &all_fine, bailout); }

  BIND(&r_double);
  {
    GotoIf(TaggedIsSmi(value), &all_fine);
    Branch(IsHeapNumber(CAST(value)), &all_fine, bailout);
  }

  BIND(&r_heapobject);
  {
    GotoIf(TaggedIsSmi(value), bailout);
    TNode<MaybeObject> field_type =
        LoadFieldTypeByKeyIndex(descriptors, name_index);
    const Address kAnyType = FieldType::Any().ptr();
    // FieldType::Any can hold any value.
    GotoIf(
        TaggedEqual(field_type, BitcastWordToTagged(IntPtrConstant(kAnyType))),
        &all_fine);
    // FieldType::Class(...) performs a map check.
    // If the type is None we want this check to fail too, thus we compare the
    // maybe weak field type as is against a weak map ptr.
#ifdef DEBUG
    {
      // Check the field type is None or a weak map.
      Label check_done(this);
      GotoIf(TaggedEqual(field_type, BitcastWordToTagged(IntPtrConstant(
                                         FieldType::None().ptr()))),
             &check_done);
      CSA_DCHECK(this, IsMap(GetHeapObjectAssumeWeak(field_type)));
      Goto(&check_done);
      BIND(&check_done);
    }
#endif  // DEBUG
    Branch(TaggedEqual(MakeWeak(LoadMap(CAST(value))), field_type), &all_fine,
           bailout);
  }

  BIND(&all_fine);
}

TNode<BoolT> AccessorAssembler::IsPropertyDetailsConst(TNode<Uint32T> details) {
  return Word32Equal(
      DecodeWord32<PropertyDetails::ConstnessField>(details),
      Int32Constant(static_cast<int32_t>(PropertyConstness::kConst)));
}

void AccessorAssembler::OverwriteExistingFastDataProperty(
    TNode<HeapObject> object, TNode<Map> object_map,
    TNode<DescriptorArray> descriptors, TNode<IntPtrT> descriptor_name_index,
    TNode<Uint32T> details, TNode<Object> value, Label* slow,
    bool do_transitioning_store) {
  Label done(this), if_field(this), if_descriptor(this);

  CSA_DCHECK(this,
             Word32Equal(DecodeWord32<PropertyDetails::KindField>(details),
                         Int32Constant(static_cast<int>(PropertyKind::kData))));

  Branch(Word32Equal(
             DecodeWord32<PropertyDetails::LocationField>(details),
             Int32Constant(static_cast<int32_t>(PropertyLocation::kField))),
         &if_field, &if_descriptor);

  BIND(&if_field);
  {
    TNode<Uint32T> representation =
        DecodeWord32<PropertyDetails::RepresentationField>(details);

    CheckFieldType(descriptors, descriptor_name_index, representation, value,
                   slow);

    TNode<UintPtrT> field_index =
        DecodeWordFromWord32<PropertyDetails::FieldIndexField>(details);
    field_index = Unsigned(
        IntPtrAdd(field_index,
                  Unsigned(LoadMapInobjectPropertiesStartInWords(object_map))));
    TNode<IntPtrT> instance_size_in_words =
        LoadMapInstanceSizeInWords(object_map);

    Label inobject(this), backing_store(this);
    Branch(UintPtrLessThan(field_index, instance_size_in_words), &inobject,
           &backing_store);

    BIND(&inobject);
    {
      TNode<IntPtrT> field_offset = Signed(TimesTaggedSize(field_index));
      Label tagged_rep(this), double_rep(this);
      Branch(
          Word32Equal(representation, Int32Constant(Representation::kDouble)),
          &double_rep, &tagged_rep);
      BIND(&double_rep);
      {
        TNode<Float64T> double_value = ChangeNumberToFloat64(CAST(value));
        if (do_transitioning_store) {
          TNode<HeapNumber> heap_number =
              AllocateHeapNumberWithValue(double_value);
          StoreMap(object, object_map);
          StoreObjectField(object, field_offset, heap_number);
        } else {
          GotoIf(IsPropertyDetailsConst(details), slow);
          TNode<HeapNumber> heap_number =
              CAST(LoadObjectField(object, field_offset));
          StoreHeapNumberValue(heap_number, double_value);
        }
        Goto(&done);
      }

      BIND(&tagged_rep);
      {
        if (do_transitioning_store) {
          StoreMap(object, object_map);
        } else {
          GotoIf(IsPropertyDetailsConst(details), slow);
        }
        StoreObjectField(object, field_offset, value);
        Goto(&done);
      }
    }

    BIND(&backing_store);
    {
      TNode<IntPtrT> backing_store_index =
          Signed(IntPtrSub(field_index, instance_size_in_words));

      if (do_transitioning_store) {
        // Allocate mutable heap number before extending properties backing
        // store to ensure that heap verifier will not see the heap in
        // inconsistent state.
        TVARIABLE(Object, var_value, value);
        {
          Label cont(this);
          GotoIf(Word32NotEqual(representation,
                                Int32Constant(Representation::kDouble)),
                 &cont);
          {
            TNode<Float64T> double_value = ChangeNumberToFloat64(CAST(value));
            TNode<HeapNumber> heap_number =
                AllocateHeapNumberWithValue(double_value);
            var_value = heap_number;
            Goto(&cont);
          }
          BIND(&cont);
        }

        TNode<PropertyArray> properties =
            ExtendPropertiesBackingStore(object, backing_store_index);
        StorePropertyArrayElement(properties, backing_store_index,
                                  var_value.value());
        StoreMap(object, object_map);
        Goto(&done);

      } else {
        Label tagged_rep(this), double_rep(this);
        TNode<PropertyArray> properties =
            CAST(LoadFastProperties(CAST(object), true));
        Branch(
            Word32Equal(representation, Int32Constant(Representation::kDouble)),
            &double_rep, &tagged_rep);
        BIND(&double_rep);
        {
          GotoIf(IsPropertyDetailsConst(details), slow);
          TNode<HeapNumber> heap_number =
              CAST(LoadPropertyArrayElement(properties, backing_store_index));
          TNode<Float64T> double_value = ChangeNumberToFloat64(CAST(value));
          StoreHeapNumberValue(heap_number, double_value);
          Goto(&done);
        }
        BIND(&tagged_rep);
        {
          GotoIf(IsPropertyDetailsConst(details), slow);
          StorePropertyArrayElement(properties, backing_store_index, value);
          Goto(&done);
        }
      }
    }
  }

  BIND(&if_descriptor);
  {
    // Check that constant matches value.
    TNode<Object> constant =
        LoadValueByKeyIndex(descriptors, descriptor_name_index);
    GotoIf(TaggedNotEqual(value, constant), slow);

    if (do_transitioning_store) {
      StoreMap(object, object_map);
    }
    Goto(&done);
  }
  BIND(&done);
}

void AccessorAssembler::StoreJSSharedStructField(
    TNode<Context> context, TNode<HeapObject> shared_struct,
    TNode<Map> shared_struct_map, TNode<DescriptorArray> descriptors,
    TNode<IntPtrT> descriptor_name_index, TNode<Uint32T> details,
    TNode<Object> maybe_local_value) {
  CSA_DCHECK(this, IsJSSharedStruct(shared_struct));

  Label done(this);

  TNode<UintPtrT> field_index =
      DecodeWordFromWord32<PropertyDetails::FieldIndexField>(details);
  field_index = Unsigned(IntPtrAdd(
      field_index,
      Unsigned(LoadMapInobjectPropertiesStartInWords(shared_struct_map))));

  TNode<IntPtrT> instance_size_in_words =
      LoadMapInstanceSizeInWords(shared_struct_map);

  TVARIABLE(Object, shared_value, maybe_local_value);
  SharedValueBarrier(context, &shared_value);

  Label inobject(this), backing_store(this);
  Branch(UintPtrLessThan(field_index, instance_size_in_words), &inobject,
         &backing_store);

  BIND(&inobject);
  {
    TNode<IntPtrT> field_offset = Signed(TimesTaggedSize(field_index));
    StoreSharedObjectField(shared_struct, field_offset, shared_value.value());
    Goto(&done);
  }

  BIND(&backing_store);
  {
    TNode<IntPtrT> backing_store_index =
        Signed(IntPtrSub(field_index, instance_size_in_words));

    CSA_DCHECK(
        this,
        Word32Equal(DecodeWord32<PropertyDetails::RepresentationField>(details),
                    Int32Constant(Representation::kTagged)));
    TNode<PropertyArray> properties =
        CAST(LoadFastProperties(CAST(shared_struct), true));
    StoreJSSharedStructPropertyArrayElement(properties, backing_store_index,
                                            shared_value.value());
    Goto(&done);
  }

  BIND(&done);
}

void AccessorAssembler::CheckPrototypeValidityCell(
    TNode<Object> maybe_validity_cell, Label* miss) {
  Label done(this);
  GotoIf(
      TaggedEqual(maybe_validity_cell, SmiConstant(Map::kPrototypeChainValid)),
      &done);
  CSA_DCHECK(this, TaggedIsNotSmi(maybe_validity_cell));

  TNode<Object> cell_value =
      LoadObjectField(CAST(maybe_validity_cell), Cell::kValueOffset);
  Branch(TaggedEqual(cell_value, SmiConstant(Map::kPrototypeChainValid)), &done,
         miss);

  BIND(&done);
}

void AccessorAssembler::HandleStoreICProtoHandler(
    const StoreICParameters* p, TNode<StoreHandler> handler, Label* slow,
    Label* miss, ICMode ic_mode, ElementSupport support_elements) {
  Comment("HandleStoreICProtoHandler");

  OnCodeHandler on_code_handler;
  if (support_elements == kSupportElements) {
    // Code sub-handlers are expected only in KeyedStoreICs.
    on_code_handler = [=, this](TNode<Code> code_handler) {
      // This is either element store or transitioning element store.
      Label if_element_store(this), if_transitioning_element_store(this);
      Branch(IsStoreHandler0Map(LoadMap(handler)), &if_element_store,
             &if_transitioning_element_store);
      BIND(&if_element_store);
      {
        TailCallStub(StoreWithVectorDescriptor{}, code_handler, p->context(),
                     p->receiver(), p->name(), p->value(), p->slot(),
                     p->vector());
      }

      BIND(&if_transitioning_element_store);
      {
        TNode<MaybeObject> maybe_transition_map =
            LoadHandlerDataField(handler, 1);
        TNode<Map> transition_map =
            CAST(GetHeapObjectAssumeWeak(maybe_transition_map, miss));

        GotoIf(IsDeprecatedMap(transition_map), miss);

        TailCallStub(StoreTransitionDescriptor{}, code_handler, p->context(),
                     p->receiver(), p->name(), transition_map, p->value(),
                     p->slot(), p->vector());
      }
    };
  }

  TNode<Object> smi_handler = HandleProtoHandler<StoreHandler>(
      p, handler, on_code_handler,
      // on_found_on_lookup_start_object
      [=, this](TNode<PropertyDictionary> properties,
                TNode<IntPtrT> name_index) {
        TNode<Uint32T> details = LoadDetailsByKeyIndex(properties, name_index);
        // Check that the property is a writable data property (no accessor).
        const int kTypeAndReadOnlyMask =
            PropertyDetails::KindField::kMask |
            PropertyDetails::kAttributesReadOnlyMask;
        static_assert(static_cast<int>(PropertyKind::kData) == 0);
        GotoIf(IsSetWord32(details, kTypeAndReadOnlyMask), miss);

        StoreValueByKeyIndex<PropertyDictionary>(properties, name_index,
                                                 p->value());
        Return(p->value());
      },
      miss, ic_mode);

  {
    Label if_add_normal(this), if_store_global_proxy(this), if_api_setter(this),
        if_accessor(this), if_native_data_property(this);

    CSA_DCHECK(this, TaggedIsSmi(smi_handler));
    TNode<Int32T> handler_word = SmiToInt32(CAST(smi_handler));

    TNode<Uint32T> handler_kind =
        DecodeWord32<StoreHandler::KindBits>(handler_word);
    GotoIf(Word32Equal(handler_kind, STORE_KIND(kNormal)), &if_add_normal);

    GotoIf(Word32Equal(handler_kind, STORE_KIND(kSlow)), slow);

    TNode<MaybeObject> maybe_holder = LoadHandlerDataField(handler, 1);
    CSA_DCHECK(this, IsWeakOrCleared(maybe_holder));
    TNode<HeapObject> holder = GetHeapObjectAssumeWeak(maybe_holder, miss);

    GotoIf(Word32Equal(handler_kind, STORE_KIND(kGlobalProxy)),
           &if_store_global_proxy);

    GotoIf(Word32Equal(handler_kind, STORE_KIND(kAccessorFromPrototype)),
           &if_accessor);

    GotoIf(Word32Equal(handler_kind, STORE_KIND(kNativeDataProperty)),
           &if_native_data_property);

    GotoIf(Word32Equal(handler_kind, STORE_KIND(kApiSetter)), &if_api_setter);

    GotoIf(Word32Equal(handler_kind, STORE_KIND(kApiSetterHolderIsPrototype)),
           &if_api_setter);

    CSA_DCHECK(this, Word32Equal(handler_kind, STORE_KIND(kProxy)));
    HandleStoreToProxy(p, CAST(holder), miss, support_elements);

    BIND(&if_add_normal);
    {
      // This is a case of "transitioning store" to a dictionary mode object
      // when the property does not exist. The "existing property" case is
      // covered above by LookupOnLookupStartObject bit handling of the smi
      // handler.
      Label slow(this);
      TNode<Map> receiver_map = LoadMap(CAST(p->receiver()));
      InvalidateValidityCellIfPrototype(receiver_map);

      TNode<PropertyDictionary> properties =
          CAST(LoadSlowProperties(CAST(p->receiver())));
      TNode<Name> name = CAST(p->name());
      AddToDictionary<PropertyDictionary>(properties, name, p->value(), &slow);
      UpdateMayHaveInterestingProperty(properties, name);
      Return(p->value());

      BIND(&slow);
      TailCallRuntime(Runtime::kAddDictionaryProperty, p->context(),
                      p->receiver(), p->name(), p->value());
    }

    BIND(&if_accessor);
    {
      Comment("accessor_store");
      // The "holder" slot (data1) in the from-prototype StoreHandler is
      // instead directly the setter function.
      TNode<JSFunction> setter = CAST(holder);

      // As long as this code path is not used for StoreSuperIC the receiver
      // is known to be neither undefined nor null.
      ConvertReceiverMode mode = ConvertReceiverMode::kNotNullOrUndefined;
      Return(
          CallFunction(p->context(), setter, mode, p->receiver(), p->value()));
    }

    BIND(&if_native_data_property);
    HandleStoreICNativeDataProperty(p, holder, handler_word);

    BIND(&if_api_setter);
    {
      Comment("api_setter");
      CSA_DCHECK(this, TaggedIsNotSmi(handler));
      TNode<FunctionTemplateInfo> function_template_info = CAST(holder);

      // Context is stored either in data2 or data3 field depending on whether
      // the access check is enabled for this handler or not.
      TNode<MaybeObject> maybe_context = Select<MaybeObject>(
          IsSetWord32<StoreHandler::DoAccessCheckOnLookupStartObjectBits>(
              handler_word),
          [=, this] { return LoadHandlerDataField(handler, 3); },
          [=, this] { return LoadHandlerDataField(handler, 2); });

      CSA_DCHECK(this, IsWeakOrCleared(maybe_context));
      TNode<Object> context = Select<Object>(
          IsCleared(maybe_context), [=, this] { return SmiConstant(0); },
          [=, this] { return GetHeapObjectAssumeWeak(maybe_context); });

      TVARIABLE(Object, api_holder, p->receiver());
      Label store(this);
      GotoIf(Word32Equal(handler_kind, STORE_KIND(kApiSetter)), &store);

      CSA_DCHECK(this, Word32Equal(handler_kind,
                                   STORE_KIND(kApiSetterHolderIsPrototype)));

      api_holder = LoadMapPrototype(LoadMap(CAST(p->receiver())));
      Goto(&store);

      BIND(&store);
      {
        TNode<Int32T> argc = Int32Constant(1);
        TNode<Context> caller_context = p->context();
        Return(CallBuiltin(Builtin::kCallApiCallbackGeneric, context, argc,
                           caller_context, function_template_info,
                           api_holder.value(), p->receiver(), p->value()));
      }
    }

    BIND(&if_store_global_proxy);
    {
      ExitPoint direct_exit(this);
      // StoreGlobalIC_PropertyCellCase doesn't properly handle private names
      // but they are not expected here anyway.
      CSA_DCHECK(this, BoolConstant(!p->IsDefineKeyedOwn()));
      StoreGlobalIC_PropertyCellCase(CAST(holder), p->value(), &direct_exit,
                                     miss);
    }
  }
}

void AccessorAssembler::HandleStoreToProxy(const StoreICParameters* p,
                                           TNode<JSProxy> proxy, Label* miss,
                                           ElementSupport support_elements) {
  TVARIABLE(IntPtrT, var_index);
  TVARIABLE(Name, var_unique);

  Label if_index(this), if_unique_name(this),
      to_name_failed(this, Label::kDeferred);

  if (support_elements == kSupportElements) {
    TryToName(p->name(), &if_index, &var_index, &if_unique_name, &var_unique,
              &to_name_failed);

    BIND(&if_unique_name);
    CallBuiltin(Builtin::kProxySetProperty, p->context(), proxy,
                var_unique.value(), p->value(), p->receiver());
    Return(p->value());

    // The index case is handled earlier by the runtime.
    BIND(&if_index);
    // TODO(mslekova): introduce TryToName that doesn't try to compute
    // the intptr index value
    Goto(&to_name_failed);

    BIND(&to_name_failed);
    TailCallRuntime(Runtime::kSetPropertyWithReceiver, p->context(), proxy,
                    p->name(), p->value(), p->receiver());
  } else {
    TNode<Object> name = CallBuiltin(Builtin::kToName, p->context(), p->name());
    TailCallBuiltin(Builtin::kProxySetProperty, p->context(), proxy, name,
                    p->value(), p->receiver());
  }
}

void AccessorAssembler::HandleStoreICSmiHandlerCase(TNode<Word32T> handler_word,
                                                    TNode<JSObject> holder,
                                                    TNode<Object> value,
                                                    Label* miss) {
  Comment("field store");
#ifdef DEBUG
  TNode<Uint32T> handler_kind =
      DecodeWord32<StoreHandler::KindBits>(handler_word);
  CSA_DCHECK(this,
             Word32Or(Word32Equal(handler_kind, STORE_KIND(kField)),
                      Word32Equal(handler_kind, STORE_KIND(kConstField))));
#endif

  TNode<Uint32T> field_representation =
      DecodeWord32<StoreHandler::RepresentationBits>(handler_word);

  Label if_smi_field(this), if_double_field(this), if_heap_object_field(this),
      if_tagged_field(this);

  int32_t case_values[] = {Representation::kTagged, Representation::kHeapObject,
                           Representation::kSmi};
  Label* case_labels[] = {&if_tagged_field, &if_heap_object_field,
                          &if_smi_field};

  Switch(field_representation, &if_double_field, case_values, case_labels, 3);

  BIND(&if_tagged_field);
  {
    Comment("store tagged field");
    HandleStoreFieldAndReturn(handler_word, holder, value, std::nullopt,
                              Representation::Tagged(), miss);
  }

  BIND(&if_heap_object_field);
  {
    Comment("heap object field checks");
    CheckHeapObjectTypeMatchesDescriptor(handler_word, holder, value, miss);

    Comment("store heap object field");
    HandleStoreFieldAndReturn(handler_word, holder, value, std::nullopt,
                              Representation::HeapObject(), miss);
  }

  BIND(&if_smi_field);
  {
    Comment("smi field checks");
    GotoIfNot(TaggedIsSmi(value), miss);

    Comment("store smi field");
    HandleStoreFieldAndReturn(handler_word, holder, value, std::nullopt,
                              Representation::Smi(), miss);
  }

  BIND(&if_double_field);
  {
    CSA_DCHECK(this, Word32Equal(field_representation,
                                 Int32Constant(Representation::kDouble)));
    Comment("double field checks");
    TNode<Float64T> double_value = TryTaggedToFloat64(value, miss);
    CheckDescriptorConsidersNumbersMutable(handler_word, holder, miss);

    Comment("store double field");
    HandleStoreFieldAndReturn(handler_word, holder, value, double_value,
                              Representation::Double(), miss);
  }
}

void AccessorAssembler::CheckHeapObjectTypeMatchesDescriptor(
    TNode<Word32T> handler_word, TNode<JSObject> holder, TNode<Object> value,
    Label* bailout) {
  GotoIf(TaggedIsSmi(value), bailout);

  Label done(this);
  // Skip field type check in favor of constant value check when storing
  // to constant field.
  GotoIf(Word32Equal(DecodeWord32<StoreHandler::KindBits>(handler_word),
                     STORE_KIND(kConstField)),
         &done);
  TNode<IntPtrT> descriptor =
      Signed(DecodeWordFromWord32<StoreHandler::DescriptorBits>(handler_word));
  TNode<MaybeObject> field_type =
      LoadDescriptorValueOrFieldType(LoadMap(holder), descriptor);

  const Address kAnyType = FieldType::Any().ptr();
  GotoIf(TaggedEqual(field_type, BitcastWordToTagged(IntPtrConstant(kAnyType))),
         &done);
  // Check that value type matches the field type.
  {
    // If the type is None we want this check to fail too, thus we compare the
    // maybe weak field type as is against a weak map ptr.
#ifdef DEBUG
    {
      // Check the field type is None or a weak map.
      Label check_done(this);
      GotoIf(TaggedEqual(field_type, BitcastWordToTagged(IntPtrConstant(
                                         FieldType::None().ptr()))),
             &check_done);
      CSA_DCHECK(this, IsMap(GetHeapObjectAssumeWeak(field_type)));
      Goto(&check_done);
      BIND(&check_done);
    }
#endif  // DEBUG
    Branch(TaggedEqual(MakeWeak(LoadMap(CAST(value))), field_type), &done,
           bailout);
  }
  BIND(&done);
}

void AccessorAssembler::CheckDescriptorConsidersNumbersMutable(
    TNode<Word32T> handler_word, TNode<JSObject> holder, Label* bailout) {
  // We have to check that the representation is Double. Checking the value
  // (either in the field or being assigned) is not enough, as we could have
  // transitioned to Tagged but still be holding a HeapNumber, which would no
  // longer be allowed to be mutable.

  // TODO(leszeks): We could skip the representation check in favor of a
  // constant value check in HandleStoreFieldAndReturn here, but then
  // HandleStoreFieldAndReturn would need an IsHeapNumber check in case both the
  // representation changed and the value is no longer a HeapNumber.
  TNode<IntPtrT> descriptor_entry =
      Signed(DecodeWordFromWord32<StoreHandler::DescriptorBits>(handler_word));
  TNode<DescriptorArray> descriptors = LoadMapDescriptors(LoadMap(holder));
  TNode<Uint32T> details =
      LoadDetailsByDescriptorEntry(descriptors, descriptor_entry);

  GotoIfNot(IsEqualInWord32<PropertyDetails::RepresentationField>(
                details, Representation::kDouble),
            bailout);
}

void AccessorAssembler::GotoIfNotSameNumberBitPattern(TNode<Float64T> left,
                                                      TNode<Float64T> right,
                                                      Label* miss) {
  // TODO(verwaest): Use a single compare on 64bit archs.
  const TNode<Uint32T> lhs_hi = Float64ExtractHighWord32(left);
  const TNode<Uint32T> rhs_hi = Float64ExtractHighWord32(right);
  GotoIfNot(Word32Equal(lhs_hi, rhs_hi), miss);
  const TNode<Uint32T> lhs_lo = Float64ExtractLowWord32(left);
  const TNode<Uint32T> rhs_lo = Float64ExtractLowWord32(right);
  GotoIfNot(Word32Equal(lhs_lo, rhs_lo), miss);
}

void AccessorAssembler::HandleStoreFieldAndReturn(
    TNode<Word32T> handler_word, TNode<JSObject> holder, TNode<Object> value,
    std::optional<TNode<Float64T>> double_value, Representation representation,
    Label* miss) {
  bool store_value_as_double = representation.IsDouble();

  TNode<BoolT> is_inobject =
      IsSetWord32<StoreHandler::IsInobjectBits>(handler_word);
  TNode<HeapObject> property_storage = Select<HeapObject>(
      is_inobject, [&]() { return holder; },
      [&]() { return LoadFastProperties(holder, true); });

  TNode<UintPtrT> index =
      DecodeWordFromWord32<StoreHandler::FieldIndexBits>(handler_word);
  TNode<IntPtrT> offset = Signed(TimesTaggedSize(index));

  // For Double fields, we want to mutate the current double-value
  // field rather than changing it to point at a new HeapNumber.
  if (store_value_as_double) {
    TVARIABLE(HeapObject, actual_property_storage, property_storage);
    TVARIABLE(IntPtrT, actual_offset, offset);

    Label property_and_offset_ready(this);

    // Store the double value directly into the mutable HeapNumber.
    TNode<Object> field = LoadObjectField(property_storage, offset);
    CSA_DCHECK(this, IsHeapNumber(CAST(field)));
    actual_property_storage = CAST(field);
    actual_offset = IntPtrConstant(offsetof(HeapNumber, value_));
    Goto(&property_and_offset_ready);

    BIND(&property_and_offset_ready);
    property_storage = actual_property_storage.value();
    offset = actual_offset.value();
  }

  // Do constant value check if necessary.
  Label do_store(this);
  GotoIfNot(Word32Equal(DecodeWord32<StoreHandler::KindBits>(handler_word),
                        STORE_KIND(kConstField)),
            &do_store);
  {
    if (store_value_as_double) {
      TNode<Float64T> current_value =
          LoadObjectField<Float64T>(property_storage, offset);
      GotoIfNotSameNumberBitPattern(current_value, *double_value, miss);
      Return(value);
    } else {
      TNode<Object> current_value = LoadObjectField(property_storage, offset);
      GotoIfNot(TaggedEqual(current_value, value), miss);
      Return(value);
    }
  }

  BIND(&do_store);
  // Do the store.
  if (store_value_as_double) {
    StoreObjectFieldNoWriteBarrier(property_storage, offset, *double_value);
  } else if (representation.IsSmi()) {
    TNode<Smi> value_smi = CAST(value);
    StoreObjectFieldNoWriteBarrier(property_storage, offset, value_smi);
  } else {
    StoreObjectField(property_storage, offset, value);
  }

  Return(value);
}

TNode<PropertyArray> AccessorAssembler::ExtendPropertiesBackingStore(
    TNode<HeapObject> object, TNode<IntPtrT> index) {
  Comment("[ Extend storage");

  TVARIABLE(HeapObject, var_properties);
  TVARIABLE(Int32T, var_encoded_hash);
  TVARIABLE(IntPtrT, var_length);

  TNode<Object> properties =
      LoadObjectField(object, JSObject::kPropertiesOrHashOffset);

  Label if_smi_hash(this), if_property_array(this), extend_store(this);
  Branch(TaggedIsSmi(properties), &if_smi_hash, &if_property_array);

  BIND(&if_smi_hash);
  {
    TNode<Int32T> hash = SmiToInt32(CAST(properties));
    TNode<Int32T> encoded_hash =
        Word32Shl(hash, Int32Constant(PropertyArray::HashField::kShift));
    var_encoded_hash = encoded_hash;
    var_length = IntPtrConstant(0);
    var_properties = EmptyFixedArrayConstant();
    Goto(&extend_store);
  }

  BIND(&if_property_array);
  {
    var_properties = CAST(properties);
    TNode<Int32T> length_and_hash_int32 = LoadAndUntagToWord32ObjectField(
        var_properties.value(), PropertyArray::kLengthAndHashOffset);
    var_encoded_hash = Word32And(
        length_and_hash_int32, Int32Constant(PropertyArray::HashField::kMask));
    var_length = ChangeInt32ToIntPtr(
        Word32And(length_and_hash_int32,
                  Int32Constant(PropertyArray::LengthField::kMask)));
    Goto(&extend_store);
  }

  BIND(&extend_store);
  {
    TVARIABLE(HeapObject, var_new_properties, var_properties.value());
    Label done(this);
    // Previous property deletion could have left behind unused backing store
    // capacity even for a map that think it doesn't have any unused fields.
    // Perform a bounds check to see if we actually have to grow the array.
    GotoIf(UintPtrLessThan(index, ParameterToIntPtr(var_length.value())),
           &done);

    TNode<IntPtrT> delta = IntPtrConstant(JSObject::kFieldsAdded);
    TNode<IntPtrT> new_capacity = IntPtrAdd(var_length.value(), delta);

    // Grow properties array.
    DCHECK(kMaxNumberOfDescriptors + JSObject::kFieldsAdded <
           FixedArrayBase::GetMaxLengthForNewSpaceAllocation(PACKED_ELEMENTS));
    // The size of a new properties backing store is guaranteed to be small
    // enough that the new backing store will be allocated in new space.
    CSA_DCHECK(this, IntPtrLessThan(new_capacity,
                                    IntPtrConstant(kMaxNumberOfDescriptors +
                                                   JSObject::kFieldsAdded)));

    TNode<PropertyArray> new_properties = AllocatePropertyArray(new_capacity);
    var_new_properties = new_properties;

    FillPropertyArrayWithUndefined(new_properties, var_length.value(),
                                   new_capacity);

    // |new_properties| is guaranteed to be in new space, so we can skip
    // the write barrier.
    CopyPropertyArrayValues(var_properties.value(), new_properties,
                            var_length.value(), SKIP_WRITE_BARRIER,
                            DestroySource::kYes);

    TNode<Int32T> new_capacity_int32 = TruncateIntPtrToInt32(new_capacity);
    TNode<Int32T> new_length_and_hash_int32 =
        Word32Or(var_encoded_hash.value(), new_capacity_int32);
    StoreObjectField(new_properties, PropertyArray::kLengthAndHashOffset,
                     SmiFromInt32(new_length_and_hash_int32));
    StoreObjectField(object, JSObject::kPropertiesOrHashOffset, new_properties);
    Comment("] Extend storage");
    Goto(&done);
    BIND(&done);
    return CAST(var_new_properties.value());
  }
}

void AccessorAssembler::EmitFastElementsBoundsCheck(
    TNode<JSObject> object, TNode<FixedArrayBase> elements,
    TNode<IntPtrT> intptr_index, TNode<BoolT> is_jsarray_condition,
    Label* miss) {
  TVARIABLE(IntPtrT, var_length);
  Comment("Fast elements bounds check");
  Label if_array(this), length_loaded(this, &var_length);
  GotoIf(is_jsarray_condition, &if_array);
  {
    var_length = LoadAndUntagFixedArrayBaseLength(elements);
    Goto(&length_loaded);
  }
  BIND(&if_array);
  {
    var_length = PositiveSmiUntag(LoadFastJSArrayLength(CAST(object)));
    Goto(&length_loaded);
  }
  BIND(&length_loaded);
  GotoIfNot(UintPtrLessThan(intptr_index, var_length.value()), miss);
}

void AccessorAssembler::EmitElementLoad(
    TNode<HeapObject> object, TNode<Word32T> elements_kind,
    TNode<IntPtrT> intptr_index, TNode<BoolT> is_jsarray_condition,
    Label* if_hole, Label* rebox_double, TVariable<Float64T>* var_double_value,
    Label* unimplemented_elements_kind, Label* out_of_bounds, Label* miss,
    ExitPoint* exit_point, LoadAccessMode access_mode) {
  Label if_rab_gsab_typed_array(this), if_typed_array(this), if_fast(this),
      if_fast_packed(this), if_fast_holey(this), if_fast_double(this),
      if_fast_holey_double(this), if_nonfast(this), if_dictionary(this);
  Branch(Int32GreaterThan(elements_kind,
                          Int32Constant(LAST_ANY_NONEXTENSIBLE_ELEMENTS_KIND)),
         &if_nonfast, &if_fast);

  BIND(&if_fast);
  {
    TNode<FixedArrayBase> elements = LoadJSObjectElements(CAST(object));
    EmitFastElementsBoundsCheck(CAST(object), elements, intptr_index,
                                is_jsarray_condition, out_of_bounds);
    int32_t kinds[] = {
        // Handled by if_fast_packed.
        PACKED_SMI_ELEMENTS, PACKED_ELEMENTS, PACKED_NONEXTENSIBLE_ELEMENTS,
        PACKED_SEALED_ELEMENTS, PACKED_FROZEN_ELEMENTS, SHARED_ARRAY_ELEMENTS,
        // Handled by if_fast_holey.
        HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS, HOLEY_NONEXTENSIBLE_ELEMENTS,
        HOLEY_FROZEN_ELEMENTS, HOLEY_SEALED_ELEMENTS,
        // Handled by if_fast_double.
        PACKED_DOUBLE_ELEMENTS,
        // Handled by if_fast_holey_double.
        HOLEY_DOUBLE_ELEMENTS};
    Label* labels[] = {// FAST_{SMI,}_ELEMENTS
                       &if_fast_packed, &if_fast_packed, &if_fast_packed,
                       &if_fast_packed, &if_fast_packed, &if_fast_packed,
                       // FAST_HOLEY_{SMI,}_ELEMENTS
                       &if_fast_holey, &if_fast_holey, &if_fast_holey,
                       &if_fast_holey, &if_fast_holey,
                       // PACKED_DOUBLE_ELEMENTS
                       &if_fast_double,
                       // HOLEY_DOUBLE_ELEMENTS
                       &if_fast_holey_double};
    Switch(elements_kind, unimplemented_elements_kind, kinds, labels,
           arraysize(kinds));

    BIND(&if_fast_packed);
    {
      Comment("fast packed elements");
      exit_point->Return(
          access_mode == LoadAccessMode::kHas
              ? TrueConstant()
              : UnsafeLoadFixedArrayElement(CAST(elements), intptr_index));
    }

    BIND(&if_fast_holey);
    {
      Comment("fast holey elements");
      TNode<Object> element =
          UnsafeLoadFixedArrayElement(CAST(elements), intptr_index);
      GotoIf(TaggedEqual(element, TheHoleConstant()), if_hole);
      exit_point->Return(access_mode == LoadAccessMode::kHas ? TrueConstant()
                                                             : element);
    }

    BIND(&if_fast_double);
    {
      Comment("packed double elements");
      if (access_mode == LoadAccessMode::kHas) {
        exit_point->Return(TrueConstant());
      } else {
        *var_double_value =
            LoadFixedDoubleArrayElement(CAST(elements), intptr_index);
        Goto(rebox_double);
      }
    }

    BIND(&if_fast_holey_double);
    {
      Comment("holey double elements");
      TNode<Float64T> value =
          LoadFixedDoubleArrayElement(CAST(elements), intptr_index, if_hole);
      if (access_mode == LoadAccessMode::kHas) {
        exit_point->Return(TrueConstant());
      } else {
        *var_double_value = value;
        Goto(rebox_double);
      }
    }
  }

  BIND(&if_nonfast);
  {
    Label uint8_elements(this), int8_elements(this), uint16_elements(this),
        int16_elements(this), uint32_elements(this), int32_elements(this),
        float32_elements(this), float64_elements(this), bigint64_elements(this),
        biguint64_elements(this), float16_elements(this);
    static_assert(LAST_ELEMENTS_KIND ==
                  LAST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND);
    GotoIf(Int32GreaterThanOrEqual(
               elements_kind,
               Int32Constant(FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND)),
           &if_rab_gsab_typed_array);
    GotoIf(Int32GreaterThanOrEqual(
               elements_kind,
               Int32Constant(FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND)),
           &if_typed_array);
    GotoIf(Word32Equal(elements_kind, Int32Constant(DICTIONARY_ELEMENTS)),
           &if_dictionary);
    Goto(unimplemented_elements_kind);

    BIND(&if_dictionary);
    {
      Comment("dictionary elements");
      if (Is64()) {
        GotoIf(UintPtrLessThan(IntPtrConstant(JSObject::kMaxElementIndex),
                               intptr_index),
               out_of_bounds);
      } else {
        GotoIf(IntPtrLessThan(intptr_index, IntPtrConstant(0)), out_of_bounds);
      }

      TNode<FixedArrayBase> elements = LoadJSObjectElements(CAST(object));
      TNode<Object> value = BasicLoadNumberDictionaryElement(
          CAST(elements), intptr_index, miss, if_hole);
      exit_point->Return(access_mode == LoadAccessMode::kHas ? TrueConstant()
                                                             : value);
    }
    {
      TVARIABLE(RawPtrT, data_ptr);
      BIND(&if_rab_gsab_typed_array);
      {
        Comment("rab gsab typed elements");
        Label variable_length(this), normal(this), length_check_ok(this);

        TNode<JSTypedArray> array = CAST(object);
        TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(array);

        // Bounds check (incl. detachedness check).
        TNode<UintPtrT> length =
            LoadVariableLengthJSTypedArrayLength(array, buffer, miss);
        Branch(UintPtrLessThan(intptr_index, length), &length_check_ok,
               out_of_bounds);
        BIND(&length_check_ok);
        {
          if (access_mode == LoadAccessMode::kHas) {
            exit_point->Return(TrueConstant());
          } else {
            data_ptr = LoadJSTypedArrayDataPtr(array);
            Label* elements_kind_labels[] = {
                &uint8_elements,    &uint8_elements,     &int8_elements,
                &uint16_elements,   &int16_elements,     &uint32_elements,
                &int32_elements,    &float32_elements,   &float64_elements,
                &bigint64_elements, &biguint64_elements, &float16_elements,
            };
            int32_t elements_kinds[] = {
                RAB_GSAB_UINT8_ELEMENTS,     RAB_GSAB_UINT8_CLAMPED_ELEMENTS,
                RAB_GSAB_INT8_ELEMENTS,      RAB_GSAB_UINT16_ELEMENTS,
                RAB_GSAB_INT16_ELEMENTS,     RAB_GSAB_UINT32_ELEMENTS,
                RAB_GSAB_INT32_ELEMENTS,     RAB_GSAB_FLOAT32_ELEMENTS,
                RAB_GSAB_FLOAT64_ELEMENTS,   RAB_GSAB_BIGINT64_ELEMENTS,
                RAB_GSAB_BIGUINT64_ELEMENTS, RAB_GSAB_FLOAT16_ELEMENTS};
            const size_t kTypedElementsKindCount =
                LAST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND -
                FIRST_RAB_GSAB_FIXED_TYPED_ARRAY_ELEMENTS_KIND + 1;
            DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kinds));
            DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kind_labels));
            Switch(elements_kind, miss, elements_kinds, elements_kind_labels,
                   kTypedElementsKindCount);
          }
        }
      }
      BIND(&if_typed_array);
      {
        Comment("typed elements");
        // Check if buffer has been detached.
        TNode<JSArrayBuffer> buffer = LoadJSArrayBufferViewBuffer(CAST(object));
        GotoIf(IsDetachedBuffer(buffer), miss);

        // Bounds check.
        TNode<UintPtrT> length = LoadJSTypedArrayLength(CAST(object));
        GotoIfNot(UintPtrLessThan(intptr_index, length), out_of_bounds);
        if (access_mode == LoadAccessMode::kHas) {
          exit_point->Return(TrueConstant());
        } else {
          data_ptr = LoadJSTypedArrayDataPtr(CAST(object));

          Label* elements_kind_labels[] = {
              &uint8_elements,    &uint8_elements,     &int8_elements,
              &uint16_elements,   &int16_elements,     &uint32_elements,
              &int32_elements,    &float32_elements,   &float64_elements,
              &bigint64_elements, &biguint64_elements, &float16_elements};
          int32_t elements_kinds[] = {
              UINT8_ELEMENTS,    UINT8_CLAMPED_ELEMENTS, INT8_ELEMENTS,
              UINT16_ELEMENTS,   INT16_ELEMENTS,         UINT32_ELEMENTS,
              INT32_ELEMENTS,    FLOAT32_ELEMENTS,       FLOAT64_ELEMENTS,
              BIGINT64_ELEMENTS, BIGUINT64_ELEMENTS,     FLOAT16_ELEMENTS};
          const size_t kTypedElementsKindCount =
              LAST_FIXED_TYPED_ARRAY_ELEMENTS_KIND -
              FIRST_FIXED_TYPED_ARRAY_ELEMENTS_KIND + 1;
          DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kinds));
          DCHECK_EQ(kTypedElementsKindCount, arraysize(elements_kind_labels));
          Switch(elements_kind, miss, elements_kinds, elements_kind_labels,
                 kTypedElementsKindCount);
        }
      }
      if (access_mode != LoadAccessMode::kHas) {
        BIND(&uint8_elements);
        {
          Comment("UINT8_ELEMENTS");  // Handles UINT8_CLAMPED_ELEMENTS too.
          TNode<Int32T> element = Load<Uint8T>(data_ptr.value(), intptr_index);
          exit_point->Return(SmiFromInt32(element));
        }
        BIND(&int8_elements);
        {
          Comment("INT8_ELEMENTS");
          TNode<Int32T> element = Load<Int8T>(data_ptr.value(), intptr_index);
          exit_point->Return(SmiFromInt32(element));
        }
        BIND(&uint16_elements);
        {
          Comment("UINT16_ELEMENTS");
          TNode<IntPtrT> index = WordShl(intptr_index, IntPtrConstant(1));
          TNode<Int32T> element = Load<Uint16T>(data_ptr.value(), index);
          exit_point->Return(SmiFromInt32(element));
        }
        BIND(&int16_elements);
        {
          Comment("INT16_ELEMENTS");
          TNode<IntPtrT> index = WordShl(intptr_index, IntPtrConstant(1));
          TNode<Int32T> element = Load<Int16T>(data_ptr.value(), index);
          exit_point->Return(SmiFromInt32(element));
        }
        BIND(&uint32_elements);
        {
          Comment("UINT32_ELEMENTS");
          TNode<IntPtrT> index = WordShl(intptr_index, IntPtrConstant(2));
          TNode<Uint32T> element = Load<Uint32T>(data_ptr.value(), index);
          exit_point->Return(ChangeUint32ToTagged(element));
        }
        BIND(&int32_elements);
        {
          Comment("INT32_ELEMENTS");
          TNode<IntPtrT> index = WordShl(intptr_index, IntPtrConstant(2));
          TNode<Int32T> element = Load<Int32T>(data_ptr.value(), index);
          exit_point->Return(ChangeInt32ToTagged(element));
        }
        BIND(&float16_elements);
        {
          Comment("FLOAT16_ELEMENTS");
          TNode<IntPtrT> index = WordShl(intptr_index, IntPtrConstant(1));
          TNode<Float16T> raw_element = Load<Float16T>(data_ptr.value(), index);
          *var_double_value = ChangeFloat16ToFloat64(raw_element);
          Goto(rebox_double);
        }
        BIND(&float32_elements);
        {
          Comment("FLOAT32_ELEMENTS");
          TNode<IntPtrT> index = WordShl(intptr_index, IntPtrConstant(2));
          TNode<Float32T> element = Load<Float32T>(data_ptr.value(), index);
          *var_double_value = ChangeFloat32ToFloat64(element);
          Goto(rebox_double);
        }
        BIND(&float64_elements);
        {
          Comment("FLOAT64_ELEMENTS");
          TNode<IntPtrT> index = WordShl(intptr_index, IntPtrConstant(3));
          TNode<Float64T> element = Load<Float64T>(data_ptr.value(), index);
          *var_double_value = element;
          Goto(rebox_double);
        }
        BIND(&bigint64_elements);
        {
          Comment("BIGINT64_ELEMENTS");
          exit_point->Return(LoadFixedTypedArrayElementAsTagged(
              data_ptr.value(), Unsigned(intptr_index), BIGINT64_ELEMENTS));
        }
        BIND(&biguint64_elements);
        {
          Comment("BIGUINT64_ELEMENTS");
          exit_point->Return(LoadFixedTypedArrayElementAsTagged(
              data_ptr.value(), Unsigned(intptr_index), BIGUINT64_ELEMENTS));
        }
      }
    }
  }
}

void AccessorAssembler::InvalidateValidityCellIfPrototype(
    TNode<Map> map, std::optional<TNode<Uint32T>> maybe_bitfield3) {
  Label is_prototype(this), cont(this);
  TNode<Uint32T> bitfield3;
  if (maybe_bitfield3) {
    bitfield3 = maybe_bitfield3.value();
  } else {
    bitfield3 = LoadMapBitField3(map);
  }

  Branch(IsSetWord32(bitfield3, Map::Bits3::IsPrototypeMapBit::kMask),
         &is_prototype, &cont);

  BIND(&is_prototype);
  {
    TNode<Object> maybe_prototype_info =
        LoadObjectField(map, Map::kTransitionsOrPrototypeInfoOffset);
    // If there's no prototype info then there's nothing to invalidate.
    GotoIf(TaggedIsSmi(maybe_prototype_info), &cont);

    TNode<ExternalReference> function = ExternalConstant(
        ExternalReference::invalidate_prototype_chains_function());
    CallCFunction(function, MachineType::AnyTagged(),
                  std::make_pair(MachineType::AnyTagged(), map));
    Goto(&cont);
  }
  BIND(&cont);
}

void AccessorAssembler::GenericElementLoad(
    TNode<HeapObject> lookup_start_object, TNode<Map> lookup_start_object_map,
    TNode<Int32T> lookup_start_object_instance_type, TNode<IntPtrT> index,
    Label* slow) {
  Comment("integer index");

  ExitPoint direct_exit(this);

  Label if_custom(this), if_element_hole(this), if_oob(this);
  Label return_undefined(this);
  // Receivers requiring non-standard element accesses (interceptors, access
  // checks, strings and string wrappers, proxies) are handled in the runtime.
  GotoIf(
      IsCustomElementsReceiverInstanceType(lookup_start_object_instance_type),
      &if_custom);
  TNode<Int32T> elements_kind = LoadMapElementsKind(lookup_start_object_map);
  TNode<BoolT> is_jsarray_condition =
      IsJSArrayInstanceType(lookup_start_object_instance_type);
  TVARIABLE(Float64T, var_double_value);
  Label rebox_double(this, &var_double_value);

  // Unimplemented elements kinds fall back to a runtime call.
  Label* unimplemented_elements_kind = slow;
  EmitElementLoad(lookup_start_object, elements_kind, index,
                  is_jsarray_condition, &if_element_hole, &rebox_double,
                  &var_double_value, unimplemented_elements_kind, &if_oob, slow,
                  &direct_exit);

  BIND(&rebox_double);
  Return(AllocateHeapNumberWithValue(var_double_value.value()));

  BIND(&if_oob);
  {
    Comment("out of bounds");
    // On TypedArrays, all OOB loads (positive and negative) return undefined
    // without ever checking the prototype chain.
    GotoIf(IsJSTypedArrayInstanceType(lookup_start_object_instance_type),
           &return_undefined);
    // Positive OOB indices within elements index range are effectively the same
    // as hole loads. Larger keys and negative keys are named loads.
    if (Is64()) {
      Branch(UintPtrLessThanOrEqual(index,
                                    IntPtrConstant(JSObject::kMaxElementIndex)),
             &if_element_hole, slow);
    } else {
      Branch(IntPtrLessThan(index, IntPtrConstant(0)), slow, &if_element_hole);
    }
  }

  BIND(&if_element_hole);
  {
    Comment("found the hole");
    BranchIfPrototypesHaveNoElements(lookup_start_object_map, &return_undefined,
                                     slow);
  }

  BIND(&if_custom);
  {
    Comment("check if string");
    GotoIfNot(IsStringInstanceType(lookup_start_object_instance_type), slow);
    Comment("load string character");
    TNode<IntPtrT> length = LoadStringLengthAsWord(CAST(lookup_start_object));
    GotoIfNot(UintPtrLessThan(index, length), slow);
    TailCallBuiltin(Builtin::kStringCharAt, NoContextConstant(),
                    lookup_start_object, index);
  }

  BIND(&return_undefined);
  Return(UndefinedConstant());
}

void AccessorAssembler::GenericPropertyLoad(
    TNode<HeapObject> lookup_start_object, TNode<Map> lookup_start_object_map,
    TNode<Int32T> lookup_start_object_instance_type, const LoadICParameters* p,
    Label* slow, UseStubCache use_stub_cache) {
  DCHECK_EQ(lookup_start_object, p->lookup_start_object());
  ExitPoint direct_exit(this);

  Comment("key is unique name");
  Label if_found_on_lookup_start_object(this), if_property_dictionary(this),
      lookup_prototype_chain(this), special_receiver(this);
  TVARIABLE(Uint32T, var_details);
  TVARIABLE(Object, var_value);

  TNode<Name> name = CAST(p->name());

  // Receivers requiring non-standard accesses (interceptors, access
  // checks, strings and string wrappers) are handled in the runtime.
  GotoIf(IsSpecialReceiverInstanceType(lookup_start_object_instance_type),
         &special_receiver);

  // Check if the lookup_start_object has fast or slow properties.
  TNode<Uint32T> bitfield3 = LoadMapBitField3(lookup_start_object_map);
  GotoIf(IsSetWord32<Map::Bits3::IsDictionaryMapBit>(bitfield3),
         &if_property_dictionary);

  {
    // Try looking up the property on the lookup_start_object; if unsuccessful,
    // look for a handler in the stub cache.
    TNode<DescriptorArray> descriptors =
        LoadMapDescriptors(lookup_start_object_map);

    Label if_descriptor_found(this), try_stub_cache(this);
    TVARIABLE(IntPtrT, var_name_index);
    Label* notfound = use_stub_cache == kUseStubCache ? &try_stub_cache
                                                      : &lookup_prototype_chain;
    DescriptorLookup(name, descriptors, bitfield3, &if_descriptor_found,
                     &var_name_index, notfound);

    BIND(&if_descriptor_found);
    {
      LoadPropertyFromFastObject(lookup_start_object, lookup_start_object_map,
                                 descriptors, var_name_index.value(),
                                 &var_details, &var_value);
      Goto(&if_found_on_lookup_start_object);
    }

    if (use_stub_cache == kUseStubCache) {
      DCHECK_EQ(lookup_start_object, p->receiver_and_lookup_start_object());
      Label stub_cache(this);
      BIND(&try_stub_cache);
      // When there is no feedback vector don't use stub cache.
      GotoIfNot(IsUndefined(p->vector()), &stub_cache);
      // Fall back to the slow path for private symbols.
      Branch(IsPrivateSymbol(name), slow, &lookup_prototype_chain);

      BIND(&stub_cache);
      Comment("stub cache probe for fast property load");
      TVARIABLE(MaybeObject, var_handler);
      Label found_handler(this, &var_handler), stub_cache_miss(this);
      TryProbeStubCache(isolate()->load_stub_cache(), lookup_start_object,
                        lookup_start_object_map, name, &found_handler,
                        &var_handler, &stub_cache_miss);
      BIND(&found_handler);
      {
        LazyLoadICParameters lazy_p(p);
        HandleLoadICHandlerCase(&lazy_p, var_handler.value(), &stub_cache_miss,
                                &direct_exit);
      }

      BIND(&stub_cache_miss);
      {
        // TODO(jkummerow): Check if the property exists on the prototype
        // chain. If it doesn't, then there's no point in missing.
        Comment("KeyedLoadGeneric_miss");
        TailCallRuntime(Runtime::kKeyedLoadIC_Miss, p->context(),
                        p->receiver_and_lookup_start_object(), name, p->slot(),
                        p->vector());
      }
    }
  }

  BIND(&if_property_dictionary);
  {
    Comment("dictionary property load");
    // We checked for LAST_CUSTOM_ELEMENTS_RECEIVER before, which rules out
    // seeing global objects here (which would need special handling).

    TVARIABLE(IntPtrT, var_name_index);
    Label dictionary_found(this, &var_name_index);
    TNode<PropertyDictionary> properties =
        CAST(LoadSlowProperties(CAST(lookup_start_object)));
    NameDictionaryLookup<PropertyDictionary>(properties, name,
                                             &dictionary_found, &var_name_index,
                                             &lookup_prototype_chain);
    BIND(&dictionary_found);
    {
      LoadPropertyFromDictionary<PropertyDictionary>(
          properties, var_name_index.value(), &var_details, &var_value);
      Goto(&if_found_on_lookup_start_object);
    }
  }

  BIND(&if_found_on_lookup_start_object);
  {
    TNode<Object> value = CallGetterIfAccessor(
        var_value.value(), lookup_start_object, var_details.value(),
        p->context(), p->receiver(), p->name(), slow);
    Return(value);
  }

  BIND(&lookup_prototype_chain);
  {
    TVARIABLE(Map, var_holder_map);
    TVARIABLE(Int32T, var_holder_instance_type);
    Label return_undefined(this), is_private_symbol(this);
    Label loop(this, {&var_holder_map, &var_holder_instance_type});

    var_holder_map = lookup_start_object_map;
    var_holder_instance_type = lookup_start_object_instance_type;
    GotoIf(IsPrivateSymbol(name), &is_private_symbol);

    Goto(&loop);
    BIND(&loop);
    {
      // Bailout if it can be an integer indexed exotic case.
      GotoIf(InstanceTypeEqual(var_holder_instance_type.value(),
                               JS_TYPED_ARRAY_TYPE),
             slow);
      TNode<HeapObject> proto = LoadMapPrototype(var_holder_map.value());
      GotoIf(TaggedEqual(proto, NullConstant()), &return_undefined);
      TNode<Map> proto_map = LoadMap(proto);
      TNode<Uint16T> proto_instance_type = LoadMapInstanceType(proto_map);
      var_holder_map = proto_map;
      var_holder_instance_type = proto_instance_type;
      Label next_proto(this), return_value(this, &var_value), goto_slow(this);
      TryGetOwnProperty(p->context(), p->receiver(), CAST(proto), proto_map,
                        proto_instance_type, name, &return_value, &var_value,
                        &next_proto, &goto_slow);

      // This trampoline and the next are required to appease Turbofan's
      // variable merging.
      BIND(&next_proto);
      Goto(&loop);

      BIND(&goto_slow);
      Goto(slow);

      BIND(&return_value);
      Return(var_value.value());
    }

    BIND(&is_private_symbol);
    {
      CSA_DCHECK(this, IsPrivateSymbol(name));

      // For private names that don't exist on the receiver, we bail
      // to the runtime to throw. For private symbols, we just return
      // undefined.
      Branch(IsPrivateName(CAST(name)), slow, &return_undefined);
    }

    BIND(&return_undefined);
    Return(UndefinedConstant());
  }

  BIND(&special_receiver);
  {
    // TODO(ishell): Consider supporting WasmObjects.
    // TODO(jkummerow): Consider supporting JSModuleNamespace.
    GotoIfNot(
        InstanceTypeEqual(lookup_start_object_instance_type, JS_PROXY_TYPE),
        slow);

    // Private field/symbol lookup is not supported.
    GotoIf(IsPrivateSymbol(name), slow);

    direct_exit.ReturnCallBuiltin(Builtin::kProxyGetProperty, p->context(),
                                  lookup_start_object, name, p->receiver(),
                                  SmiConstant(OnNonExistent::kReturnUndefined));
  }
}

//////////////////// Stub cache access helpers.

enum AccessorAssembler::StubCacheTable : int {
  kPrimary = static_cast<int>(StubCache::kPrimary),
  kSecondary = static_cast<int>(StubCache::kSecondary)
};

TNode<IntPtrT> AccessorAssembler::StubCachePrimaryOffset(TNode<Name> name,
                                                         TNode<Map> map) {
  // Compute the hash of the name (use entire hash field).
  TNode<Uint32T> raw_hash_field = LoadNameRawHash(name);
  CSA_DCHECK(this,
             Word32Equal(Word32And(raw_hash_field,
                                   Int32Constant(Name::kHashNotComputedMask)),
                         Int32Constant(0)));

  // Using only the low bits in 64-bit mode is unlikely to increase the
  // risk of collision even if the heap is spread over an area larger than
  // 4Gb (and not at all if it isn't).
  TNode<IntPtrT> map_word = BitcastTaggedToWord(map);

  TNode<Int32T> map32 = TruncateIntPtrToInt32(UncheckedCast<IntPtrT>(
      WordXor(map_word, WordShr(map_word, StubCache::kPrimaryTableBits))));
  // Base the offset on a simple combination of name and map.
  TNode<Word32T> hash = Int32Add(raw_hash_field, map32);
  uint32_t mask = (StubCache::kPrimaryTableSize - 1)
                  << StubCache::kCacheIndexShift;
  TNode<UintPtrT> result =
      ChangeUint32ToWord(Word32And(hash, Int32Constant(mask)));
  return Signed(result);
}

TNode<IntPtrT> AccessorAssembler::StubCacheSecondaryOffset(TNode<Name> name,
                                                           TNode<Map> map) {
  // See v8::internal::StubCache::SecondaryOffset().

  // Use the seed from the primary cache in the secondary cache.
  TNode<Int32T> name32 = TruncateIntPtrToInt32(BitcastTaggedToWord(name));
  TNode<Int32T> map32 = TruncateIntPtrToInt32(BitcastTaggedToWord(map));
  // Base the offset on a simple combination of name and map.
  TNode<Word32T> hash_a = Int32Add(map32, name32);
  TNode<Word32T> hash_b = Word32Shr(hash_a, StubCache::kSecondaryTableBits);
  TNode<Word32T> hash = Int32Add(hash_a, hash_b);
  int32_t mask = (StubCache::kSecondaryTableSize - 1)
                 << StubCache::kCacheIndexShift;
  TNode<UintPtrT> result =
      ChangeUint32ToWord(Word32And(hash, Int32Constant(mask)));
  return Signed(result);
}

void AccessorAssembler::TryProbeStubCacheTable(
    StubCache* stub_cache, StubCacheTable table_id, TNode<IntPtrT> entry_offset,
    TNode<Object> name, TNode<Map> map, Label* if_handler,
    TVariable<MaybeObject>* var_handler, Label* if_miss) {
  StubCache::Table table = static_cast<StubCache::Table>(table_id);
  // The {table_offset} holds the entry offset times four (due to masking
  // and shifting optimizations).
  const int kMultiplier =
      sizeof(StubCache::Entry) >> StubCache::kCacheIndexShift;
  entry_offset = IntPtrMul(entry_offset, IntPtrConstant(kMultiplier));

  TNode<ExternalReference> key_base = ExternalConstant(
      ExternalReference::Create(stub_cache->key_reference(table)));

  // Check that the key in the entry matches the name.
  DCHECK_EQ(0, offsetof(StubCache::Entry, key));
  TNode<HeapObject> cached_key =
      CAST(Load(MachineType::TaggedPointer(), key_base, entry_offset));
  GotoIf(TaggedNotEqual(name, cached_key), if_miss);

  // Check that the map in the entry matches.
  TNode<Object> cached_map = Load<Object>(
      key_base,
      IntPtrAdd(entry_offset, IntPtrConstant(offsetof(StubCache::Entry, map))));
  GotoIf(TaggedNotEqual(map, cached_map), if_miss);

  TNode<MaybeObject> handler = ReinterpretCast<MaybeObject>(
      Load(MachineType::AnyTagged(), key_base,
           IntPtrAdd(entry_offset,
                     IntPtrConstant(offsetof(StubCache::Entry, value)))));

  // We found the handler.
  *var_handler = handler;
  Goto(if_handler);
}

void AccessorAssembler::TryProbeStubCache(StubCache* stub_cache,
                                          TNode<Object> lookup_start_object,
                                          TNode<Map> lookup_start_object_map,
                                          TNode<Name> name, Label* if_handler,
                                          TVariable<MaybeObject>* var_handler,
                                          Label* if_miss) {
  Label try_secondary(this), miss(this);

  Counters* counters = isolate()->counters();
  IncrementCounter(counters->megamorphic_stub_cache_probes(), 1);

  // Probe the primary table.
  TNode<IntPtrT> primary_offset =
      StubCachePrimaryOffset(name, lookup_start_object_map);
  TryProbeStubCacheTable(stub_cache, kPrimary, primary_offset, name,
                         lookup_start_object_map, if_handler, var_handler,
                         &try_secondary);

  BIND(&try_secondary);
  {
    // Probe the secondary table.
    TNode<IntPtrT> secondary_offset =
        StubCacheSecondaryOffset(name, lookup_start_object_map);
    TryProbeStubCacheTable(stub_cache, kSecondary, secondary_offset, name,
                           lookup_start_object_map, if_handler, var_handler,
                           &miss);
  }

  BIND(&miss);
  {
    IncrementCounter(counters->megamorphic_stub_cache_misses(), 1);
    Goto(if_miss);
  }
}

//////////////////// Entry points into private implementation (one per stub).

void AccessorAssembler::LoadIC_BytecodeHandler(const LazyLoadICParameters* p,
                                               ExitPoint* exit_point) {
  // Must be kept in sync with LoadIC.

  // This function is hand-tuned to omit frame construction for common cases,
  // e.g.: monomorphic field and constant loads through smi handlers.
  // Polymorphic ICs with a hit in the first two entries also omit frames.
  // TODO(jgruber): Frame omission is fragile and can be affected by minor
  // changes in control flow and logic. We currently have no way of ensuring
  // that no frame is constructed, so it's easy to break this optimization by
  // accident.
  Label stub_call(this, Label::kDeferred), miss(this, Label::kDeferred),
      no_feedback(this, Label::kDeferred);

  GotoIf(IsUndefined(p->vector()), &no_feedback);

  TNode<Map> lookup_start_object_map =
      LoadReceiverMap(p->receiver_and_lookup_start_object());

  // Inlined fast path.
  {
    Comment("LoadIC_BytecodeHandler_fast");

    TVARIABLE(MaybeObject, var_handler);
    Label try_polymorphic(this), if_handler(this, &var_handler);

    TNode<HeapObjectReference> weak_lookup_start_object_map =
        MakeWeak(lookup_start_object_map);
    TNode<HeapObjectReference> feedback = TryMonomorphicCase(
        p->slot(), CAST(p->vector()), weak_lookup_start_object_map, &if_handler,
        &var_handler, &try_polymorphic);

    BIND(&if_handler);
    HandleLoadICHandlerCase(p, var_handler.value(), &miss, exit_point);

    BIND(&try_polymorphic);
    {
      TNode<HeapObject> strong_feedback =
          GetHeapObjectIfStrong(feedback, &miss);
      GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)), &stub_call);
      HandlePolymorphicCase(weak_lookup_start_object_map, CAST(strong_feedback),
                            &if_handler, &var_handler, &miss);
    }
  }

  BIND(&stub_call);
  {
    Comment("LoadIC_BytecodeHandler_noninlined");

    // Call into the stub that implements the non-inlined parts of LoadIC.
    exit_point->ReturnCallBuiltin(Builtin::kLoadIC_Noninlined, p->context(),
                                  p->receiver_and_lookup_start_object(),
                                  p->name(), p->slot(), p->vector());
  }

  BIND(&no_feedback);
  {
    Comment("LoadIC_BytecodeHandler_nofeedback");
    // Call into the stub that implements the non-inlined parts of LoadIC.
    exit_point->ReturnCallBuiltin(Builtin::kLoadIC_NoFeedback, p->context(),
                                  p->receiver(), p->name(),
                                  SmiConstant(FeedbackSlotKind::kLoadProperty));
  }

  BIND(&miss);
  {
    Comment("LoadIC_BytecodeHandler_miss");

    exit_point->ReturnCallRuntime(Runtime::kLoadIC_Miss, p->context(),
                                  p->receiver(), p->name(), p->slot(),
                                  p->vector());
  }
}

void AccessorAssembler::LoadIC(const LoadICParameters* p) {
  // Must be kept in sync with LoadIC_BytecodeHandler.

  ExitPoint direct_exit(this);

  TVARIABLE(MaybeObject, var_handler);
  Label if_handler(this, &var_handler), non_inlined(this, Label::kDeferred),
      try_polymorphic(this), miss(this, Label::kDeferred),
      no_feedback(this, Label::kDeferred);

  TNode<Map> lookup_start_object_map =
      LoadReceiverMap(p->receiver_and_lookup_start_object());

  GotoIf(IsUndefined(p->vector()), &no_feedback);

  // Check monomorphic case.
  TNode<HeapObjectReference> weak_lookup_start_object_map =
      MakeWeak(lookup_start_object_map);
  TNode<HeapObjectReference> feedback = TryMonomorphicCase(
      p->slot(), CAST(p->vector()), weak_lookup_start_object_map, &if_handler,
      &var_handler, &try_polymorphic);
  BIND(&if_handler);
  {
    LazyLoadICParameters lazy_p(p);
    HandleLoadICHandlerCase(&lazy_p, var_handler.value(), &miss, &direct_exit);
  }

  BIND(&try_polymorphic);
  TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
  {
    // Check polymorphic case.
    Comment("LoadIC_try_polymorphic");
    GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)), &non_inlined);
    HandlePolymorphicCase(weak_lookup_start_object_map, CAST(strong_feedback),
                          &if_handler, &var_handler, &miss);
  }

  BIND(&non_inlined);
  {
    LoadIC_Noninlined(p, lookup_start_object_map, strong_feedback, &var_handler,
                      &if_handler, &miss, &direct_exit);
  }

  BIND(&no_feedback);
  {
    Comment("LoadIC_nofeedback");
    // Call into the stub that implements the non-inlined parts of LoadIC.
    direct_exit.ReturnCallBuiltin(Builtin::kLoadIC_NoFeedback, p->context(),
                                  p->receiver(), p->name(),
                                  SmiConstant(FeedbackSlotKind::kLoadProperty));
  }

  BIND(&miss);
  direct_exit.ReturnCallRuntime(Runtime::kLoadIC_Miss, p->context(),
                                p->receiver_and_lookup_start_object(),
                                p->name(), p->slot(), p->vector());
}

void AccessorAssembler::LoadSuperIC(const LoadICParameters* p) {
  ExitPoint direct_exit(this);

  TVARIABLE(MaybeObject, var_handler);
  Label if_handler(this, &var_handler), no_feedback(this),
      non_inlined(this, Label::kDeferred), try_polymorphic(this),
      miss(this, Label::kDeferred);

  GotoIf(IsUndefined(p->vector()), &no_feedback);

  // The lookup start object cannot be a SMI, since it's the home object's
  // prototype, and it's not possible to set SMIs as prototypes.
  TNode<Map> lookup_start_object_map = LoadMap(CAST(p->lookup_start_object()));
  GotoIf(IsDeprecatedMap(lookup_start_object_map), &miss);

  TNode<HeapObjectReference> weak_lookup_start_object_map =
      MakeWeak(lookup_start_object_map);
  TNode<HeapObjectReference> feedback = TryMonomorphicCase(
      p->slot(), CAST(p->vector()), weak_lookup_start_object_map, &if_handler,
      &var_handler, &try_polymorphic);

  BIND(&if_handler);
  {
    LazyLoadICParameters lazy_p(p);
    HandleLoadICHandlerCase(&lazy_p, var_handler.value(), &miss, &direct_exit);
  }

  BIND(&no_feedback);
  { LoadSuperIC_NoFeedback(p); }

  BIND(&try_polymorphic);
  TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
  {
    Comment("LoadSuperIC_try_polymorphic");
    GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)), &non_inlined);
    HandlePolymorphicCase(weak_lookup_start_object_map, CAST(strong_feedback),
                          &if_handler, &var_handler, &miss);
  }

  BIND(&non_inlined);
  {
    // LoadIC_Noninlined can be used here, since it handles the
    // lookup_start_object != receiver case gracefully.
    LoadIC_Noninlined(p, lookup_start_object_map, strong_feedback, &var_handler,
                      &if_handler, &miss, &direct_exit);
  }

  BIND(&miss);
  direct_exit.ReturnCallRuntime(Runtime::kLoadWithReceiverIC_Miss, p->context(),
                                p->receiver(), p->lookup_start_object(),
                                p->name(), p->slot(), p->vector());
}

void AccessorAssembler::LoadIC_Noninlined(const LoadICParameters* p,
                                          TNode<Map> lookup_start_object_map,
                                          TNode<HeapObject> feedback,
                                          TVariable<MaybeObject>* var_handler,
                                          Label* if_handler, Label* miss,
                                          ExitPoint* exit_point) {
  // Not monomorphic -- this cases is handled in the bytecode handler.
  CSA_DCHECK(this, TaggedNotEqual(lookup_start_object_map, feedback));
  CSA_DCHECK(this, Word32BinaryNot(IsWeakFixedArrayMap(LoadMap(feedback))));
  DCHECK_EQ(MachineRepresentation::kTagged, var_handler->rep());

  {
    Label try_megamorphic(this), try_megadom(this);
    GotoIf(TaggedEqual(feedback, MegamorphicSymbolConstant()),
           &try_megamorphic);
    GotoIf(TaggedEqual(feedback, MegaDOMSymbolConstant()), &try_megadom);
    Goto(miss);

    BIND(&try_megamorphic);
    {
      TryProbeStubCache(isolate()->load_stub_cache(), p->lookup_start_object(),
                        lookup_start_object_map, CAST(p->name()), if_handler,
                        var_handler, miss);
    }

    BIND(&try_megadom);
    {
      TryMegaDOMCase(p->lookup_start_object(), lookup_start_object_map,
                     var_handler, p->vector(), p->slot(), miss, exit_point);
    }
  }
}

void AccessorAssembler::LoadIC_NoFeedback(const LoadICParameters* p,
                                          TNode<Smi> ic_kind) {
  Label miss(this, Label::kDeferred);
  TNode<Object> lookup_start_object = p->receiver_and_lookup_start_object();
  GotoIf(TaggedIsSmi(lookup_start_object), &miss);
  TNode<Map> lookup_start_object_map = LoadMap(CAST(lookup_start_object));
  GotoIf(IsDeprecatedMap(lookup_start_object_map), &miss);

  TNode<Uint16T> instance_type = LoadMapInstanceType(lookup_start_object_map);

  {
    // Special case for Function.prototype load, because it's very common
    // for ICs that are only executed once (MyFunc.prototype.foo = ...).
    Label not_function_prototype(this, Label::kDeferred);
    GotoIfNot(IsJSFunctionInstanceType(instance_type), &not_function_prototype);
    GotoIfNot(IsPrototypeString(p->name()), &not_function_prototype);

    GotoIfPrototypeRequiresRuntimeLookup(CAST(lookup_start_object),
                                         lookup_start_object_map,
                                         &not_function_prototype);
    Return(LoadJSFunctionPrototype(CAST(lookup_start_object), &miss));
    BIND(&not_function_prototype);
  }

  GenericPropertyLoad(CAST(lookup_start_object), lookup_start_object_map,
                      instance_type, p, &miss, kDontUseStubCache);

  BIND(&miss);
  {
    TailCallRuntime(Runtime::kLoadNoFeedbackIC_Miss, p->context(),
                    p->receiver(), p->name(), ic_kind);
  }
}

void AccessorAssembler::LoadSuperIC_NoFeedback(const LoadICParameters* p) {
  Label miss(this, Label::kDeferred);
  TNode<Object> lookup_start_object = p->lookup_start_object();

  // The lookup start object cannot be a SMI, since it's the home object's
  // prototype, and it's not possible to set SMIs as prototypes.
  TNode<Map> lookup_start_object_map = LoadMap(CAST(lookup_start_object));
  GotoIf(IsDeprecatedMap(lookup_start_object_map), &miss);

  TNode<Uint16T> instance_type = LoadMapInstanceType(lookup_start_object_map);

  GenericPropertyLoad(CAST(lookup_start_object), lookup_start_object_map,
                      instance_type, p, &miss, kDontUseStubCache);

  BIND(&miss);
  {
    TailCallRuntime(Runtime::kLoadWithReceiverNoFeedbackIC_Miss, p->context(),
                    p->receiver(), p->lookup_start_object(), p->name());
  }
}

void AccessorAssembler::LoadGlobalIC(TNode<HeapObject> maybe_feedback_vector,
                                     const LazyNode<TaggedIndex>& lazy_slot,
                                     const LazyNode<Context>& lazy_context,
                                     const LazyNode<Name>& lazy_name,
                                     TypeofMode typeof_mode,
                                     ExitPoint* exit_point) {
  Label try_handler(this, Label::kDeferred), miss(this, Label::kDeferred),
      no_feedback(this, Label::kDeferred);

  GotoIf(IsUndefined(maybe_feedback_vector), &no_feedback);
  {
    TNode<TaggedIndex> slot = lazy_slot();

    {
      TNode<FeedbackVector> vector = CAST(maybe_feedback_vector);
      LoadGlobalIC_TryPropertyCellCase(vector, slot, lazy_context, exit_point,
                                       &try_handler, &miss);

      BIND(&try_handler);
      LoadGlobalIC_TryHandlerCase(vector, slot, lazy_context, lazy_name,
                                  typeof_mode, exit_point, &miss);
    }

    BIND(&miss);
    {
      Comment("LoadGlobalIC_MissCase");
      TNode<Context> context = lazy_context();
      TNode<Name> name = lazy_name();
      exit_point->ReturnCallRuntime(Runtime::kLoadGlobalIC_Miss, context, name,
                                    slot, maybe_feedback_vector,
                                    SmiConstant(typeof_mode));
    }
  }

  BIND(&no_feedback);
  {
    int ic_kind =
        static_cast<int>((typeof_mode == TypeofMode::kInside)
                             ? FeedbackSlotKind::kLoadGlobalInsideTypeof
                             : FeedbackSlotKind::kLoadGlobalNotInsideTypeof);
    exit_point->ReturnCallBuiltin(Builtin::kLoadGlobalIC_NoFeedback,
                                  lazy_context(), lazy_name(),
                                  SmiConstant(ic_kind));
  }
}

void AccessorAssembler::LoadGlobalIC_TryPropertyCellCase(
    TNode<FeedbackVector> vector, TNode<TaggedIndex> slot,
    const LazyNode<Context>& lazy_context, ExitPoint* exit_point,
    Label* try_handler, Label* miss) {
  Comment("LoadGlobalIC_TryPropertyCellCase");

  Label if_lexical_var(this), if_property_cell(this);
  TNode<MaybeObject> maybe_weak_ref = LoadFeedbackVectorSlot(vector, slot);
  Branch(TaggedIsSmi(maybe_weak_ref), &if_lexical_var, &if_property_cell);

  BIND(&if_property_cell);
  {
    // This branch also handles the "handler mode": the weak reference is
    // cleared, the feedback extra is the handler. In that case we jump to
    // try_handler. (See FeedbackNexus::ConfigureHandlerMode.)
    CSA_DCHECK(this, IsWeakOrCleared(maybe_weak_ref));
    TNode<PropertyCell> property_cell =
        CAST(GetHeapObjectAssumeWeak(maybe_weak_ref, try_handler));
    TNode<Object> value =
        LoadObjectField(property_cell, PropertyCell::kValueOffset);
    GotoIf(TaggedEqual(value, PropertyCellHoleConstant()), miss);
    exit_point->Return(value);
  }

  BIND(&if_lexical_var);
  {
    // This branch handles the "lexical variable mode": the feedback is a SMI
    // encoding the variable location. (See
    // FeedbackNexus::ConfigureLexicalVarMode.)
    Comment("Load lexical variable");
    TNode<IntPtrT> lexical_handler = SmiUntag(CAST(maybe_weak_ref));
    TNode<IntPtrT> context_index =
        Signed(DecodeWord<FeedbackNexus::ContextIndexBits>(lexical_handler));
    TNode<IntPtrT> slot_index =
        Signed(DecodeWord<FeedbackNexus::SlotIndexBits>(lexical_handler));
    TNode<Context> context = lazy_context();
    TNode<Context> script_context = LoadScriptContext(context, context_index);
    TNode<Object> result = LoadContextElement(script_context, slot_index);
    exit_point->Return(result);
  }
}

void AccessorAssembler::LoadGlobalIC_TryHandlerCase(
    TNode<FeedbackVector> vector, TNode<TaggedIndex> slot,
    const LazyNode<Context>& lazy_context, const LazyNode<Name>& lazy_name,
    TypeofMode typeof_mode, ExitPoint* exit_point, Label* miss) {
  Comment("LoadGlobalIC_TryHandlerCase");

  Label call_handler(this), non_smi(this);

  TNode<MaybeObject> feedback_element =
      LoadFeedbackVectorSlot(vector, slot, kTaggedSize);
  TNode<Object> handler = CAST(feedback_element);
  GotoIf(TaggedEqual(handler, UninitializedSymbolConstant()), miss);

  OnNonExistent on_nonexistent = typeof_mode == TypeofMode::kNotInside
                                     ? OnNonExistent::kThrowReferenceError
                                     : OnNonExistent::kReturnUndefined;

  TNode<Context> context = lazy_context();
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<JSGlobalProxy> receiver =
      CAST(LoadContextElement(native_context, Context::GLOBAL_PROXY_INDEX));
  TNode<Object> global =
      LoadContextElement(native_context, Context::EXTENSION_INDEX);

  LazyLoadICParameters p([=] { return context; }, receiver, lazy_name,
                         [=] { return slot; }, vector, global);

  HandleLoadICHandlerCase(&p, handler, miss, exit_point, ICMode::kGlobalIC,
                          on_nonexistent);
}

void AccessorAssembler::ScriptContextTableLookup(
    TNode<Name> name, TNode<NativeContext> native_context, Label* found_hole,
    Label* not_found) {
  TNode<ScriptContextTable> script_context_table = CAST(
      LoadContextElement(native_context, Context::SCRIPT_CONTEXT_TABLE_INDEX));
  TVARIABLE(IntPtrT, context_index, IntPtrConstant(-1));
  Label loop(this, &context_index);
  TNode<IntPtrT> num_script_contexts = PositiveSmiUntag(CAST(LoadObjectField(
      script_context_table, ScriptContextTable::kLengthOffset)));
  Goto(&loop);

  BIND(&loop);
  {
    context_index = IntPtrAdd(context_index.value(), IntPtrConstant(1));
    GotoIf(IntPtrGreaterThanOrEqual(context_index.value(), num_script_contexts),
           not_found);

    TNode<Context> script_context =
        LoadArrayElement(script_context_table, context_index.value());
    TNode<ScopeInfo> scope_info =
        CAST(LoadContextElement(script_context, Context::SCOPE_INFO_INDEX));

    TNode<IntPtrT> context_local_index =
        IndexOfLocalName(scope_info, name, &loop);

    TNode<IntPtrT> var_index =
        IntPtrAdd(IntPtrConstant(Context::MIN_CONTEXT_EXTENDED_SLOTS),
                  context_local_index);
    TNode<Object> result = LoadContextElement(script_context, var_index);
    GotoIf(IsTheHole(result), found_hole);
    Return(result);
  }
}

void AccessorAssembler::LoadGlobalIC_NoFeedback(TNode<Context> context,
                                                TNode<Object> name,
                                                TNode<Smi> smi_typeof_mode) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  Label regular_load(this), throw_reference_error(this, Label::kDeferred);

  GotoIfNot(IsString(CAST(name)), &regular_load);
  ScriptContextTableLookup(CAST(name), native_context, &throw_reference_error,
                           &regular_load);

  BIND(&throw_reference_error);
  Return(CallRuntime(Runtime::kThrowReferenceError, context, name));

  BIND(&regular_load);
  TNode<JSGlobalObject> global_object =
      CAST(LoadContextElement(native_context, Context::EXTENSION_INDEX));
  TailCallBuiltin(Builtin::kLoadIC_NoFeedback, context, global_object, name,
                  smi_typeof_mode);
}

void AccessorAssembler::KeyedLoadIC(const LoadICParameters* p,
                                    LoadAccessMode access_mode) {
  ExitPoint direct_exit(this);

  TVARIABLE(MaybeObject, var_handler);
  Label if_handler(this, &var_handler), try_polymorphic(this, Label::kDeferred),
      try_megamorphic(this, Label::kDeferred),
      try_uninitialized(this, Label::kDeferred),
      try_polymorphic_name(this, Label::kDeferred),
      miss(this, Label::kDeferred), generic(this, Label::kDeferred);

  TNode<Map> lookup_start_object_map =
      LoadReceiverMap(p->receiver_and_lookup_start_object());
  GotoIf(IsDeprecatedMap(lookup_start_object_map), &miss);

  TryEnumeratedKeyedLoad(p, lookup_start_object_map, &direct_exit);

  GotoIf(IsUndefined(p->vector()), &generic);

  // Check monomorphic case.
  TNode<HeapObjectReference> weak_lookup_start_object_map =
      MakeWeak(lookup_start_object_map);
  TNode<HeapObjectReference> feedback = TryMonomorphicCase(
      p->slot(), CAST(p->vector()), weak_lookup_start_object_map, &if_handler,
      &var_handler, &try_polymorphic);
  BIND(&if_handler);
  {
    LazyLoadICParameters lazy_p(p);
    HandleLoadICHandlerCase(
        &lazy_p, var_handler.value(), &miss, &direct_exit, ICMode::kNonGlobalIC,
        OnNonExistent::kReturnUndefined, kSupportElements, access_mode);
  }

  BIND(&try_polymorphic);
  TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
  {
    // Check polymorphic case.
    Comment("KeyedLoadIC_try_polymorphic");
    GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)), &try_megamorphic);
    HandlePolymorphicCase(weak_lookup_start_object_map, CAST(strong_feedback),
                          &if_handler, &var_handler, &miss);
  }

  BIND(&try_megamorphic);
  {
    // Check megamorphic case.
    Comment("KeyedLoadIC_try_megamorphic");
    Branch(TaggedEqual(strong_feedback, MegamorphicSymbolConstant()), &generic,
           &try_uninitialized);
  }

  BIND(&generic);
  {
    // TODO(jkummerow): Inline this? Or some of it?
    TailCallBuiltin(
        access_mode == LoadAccessMode::kLoad ? Builtin::kKeyedLoadIC_Megamorphic
                                             : Builtin::kKeyedHasIC_Megamorphic,
        p->context(), p->receiver(), p->name(), p->slot(), p->vector());
  }

  BIND(&try_uninitialized);
  {
    // Check uninitialized case.
    Comment("KeyedLoadIC_try_uninitialized");
    Branch(TaggedEqual(strong_feedback, UninitializedSymbolConstant()), &miss,
           &try_polymorphic_name);
  }

  BIND(&try_polymorphic_name);
  {
    // We might have a name in feedback, and a weak fixed array in the next
    // slot.
    Comment("KeyedLoadIC_try_polymorphic_name");
    TVARIABLE(Name, var_name);
    Label if_polymorphic_name(this), feedback_matches(this),
        if_internalized(this), if_notinternalized(this, Label::kDeferred);

    // Fast-case: The recorded {feedback} matches the {name}.
    GotoIf(TaggedEqual(strong_feedback, p->name()), &feedback_matches);

    {
      // Try to internalize the {name} if it isn't already.
      TVARIABLE(IntPtrT, var_index);
      TryToName(p->name(), &miss, &var_index, &if_internalized, &var_name,
                &miss, &if_notinternalized);
    }

    BIND(&if_internalized);
    {
      // The {var_name} now contains a unique name.
      Branch(TaggedEqual(strong_feedback, var_name.value()),
             &if_polymorphic_name, &miss);
    }

    BIND(&if_notinternalized);
    {
      TVARIABLE(IntPtrT, var_index);
      TryInternalizeString(CAST(p->name()), &miss, &var_index, &if_internalized,
                           &var_name, &miss, &miss);
    }

    BIND(&feedback_matches);
    {
      var_name = CAST(p->name());
      Goto(&if_polymorphic_name);
    }

    BIND(&if_polymorphic_name);
    {
      // If the name comparison succeeded, we know we have a weak fixed array
      // with at least one map/handler pair.
      TailCallBuiltin(access_mode == LoadAccessMode::kLoad
                          ? Builtin::kKeyedLoadIC_PolymorphicName
                          : Builtin::kKeyedHasIC_PolymorphicName,
                      p->context(), p->receiver(), var_name.value(), p->slot(),
                      p->vector());
    }
  }

  BIND(&miss);
  {
    Comment("KeyedLoadIC_miss");
    TailCallRuntime(
        access_mode == LoadAccessMode::kLoad ? Runtime::kKeyedLoadIC_Miss
                                             : Runtime::kKeyedHasIC_Miss,
        p->context(), p->receiver(), p->name(), p->slot(), p->vector());
  }
}

void AccessorAssembler::KeyedLoadICGeneric(const LoadICParameters* p) {
  TVARIABLE(Object, var_name, p->name());

  Label if_runtime(this, Label::kDeferred);
  TNode<Object> lookup_start_object = p->lookup_start_object();
  GotoIf(TaggedIsSmi(lookup_start_object), &if_runtime);
  GotoIf(IsNullOrUndefined(lookup_start_object), &if_runtime);

  {
    TVARIABLE(IntPtrT, var_index);
    TVARIABLE(Name, var_unique);
    Label if_index(this), if_unique_name(this, &var_name), if_notunique(this),
        if_other(this, Label::kDeferred);

    TryToName(var_name.value(), &if_index, &var_index, &if_unique_name,
              &var_unique, &if_other, &if_notunique);

    BIND(&if_unique_name);
    {
      LoadICParameters pp(p, var_unique.value());
      TNode<Map> lookup_start_object_map = LoadMap(CAST(lookup_start_object));
      GenericPropertyLoad(CAST(lookup_start_object), lookup_start_object_map,
                          LoadMapInstanceType(lookup_start_object_map), &pp,
                          &if_runtime);
    }

    BIND(&if_other);
    {
      var_name = CallBuiltin(Builtin::kToName, p->context(), var_name.value());
      TryToName(var_name.value(), &if_index, &var_index, &if_unique_name,
                &var_unique, &if_runtime, &if_notunique);
    }

    BIND(&if_notunique);
    {
      if (v8_flags.internalize_on_the_fly) {
        // Ideally we could return undefined directly here if the name is not
        // found in the string table, i.e. it was never internalized, but that
        // invariant doesn't hold with named property interceptors (at this
        // point), so we take the {if_runtime} path instead.
        Label if_in_string_table(this);
        TryInternalizeString(CAST(var_name.value()), &if_index, &var_index,
                             &if_in_string_table, &var_unique, &if_runtime,
                             &if_runtime);

        BIND(&if_in_string_table);
        {
          // TODO(bmeurer): We currently use a version of GenericPropertyLoad
          // here, where we don't try to probe the megamorphic stub cache
          // after successfully internalizing the incoming string. Past
          // experiments with this have shown that it causes too much traffic
          // on the stub cache. We may want to re-evaluate that in the future.
          LoadICParameters pp(p, var_unique.value());
          TNode<Map> lookup_start_object_map =
              LoadMap(CAST(lookup_start_object));
          GenericPropertyLoad(CAST(lookup_start_object),
                              lookup_start_object_map,
                              LoadMapInstanceType(lookup_start_object_map), &pp,
                              &if_runtime, kDontUseStubCache);
        }
      } else {
        Goto(&if_runtime);
      }
    }

    BIND(&if_index);
    {
      TNode<Map> lookup_start_object_map = LoadMap(CAST(lookup_start_object));
      GenericElementLoad(CAST(lookup_start_object), lookup_start_object_map,
                         LoadMapInstanceType(lookup_start_object_map),
                         var_index.value(), &if_runtime);
    }
  }

  BIND(&if_runtime);
  {
    Comment("KeyedLoadGeneric_slow");
    // TODO(jkummerow): Should we use the GetProperty TF stub instead?
    TailCallRuntime(Runtime::kGetProperty, p->context(),
                    p->receiver_and_lookup_start_object(), var_name.value());
  }
}

void AccessorAssembler::KeyedLoadICPolymorphicName(const LoadICParameters* p,
                                                   LoadAccessMode access_mode) {
  TVARIABLE(MaybeObject, var_handler);
  Label if_handler(this, &var_handler), miss(this, Label::kDeferred);

  TNode<Object> lookup_start_object = p->lookup_start_object();
  TNode<Map> lookup_start_object_map = LoadReceiverMap(lookup_start_object);
  TNode<Name> name = CAST(p->name());
  TNode<FeedbackVector> vector = CAST(p->vector());
  TNode<TaggedIndex> slot = p->slot();
  TNode<Context> context = p->context();

  // When we get here, we know that the {name} matches the recorded
  // feedback name in the {vector} and can safely be used for the
  // LoadIC handler logic below.
  CSA_DCHECK(this, Word32BinaryNot(IsDeprecatedMap(lookup_start_object_map)));
  CSA_DCHECK(this, TaggedEqual(name, LoadFeedbackVectorSlot(vector, slot)),
             name, vector);

  // Check if we have a matching handler for the {lookup_start_object_map}.
  TNode<MaybeObject> feedback_element =
      LoadFeedbackVectorSlot(vector, slot, kTaggedSize);
  TNode<WeakFixedArray> array = CAST(feedback_element);
  HandlePolymorphicCase(MakeWeak(lookup_start_object_map), array, &if_handler,
                        &var_handler, &miss);

  BIND(&if_handler);
  {
    ExitPoint direct_exit(this);
    LazyLoadICParameters lazy_p(p);
    HandleLoadICHandlerCase(
        &lazy_p, var_handler.value(), &miss, &direct_exit, ICMode::kNonGlobalIC,
        OnNonExistent::kReturnUndefined, kOnlyProperties, access_mode);
  }

  BIND(&miss);
  {
    Comment("KeyedLoadIC_miss");
    TailCallRuntime(
        access_mode == LoadAccessMode::kLoad ? Runtime::kKeyedLoadIC_Miss
                                             : Runtime::kKeyedHasIC_Miss,
        context, p->receiver_and_lookup_start_object(), name, slot, vector);
  }
}

void AccessorAssembler::StoreIC(const StoreICParameters* p) {
  TVARIABLE(MaybeObject, var_handler,
            ReinterpretCast<MaybeObject>(SmiConstant(0)));

  Label if_handler(this, &var_handler),
      if_handler_from_stub_cache(this, &var_handler, Label::kDeferred),
      try_polymorphic(this, Label::kDeferred),
      try_megamorphic(this, Label::kDeferred), miss(this, Label::kDeferred),
      no_feedback(this, Label::kDeferred);

  TNode<Map> receiver_map = LoadReceiverMap(p->receiver());
  GotoIf(IsDeprecatedMap(receiver_map), &miss);

  GotoIf(IsUndefined(p->vector()), &no_feedback);

  // Check monomorphic case.
  TNode<HeapObjectReference> weak_receiver_map = MakeWeak(receiver_map);
  TNode<HeapObjectReference> feedback =
      TryMonomorphicCase(p->slot(), CAST(p->vector()), weak_receiver_map,
                         &if_handler, &var_handler, &try_polymorphic);
  BIND(&if_handler);
  {
    Comment("StoreIC_if_handler");
    HandleStoreICHandlerCase(p, var_handler.value(), &miss,
                             ICMode::kNonGlobalIC);
  }

  BIND(&try_polymorphic);
  TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
  {
    // Check polymorphic case.
    Comment("StoreIC_try_polymorphic");
    GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)), &try_megamorphic);
    HandlePolymorphicCase(weak_receiver_map, CAST(strong_feedback), &if_handler,
                          &var_handler, &miss);
  }

  BIND(&try_megamorphic);
  {
    // Check megamorphic case.
    GotoIfNot(TaggedEqual(strong_feedback, MegamorphicSymbolConstant()), &miss);

    TryProbeStubCache(p->stub_cache(isolate()), p->receiver(), receiver_map,
                      CAST(p->name()), &if_handler, &var_handler, &miss);
  }

  BIND(&no_feedback);
  {
    // TODO(v8:12548): refactor SetNamedIC as a subclass of StoreIC, which can
    // be called here and below when !p->IsDefineNamedOwn().
    auto builtin = p->IsDefineNamedOwn() ? Builtin::kDefineNamedOwnIC_NoFeedback
                                         : Builtin::kStoreIC_NoFeedback;
    TailCallBuiltin(builtin, p->context(), p->receiver(), p->name(),
                    p->value());
  }

  BIND(&miss);
  {
    auto runtime = p->IsDefineNamedOwn() ? Runtime::kDefineNamedOwnIC_Miss
                                         : Runtime::kStoreIC_Miss;
    TailCallRuntime(runtime, p->context(), p->value(), p->slot(), p->vector(),
                    p->receiver(), p->name());
  }
}

void AccessorAssembler::StoreGlobalIC(const StoreICParameters* pp) {
  Label no_feedback(this, Label::kDeferred), if_lexical_var(this),
      if_heapobject(this);
  GotoIf(IsUndefined(pp->vector()), &no_feedback);

  TNode<MaybeObject> maybe_weak_ref =
      LoadFeedbackVectorSlot(CAST(pp->vector()), pp->slot());
  Branch(TaggedIsSmi(maybe_weak_ref), &if_lexical_var, &if_heapobject);

  BIND(&if_heapobject);
  {
    Label try_handler(this), miss(this, Label::kDeferred);

    // This branch also handles the "handler mode": the weak reference is
    // cleared, the feedback extra is the handler. In that case we jump to
    // try_handler. (See FeedbackNexus::ConfigureHandlerMode.)
    CSA_DCHECK(this, IsWeakOrCleared(maybe_weak_ref));
    TNode<PropertyCell> property_cell =
        CAST(GetHeapObjectAssumeWeak(maybe_weak_ref, &try_handler));

    ExitPoint direct_exit(this);
    StoreGlobalIC_PropertyCellCase(property_cell, pp->value(), &direct_exit,
                                   &miss);

    BIND(&try_handler);
    {
      Comment("StoreGlobalIC_try_handler");
      TNode<MaybeObject> handler =
          LoadFeedbackVectorSlot(CAST(pp->vector()), pp->slot(), kTaggedSize);

      GotoIf(TaggedEqual(handler, UninitializedSymbolConstant()), &miss);

      DCHECK(pp->receiver_is_null());
      DCHECK(pp->flags_is_null());
      TNode<NativeContext> native_context = LoadNativeContext(pp->context());
      StoreICParameters p(
          pp->context(),
          LoadContextElement(native_context, Context::GLOBAL_PROXY_INDEX),
          pp->name(), pp->value(), std::nullopt, pp->slot(), pp->vector(),
          StoreICMode::kDefault);

      HandleStoreICHandlerCase(&p, handler, &miss, ICMode::kGlobalIC);
    }

    BIND(&miss);
    {
      TailCallRuntime(Runtime::kStoreGlobalIC_Miss, pp->context(), pp->value(),
                      pp->slot(), pp->vector(), pp->name());
    }
  }

  BIND(&if_lexical_var);
  {
    // This branch handles the "lexical variable mode": the feedback is a SMI
    // encoding the variable location. (See
    // FeedbackNexus::ConfigureLexicalVarMode.)
    Comment("Store lexical variable");
    TNode<IntPtrT> lexical_handler = SmiUntag(CAST(maybe_weak_ref));
    TNode<IntPtrT> context_index =
        Signed(DecodeWord<FeedbackNexus::ContextIndexBits>(lexical_handler));
    TNode<IntPtrT> slot_index =
        Signed(DecodeWord<FeedbackNexus::SlotIndexBits>(lexical_handler));
    TNode<Context> script_context =
        LoadScriptContext(pp->context(), context_index);
    StoreContextElementAndUpdateSideData(script_context, slot_index,
                                         pp->value());
    Return(pp->value());
  }

  BIND(&no_feedback);
  {
    TailCallRuntime(Runtime::kStoreGlobalICNoFeedback_Miss, pp->context(),
                    pp->value(), pp->name());
  }
}

void AccessorAssembler::StoreGlobalIC_PropertyCellCase(
    TNode<PropertyCell> property_cell, TNode<Object> value,
    ExitPoint* exit_point, Label* miss) {
  Comment("StoreGlobalIC_TryPropertyCellCase");

  // Load the payload of the global parameter cell. A hole indicates that
  // the cell has been invalidated and that the store must be handled by the
  // runtime.
  TNode<Object> cell_contents =
      LoadObjectField(property_cell, PropertyCell::kValueOffset);
  TNode<Int32T> details = LoadAndUntagToWord32ObjectField(
      property_cell, PropertyCell::kPropertyDetailsRawOffset);
  GotoIf(IsSetWord32(details, PropertyDetails::kAttributesReadOnlyMask), miss);
  CSA_DCHECK(this,
             Word32Equal(DecodeWord32<PropertyDetails::KindField>(details),
                         Int32Constant(static_cast<int>(PropertyKind::kData))));

  TNode<Uint32T> type =
      DecodeWord32<PropertyDetails::PropertyCellTypeField>(details);

  Label constant(this), store(this), not_smi(this);

  GotoIf(Word32Equal(type, Int32Constant(
                               static_cast<int>(PropertyCellType::kConstant))),
         &constant);
  CSA_DCHECK(this, IsNotAnyHole(cell_contents));

  GotoIf(Word32Equal(
             type, Int32Constant(static_cast<int>(PropertyCellType::kMutable))),
         &store);
  CSA_DCHECK(this,
             Word32Or(Word32Equal(type, Int32Constant(static_cast<int>(
                                            PropertyCellType::kConstantType))),
                      Word32Equal(type, Int32Constant(static_cast<int>(
                                            PropertyCellType::kUndefined)))));

  GotoIfNot(TaggedIsSmi(cell_contents), &not_smi);
  GotoIfNot(TaggedIsSmi(value), miss);
  Goto(&store);

  BIND(&not_smi);
  {
    GotoIf(TaggedIsSmi(value), miss);
    TNode<Map> expected_map = LoadMap(CAST(cell_contents));
    TNode<Map> map = LoadMap(CAST(value));
    GotoIfNot(TaggedEqual(expected_map, map), miss);
    Goto(&store);
  }

  BIND(&store);
  {
    StoreObjectField(property_cell, PropertyCell::kValueOffset, value);
    exit_point->Return(value);
  }

  BIND(&constant);
  {
    // Since |value| is never the hole, the equality check below also handles an
    // invalidated property cell correctly.
    CSA_DCHECK(this, IsNotAnyHole(value));
    GotoIfNot(TaggedEqual(cell_contents, value), miss);
    exit_point->Return(value);
  }
}

void AccessorAssembler::KeyedStoreIC(const StoreICParameters* p) {
  Label miss(this, Label::kDeferred);
  {
    TVARIABLE(MaybeObject, var_handler);

    Label if_handler(this, &var_handler),
        try_polymorphic(this, Label::kDeferred),
        try_megamorphic(this, Label::kDeferred),
        no_feedback(this, Label::kDeferred),
        try_polymorphic_name(this, Label::kDeferred);

    TNode<Map> receiver_map = LoadReceiverMap(p->receiver());
    GotoIf(IsDeprecatedMap(receiver_map), &miss);

    GotoIf(IsUndefined(p->vector()), &no_feedback);

    // Check monomorphic case.
    TNode<HeapObjectReference> weak_receiver_map = MakeWeak(receiver_map);
    TNode<HeapObjectReference> feedback =
        TryMonomorphicCase(p->slot(), CAST(p->vector()), weak_receiver_map,
                           &if_handler, &var_handler, &try_polymorphic);
    BIND(&if_handler);
    {
      Comment("KeyedStoreIC_if_handler");
      HandleStoreICHandlerCase(p, var_handler.value(), &miss,
                               ICMode::kNonGlobalIC, kSupportElements);
    }

    BIND(&try_polymorphic);
    TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
    {
      // CheckPolymorphic case.
      Comment("KeyedStoreIC_try_polymorphic");
      GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)),
                &try_megamorphic);
      HandlePolymorphicCase(weak_receiver_map, CAST(strong_feedback),
                            &if_handler, &var_handler, &miss);
    }

    BIND(&try_megamorphic);
    {
      // Check megamorphic case.
      Comment("KeyedStoreIC_try_megamorphic");
      Branch(TaggedEqual(strong_feedback, MegamorphicSymbolConstant()),
             &no_feedback, &try_polymorphic_name);
    }

    BIND(&no_feedback);
    {
      TailCallBuiltin(Builtin::kKeyedStoreIC_Megamorphic, p->context(),
                      p->receiver(), p->name(), p->value(), p->slot(),
                      p->vector());
    }

    BIND(&try_polymorphic_name);
    {
      // We might have a name in feedback, and a fixed array in the next slot.
      Comment("KeyedStoreIC_try_polymorphic_name");
      GotoIfNot(TaggedEqual(strong_feedback, p->name()), &miss);
      // If the name comparison succeeded, we know we have a feedback vector
      // with at least one map/handler pair.
      TNode<MaybeObject> feedback_element =
          LoadFeedbackVectorSlot(CAST(p->vector()), p->slot(), kTaggedSize);
      TNode<WeakFixedArray> array = CAST(feedback_element);
      HandlePolymorphicCase(weak_receiver_map, array, &if_handler, &var_handler,
                            &miss);
    }
  }
  BIND(&miss);
  {
    Comment("KeyedStoreIC_miss");
    TailCallRuntime(Runtime::kKeyedStoreIC_Miss, p->context(), p->value(),
                    p->slot(), p->vector(), p->receiver(), p->name());
  }
}

void AccessorAssembler::DefineKeyedOwnIC(const StoreICParameters* p) {
  Label miss(this, Label::kDeferred);
  {
    {
      // TODO(v8:13451): Port SetFunctionName to an ic so that we can remove
      // the runtime call here. Potentially we may also remove the
      // StoreICParameters flags and have builtins:kDefineKeyedOwnIC reusing
      // StoreWithVectorDescriptor again.
      Label did_set_function_name_if_needed(this);
      TNode<Int32T> needs_set_function_name = Word32And(
          SmiToInt32(p->flags()),
          Int32Constant(
              static_cast<int>(DefineKeyedOwnPropertyFlag::kSetFunctionName)));
      GotoIfNot(needs_set_function_name, &did_set_function_name_if_needed);

      Comment("DefineKeyedOwnIC_set_function_name");
      CallRuntime(Runtime::kSetFunctionName, p->context(), p->value(),
                  p->name());

      Goto(&did_set_function_name_if_needed);
      BIND(&did_set_function_name_if_needed);
    }
    TVARIABLE(MaybeObject, var_handler);

    Label if_handler(this, &var_handler),
        try_polymorphic(this, Label::kDeferred),
        try_megamorphic(this, Label::kDeferred),
        no_feedback(this, Label::kDeferred),
        try_polymorphic_name(this, Label::kDeferred);

    TNode<Map> receiver_map = LoadReceiverMap(p->receiver());
    GotoIf(IsDeprecatedMap(receiver_map), &miss);

    GotoIf(IsUndefined(p->vector()), &no_feedback);

    // Check monomorphic case.
    TNode<HeapObjectReference> weak_receiver_map = MakeWeak(receiver_map);
    TNode<HeapObjectReference> feedback =
        TryMonomorphicCase(p->slot(), CAST(p->vector()), weak_receiver_map,
                           &if_handler, &var_handler, &try_polymorphic);
    BIND(&if_handler);
    {
      Comment("DefineKeyedOwnIC_if_handler");
      HandleStoreICHandlerCase(p, var_handler.value(), &miss,
                               ICMode::kNonGlobalIC, kSupportElements);
    }

    BIND(&try_polymorphic);
    TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
    {
      // CheckPolymorphic case.
      Comment("DefineKeyedOwnIC_try_polymorphic");
      GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)),
                &try_megamorphic);
      HandlePolymorphicCase(weak_receiver_map, CAST(strong_feedback),
                            &if_handler, &var_handler, &miss);
    }

    BIND(&try_megamorphic);
    {
      // Check megamorphic case.
      Comment("DefineKeyedOwnIC_try_megamorphic");
      Branch(TaggedEqual(strong_feedback, MegamorphicSymbolConstant()),
             &no_feedback, &try_polymorphic_name);
    }

    BIND(&no_feedback);
    {
      TailCallBuiltin(Builtin::kDefineKeyedOwnIC_Megamorphic, p->context(),
                      p->receiver(), p->name(), p->value());
    }

    BIND(&try_polymorphic_name);
    {
      // We might have a name in feedback, and a fixed array in the next slot.
      Comment("DefineKeyedOwnIC_try_polymorphic_name");
      GotoIfNot(TaggedEqual(strong_feedback, p->name()), &miss);
      // If the name comparison succeeded, we know we have a feedback vector
      // with at least one map/handler pair.
      TNode<MaybeObject> feedback_element =
          LoadFeedbackVectorSlot(CAST(p->vector()), p->slot(), kTaggedSize);
      TNode<WeakFixedArray> array = CAST(feedback_element);
      HandlePolymorphicCase(weak_receiver_map, array, &if_handler, &var_handler,
                            &miss);
    }
  }
  BIND(&miss);
  {
    Comment("DefineKeyedOwnIC_miss");
    TailCallRuntime(Runtime::kDefineKeyedOwnIC_Miss, p->context(), p->value(),
                    p->slot(), p->vector(), p->receiver(), p->name());
  }
}

void AccessorAssembler::StoreInArrayLiteralIC(const StoreICParameters* p) {
  Label miss(this, Label::kDeferred), no_feedback(this, Label::kDeferred);
  {
    TVARIABLE(MaybeObject, var_handler);

    Label if_handler(this, &var_handler),
        try_polymorphic(this, Label::kDeferred),
        try_megamorphic(this, Label::kDeferred);

    TNode<Map> array_map = LoadReceiverMap(p->receiver());
    GotoIf(IsDeprecatedMap(array_map), &miss);

    GotoIf(IsUndefined(p->vector()), &no_feedback);

    TNode<HeapObjectReference> weak_array_map = MakeWeak(array_map);
    TNode<HeapObjectReference> feedback =
        TryMonomorphicCase(p->slot(), CAST(p->vector()), weak_array_map,
                           &if_handler, &var_handler, &try_polymorphic);

    BIND(&if_handler);
    {
      Comment("StoreInArrayLiteralIC_if_handler");
      // This is a stripped-down version of HandleStoreICHandlerCase.
      Label if_transitioning_element_store(this), if_smi_handler(this);

      // Check used to identify the Slow case.
      // Currently only the Slow case uses a Smi handler.
      GotoIf(TaggedIsSmi(var_handler.value()), &if_smi_handler);

      TNode<HeapObject> handler = CAST(var_handler.value());
      GotoIfNot(IsCode(handler), &if_transitioning_element_store);

      {
        // Call the handler.
        TNode<Code> code_handler = CAST(handler);
        TailCallStub(StoreWithVectorDescriptor{}, code_handler, p->context(),
                     p->receiver(), p->name(), p->value(), p->slot(),
                     p->vector());
      }

      BIND(&if_transitioning_element_store);
      {
        TNode<MaybeObject> maybe_transition_map =
            LoadHandlerDataField(CAST(handler), 1);
        TNode<Map> transition_map =
            CAST(GetHeapObjectAssumeWeak(maybe_transition_map, &miss));
        GotoIf(IsDeprecatedMap(transition_map), &miss);
        TNode<Code> code =
            CAST(LoadObjectField(handler, StoreHandler::kSmiHandlerOffset));
        TailCallStub(StoreTransitionDescriptor{}, code, p->context(),
                     p->receiver(), p->name(), transition_map, p->value(),
                     p->slot(), p->vector());
      }

      BIND(&if_smi_handler);
      {
#ifdef DEBUG
        // A check to ensure that no other Smi handler uses this path.
        TNode<Int32T> handler_word = SmiToInt32(CAST(var_handler.value()));
        TNode<Uint32T> handler_kind =
            DecodeWord32<StoreHandler::KindBits>(handler_word);
        CSA_DCHECK(this, Word32Equal(handler_kind, STORE_KIND(kSlow)));
#endif

        Comment("StoreInArrayLiteralIC_Slow");
        TailCallRuntime(Runtime::kStoreInArrayLiteralIC_Slow, p->context(),
                        p->value(), p->receiver(), p->name());
      }
    }

    BIND(&try_polymorphic);
    TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
    {
      Comment("StoreInArrayLiteralIC_try_polymorphic");
      GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)),
                &try_megamorphic);
      HandlePolymorphicCase(weak_array_map, CAST(strong_feedback), &if_handler,
                            &var_handler, &miss);
    }

    BIND(&try_megamorphic);
    {
      Comment("StoreInArrayLiteralIC_try_megamorphic");
      CSA_DCHECK(
          this,
          Word32Or(TaggedEqual(strong_feedback, UninitializedSymbolConstant()),
                   TaggedEqual(strong_feedback, MegamorphicSymbolConstant())));
      GotoIfNot(TaggedEqual(strong_feedback, MegamorphicSymbolConstant()),
                &miss);
      TailCallRuntime(Runtime::kStoreInArrayLiteralIC_Slow, p->context(),
                      p->value(), p->receiver(), p->name());
    }
  }

  BIND(&no_feedback);
  {
    Comment("StoreInArrayLiteralIC_NoFeedback");
    TailCallBuiltin(Builtin::kCreateDataProperty, p->context(), p->receiver(),
                    p->name(), p->value());
  }

  BIND(&miss);
  {
    Comment("StoreInArrayLiteralIC_miss");
    TailCallRuntime(Runtime::kStoreInArrayLiteralIC_Miss, p->context(),
                    p->value(), p->slot(), p->vector(), p->receiver(),
                    p->name());
  }
}

//////////////////// Public methods.

void AccessorAssembler::GenerateLoadIC() {
  using Descriptor = LoadWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  LoadICParameters p(context, receiver, name, slot, vector);
  LoadIC(&p);
}

void AccessorAssembler::GenerateLoadIC_Megamorphic() {
  using Descriptor = LoadWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  ExitPoint direct_exit(this);
  TVARIABLE(MaybeObject, var_handler);
  Label if_handler(this, &var_handler), miss(this, Label::kDeferred);

  CSA_DCHECK(this, TaggedEqual(LoadFeedbackVectorSlot(CAST(vector), slot),
                               MegamorphicSymbolConstant()));

  TryProbeStubCache(isolate()->load_stub_cache(), receiver, CAST(name),
                    &if_handler, &var_handler, &miss);

  BIND(&if_handler);
  LazyLoadICParameters p(
      // lazy_context
      [=] { return context; }, receiver,
      // lazy_name
      [=] { return name; },
      // lazy_slot
      [=] { return slot; }, vector);
  HandleLoadICHandlerCase(&p, var_handler.value(), &miss, &direct_exit);

  BIND(&miss);
  direct_exit.ReturnCallRuntime(Runtime::kLoadIC_Miss, context, receiver, name,
                                slot, vector);
}

void AccessorAssembler::GenerateLoadIC_Noninlined() {
  using Descriptor = LoadWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<FeedbackVector>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  ExitPoint direct_exit(this);
  TVARIABLE(MaybeObject, var_handler);
  Label if_handler(this, &var_handler), miss(this, Label::kDeferred);

  TNode<MaybeObject> feedback_element = LoadFeedbackVectorSlot(vector, slot);
  TNode<HeapObject> feedback = CAST(feedback_element);

  LoadICParameters p(context, receiver, name, slot, vector);
  TNode<Map> lookup_start_object_map = LoadReceiverMap(p.lookup_start_object());
  LoadIC_Noninlined(&p, lookup_start_object_map, feedback, &var_handler,
                    &if_handler, &miss, &direct_exit);

  BIND(&if_handler);
  {
    LazyLoadICParameters lazy_p(&p);
    HandleLoadICHandlerCase(&lazy_p, var_handler.value(), &miss, &direct_exit);
  }

  BIND(&miss);
  direct_exit.ReturnCallRuntime(Runtime::kLoadIC_Miss, context, receiver, name,
                                slot, vector);
}

void AccessorAssembler::GenerateLoadIC_NoFeedback() {
  using Descriptor = LoadNoFeedbackDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto context = Parameter<Context>(Descriptor::kContext);
  auto ic_kind = Parameter<Smi>(Descriptor::kICKind);

  LoadICParameters p(context, receiver, name,
                     TaggedIndexConstant(FeedbackSlot::Invalid().ToInt()),
                     UndefinedConstant());
  LoadIC_NoFeedback(&p, ic_kind);
}

void AccessorAssembler::GenerateLoadICTrampoline() {
  using Descriptor = LoadDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kLoadIC, context, receiver, name, slot, vector);
}

void AccessorAssembler::GenerateLoadICBaseline() {
  using Descriptor = LoadBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kLoadIC, context, receiver, name, slot, vector);
}

void AccessorAssembler::GenerateLoadICTrampoline_Megamorphic() {
  using Descriptor = LoadDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kLoadIC_Megamorphic, context, receiver, name, slot,
                  vector);
}

void AccessorAssembler::GenerateLoadSuperIC() {
  using Descriptor = LoadWithReceiverAndVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto lookup_start_object = Parameter<Object>(Descriptor::kLookupStartObject);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  LoadICParameters p(context, receiver, name, slot, vector,
                     lookup_start_object);
  LoadSuperIC(&p);
}

void AccessorAssembler::GenerateLoadSuperICBaseline() {
  using Descriptor = LoadWithReceiverBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto lookup_start_object = Parameter<Object>(Descriptor::kLookupStartObject);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kLoadSuperIC, context, receiver, lookup_start_object,
                  name, slot, vector);
}

void AccessorAssembler::GenerateLoadGlobalIC_NoFeedback() {
  using Descriptor = LoadGlobalNoFeedbackDescriptor;

  auto name = Parameter<Object>(Descriptor::kName);
  auto context = Parameter<Context>(Descriptor::kContext);
  auto ic_kind = Parameter<Smi>(Descriptor::kICKind);

  LoadGlobalIC_NoFeedback(context, name, ic_kind);
}

void AccessorAssembler::GenerateLoadGlobalIC(TypeofMode typeof_mode) {
  using Descriptor = LoadGlobalWithVectorDescriptor;

  auto name = Parameter<Name>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  ExitPoint direct_exit(this);
  LoadGlobalIC(
      vector,
      // lazy_slot
      [=] { return slot; },
      // lazy_context
      [=] { return context; },
      // lazy_name
      [=] { return name; }, typeof_mode, &direct_exit);
}

void AccessorAssembler::GenerateLoadGlobalICTrampoline(TypeofMode typeof_mode) {
  using Descriptor = LoadGlobalDescriptor;

  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtins::LoadGlobalICInOptimizedCode(typeof_mode), context,
                  name, slot, vector);
}

void AccessorAssembler::GenerateLoadGlobalICBaseline(TypeofMode typeof_mode) {
  using Descriptor = LoadGlobalBaselineDescriptor;

  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtins::LoadGlobalICInOptimizedCode(typeof_mode), context,
                  name, slot, vector);
}

void AccessorAssembler::LookupContext(LazyNode<Object> lazy_name,
                                      TNode<TaggedIndex> depth,
                                      LazyNode<TaggedIndex> lazy_slot,
                                      TNode<Context> context,
                                      TypeofMode typeof_mode) {
  Label slowpath(this, Label::kDeferred);

  // Check for context extensions to allow the fast path.
  TNode<Context> slot_context = GotoIfHasContextExtensionUpToDepth(
      context, Unsigned(TruncateWordToInt32(TaggedIndexToIntPtr(depth))),
      &slowpath);

  // Fast path does a normal load context.
  {
    auto slot = lazy_slot();
    Return(LoadContextElement(slot_context, TaggedIndexToIntPtr(slot)));
  }

  // Slow path when we have to call out to the runtime.
  BIND(&slowpath);
  {
    auto name = lazy_name();
    Runtime::FunctionId function_id = typeof_mode == TypeofMode::kInside
                                          ? Runtime::kLoadLookupSlotInsideTypeof
                                          : Runtime::kLoadLookupSlot;
    TailCallRuntime(function_id, context, name);
  }
}

void AccessorAssembler::GenerateLookupContextTrampoline(
    TypeofMode typeof_mode) {
  using Descriptor = LookupTrampolineDescriptor;
  LookupContext([&] { return Parameter<Object>(Descriptor::kName); },
                Parameter<TaggedIndex>(Descriptor::kDepth),
                [&] { return Parameter<TaggedIndex>(Descriptor::kSlot); },
                Parameter<Context>(Descriptor::kContext), typeof_mode);
}

void AccessorAssembler::GenerateLookupContextBaseline(TypeofMode typeof_mode) {
  using Descriptor = LookupBaselineDescriptor;
  LookupContext([&] { return Parameter<Object>(Descriptor::kName); },
                Parameter<TaggedIndex>(Descriptor::kDepth),
                [&] { return Parameter<TaggedIndex>(Descriptor::kSlot); },
                LoadContextFromBaseline(), typeof_mode);
}

void AccessorAssembler::LookupGlobalIC(
    LazyNode<Object> lazy_name, TNode<TaggedIndex> depth,
    LazyNode<TaggedIndex> lazy_slot, TNode<Context> context,
    LazyNode<FeedbackVector> lazy_feedback_vector, TypeofMode typeof_mode) {
  Label slowpath(this, Label::kDeferred);

  // Check for context extensions to allow the fast path
  GotoIfHasContextExtensionUpToDepth(
      context, Unsigned(TruncateWordToInt32(TaggedIndexToIntPtr(depth))),
      &slowpath);

  // Fast path does a normal load global
  {
    TailCallBuiltin(Builtins::LoadGlobalICInOptimizedCode(typeof_mode), context,
                    lazy_name(), lazy_slot(), lazy_feedback_vector());
  }

  // Slow path when we have to call out to the runtime
  BIND(&slowpath);
  Runtime::FunctionId function_id = typeof_mode == TypeofMode::kInside
                                        ? Runtime::kLoadLookupSlotInsideTypeof
                                        : Runtime::kLoadLookupSlot;
  TailCallRuntime(function_id, context, lazy_name());
}

void AccessorAssembler::GenerateLookupGlobalIC(TypeofMode typeof_mode) {
  using Descriptor = LookupWithVectorDescriptor;
  LookupGlobalIC([&] { return Parameter<Object>(Descriptor::kName); },
                 Parameter<TaggedIndex>(Descriptor::kDepth),
                 [&] { return Parameter<TaggedIndex>(Descriptor::kSlot); },
                 Parameter<Context>(Descriptor::kContext),
                 [&] { return Parameter<FeedbackVector>(Descriptor::kVector); },
                 typeof_mode);
}

void AccessorAssembler::GenerateLookupGlobalICTrampoline(
    TypeofMode typeof_mode) {
  using Descriptor = LookupTrampolineDescriptor;
  LookupGlobalIC([&] { return Parameter<Object>(Descriptor::kName); },
                 Parameter<TaggedIndex>(Descriptor::kDepth),
                 [&] { return Parameter<TaggedIndex>(Descriptor::kSlot); },
                 Parameter<Context>(Descriptor::kContext),
                 [&] { return LoadFeedbackVectorForStub(); }, typeof_mode);
}

void AccessorAssembler::GenerateLookupGlobalICBaseline(TypeofMode typeof_mode) {
  using Descriptor = LookupBaselineDescriptor;
  LookupGlobalIC([&] { return Parameter<Object>(Descriptor::kName); },
                 Parameter<TaggedIndex>(Descriptor::kDepth),
                 [&] { return Parameter<TaggedIndex>(Descriptor::kSlot); },
                 LoadContextFromBaseline(),
                 [&] { return LoadFeedbackVectorFromBaseline(); }, typeof_mode);
}

void AccessorAssembler::GenerateKeyedLoadIC() {
  using Descriptor = KeyedLoadWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  LoadICParameters p(context, receiver, name, slot, vector);
  KeyedLoadIC(&p, LoadAccessMode::kLoad);
}

void AccessorAssembler::GenerateEnumeratedKeyedLoadIC() {
  using Descriptor = EnumeratedKeyedLoadDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto enum_index = Parameter<Smi>(Descriptor::kEnumIndex);
  auto cache_type = Parameter<Object>(Descriptor::kCacheType);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);
  auto lookup_start_object = std::nullopt;

  LoadICParameters p(context, receiver, name, slot, vector, lookup_start_object,
                     enum_index, cache_type);
  KeyedLoadIC(&p, LoadAccessMode::kLoad);
}

void AccessorAssembler::GenerateKeyedLoadIC_Megamorphic() {
  using Descriptor = KeyedLoadWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  LoadICParameters p(context, receiver, name, slot, vector);
  KeyedLoadICGeneric(&p);
}

void AccessorAssembler::GenerateKeyedLoadICTrampoline() {
  using Descriptor = KeyedLoadDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kKeyedLoadIC, context, receiver, name, slot, vector);
}

void AccessorAssembler::GenerateKeyedLoadICBaseline() {
  using Descriptor = KeyedLoadBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kKeyedLoadIC, context, receiver, name, slot, vector);
}

void AccessorAssembler::GenerateEnumeratedKeyedLoadICBaseline() {
  using Descriptor = EnumeratedKeyedLoadBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto enum_index = Parameter<Smi>(Descriptor::kEnumIndex);
  auto cache_type = Parameter<Object>(Descriptor::kCacheType);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kEnumeratedKeyedLoadIC, context, receiver, name,
                  enum_index, cache_type, slot, vector);
}

void AccessorAssembler::GenerateKeyedLoadICTrampoline_Megamorphic() {
  using Descriptor = KeyedLoadDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kKeyedLoadIC_Megamorphic, context, receiver, name,
                  slot, vector);
}

void AccessorAssembler::GenerateKeyedLoadIC_PolymorphicName() {
  using Descriptor = LoadWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<FeedbackVector>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  LoadICParameters p(context, receiver, name, slot, vector);
  KeyedLoadICPolymorphicName(&p, LoadAccessMode::kLoad);
}

void AccessorAssembler::GenerateStoreGlobalIC() {
  using Descriptor = StoreGlobalWithVectorDescriptor;

  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto flags = std::nullopt;
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  StoreICParameters p(context, std::nullopt, name, value, flags, slot, vector,
                      StoreICMode::kDefault);
  StoreGlobalIC(&p);
}

void AccessorAssembler::GenerateStoreGlobalICTrampoline() {
  using Descriptor = StoreGlobalDescriptor;

  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kStoreGlobalIC, context, name, value, slot, vector);
}

void AccessorAssembler::GenerateStoreGlobalICBaseline() {
  using Descriptor = StoreGlobalBaselineDescriptor;

  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kStoreGlobalIC, context, name, value, slot, vector);
}

void AccessorAssembler::GenerateStoreIC() {
  using Descriptor = StoreWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = std::nullopt;
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  StoreICParameters p(context, receiver, name, value, flags, slot, vector,
                      StoreICMode::kDefault);
  StoreIC(&p);
}

void AccessorAssembler::GenerateStoreIC_Megamorphic() {
  using Descriptor = StoreWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = std::nullopt;
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  ExitPoint direct_exit(this);
  TVARIABLE(MaybeObject, var_handler);
  Label if_handler(this, &var_handler), miss(this, Label::kDeferred);

  CSA_DCHECK(this, TaggedEqual(LoadFeedbackVectorSlot(CAST(vector), slot),
                               MegamorphicSymbolConstant()));

  TryProbeStubCache(isolate()->store_stub_cache(), receiver, CAST(name),
                    &if_handler, &var_handler, &miss);

  BIND(&if_handler);
  {
    StoreICParameters p(context, receiver, name, value, flags, slot, vector,
                        StoreICMode::kDefault);
    HandleStoreICHandlerCase(&p, var_handler.value(), &miss,
                             ICMode::kNonGlobalIC);
  }

  BIND(&miss);
  {
    direct_exit.ReturnCallRuntime(Runtime::kStoreIC_Miss, context, value, slot,
                                  vector, receiver, name);
  }
}

void AccessorAssembler::GenerateStoreICTrampoline() {
  using Descriptor = StoreDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kStoreIC, context, receiver, name, value, slot,
                  vector);
}

void AccessorAssembler::GenerateStoreICTrampoline_Megamorphic() {
  using Descriptor = StoreDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kStoreIC_Megamorphic, context, receiver, name, value,
                  slot, vector);
}

void AccessorAssembler::GenerateStoreICBaseline() {
  using Descriptor = StoreBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kStoreIC, context, receiver, name, value, slot,
                  vector);
}

void AccessorAssembler::GenerateDefineNamedOwnIC() {
  using Descriptor = StoreWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = std::nullopt;
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  StoreICParameters p(context, receiver, name, value, flags, slot, vector,
                      StoreICMode::kDefineNamedOwn);
  // StoreIC is a generic helper than handle both set and define own
  // named stores.
  StoreIC(&p);
}

void AccessorAssembler::GenerateDefineNamedOwnICTrampoline() {
  using Descriptor = StoreDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kDefineNamedOwnIC, context, receiver, name, value,
                  slot, vector);
}

void AccessorAssembler::GenerateDefineNamedOwnICBaseline() {
  using Descriptor = StoreWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kDefineNamedOwnIC, context, receiver, name, value,
                  slot, vector);
}

void AccessorAssembler::GenerateKeyedStoreIC() {
  using Descriptor = StoreWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = std::nullopt;
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  StoreICParameters p(context, receiver, name, value, flags, slot, vector,
                      StoreICMode::kDefault);
  KeyedStoreIC(&p);
}

void AccessorAssembler::GenerateKeyedStoreICTrampoline() {
  using Descriptor = StoreDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kKeyedStoreIC, context, receiver, name, value, slot,
                  vector);
}

void AccessorAssembler::GenerateKeyedStoreICTrampoline_Megamorphic() {
  using Descriptor = StoreDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kKeyedStoreIC_Megamorphic, context, receiver, name,
                  value, slot, vector);
}

void AccessorAssembler::GenerateKeyedStoreICBaseline() {
  using Descriptor = StoreBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kKeyedStoreIC, context, receiver, name, value, slot,
                  vector);
}

void AccessorAssembler::GenerateDefineKeyedOwnIC() {
  using Descriptor = DefineKeyedOwnWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = Parameter<Smi>(Descriptor::kFlags);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  StoreICParameters p(context, receiver, name, value, flags, slot, vector,
                      StoreICMode::kDefineKeyedOwn);
  DefineKeyedOwnIC(&p);
}

void AccessorAssembler::GenerateDefineKeyedOwnICTrampoline() {
  using Descriptor = DefineKeyedOwnDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = Parameter<Smi>(Descriptor::kFlags);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<FeedbackVector> vector = LoadFeedbackVectorForStub();

  TailCallBuiltin(Builtin::kDefineKeyedOwnIC, context, receiver, name, value,
                  flags, slot, vector);
}

void AccessorAssembler::GenerateDefineKeyedOwnICBaseline() {
  using Descriptor = DefineKeyedOwnBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = Parameter<Smi>(Descriptor::kFlags);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kDefineKeyedOwnIC, context, receiver, name, value,
                  flags, slot, vector);
}

void AccessorAssembler::GenerateStoreInArrayLiteralIC() {
  using Descriptor = StoreWithVectorDescriptor;

  auto array = Parameter<Object>(Descriptor::kReceiver);
  auto index = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto flags = std::nullopt;
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  StoreICParameters p(context, array, index, value, flags, slot, vector,
                      StoreICMode::kDefault);
  StoreInArrayLiteralIC(&p);
}

void AccessorAssembler::GenerateStoreInArrayLiteralICBaseline() {
  using Descriptor = StoreBaselineDescriptor;

  auto array = Parameter<Object>(Descriptor::kReceiver);
  auto index = Parameter<Object>(Descriptor::kName);
  auto value = Parameter<Object>(Descriptor::kValue);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);

  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kStoreInArrayLiteralIC, context, array, index, value,
                  slot, vector);
}

void AccessorAssembler::GenerateCloneObjectIC_Slow() {
  using Descriptor = CloneObjectWithVectorDescriptor;
  auto source = Parameter<Object>(Descriptor::kSource);
  auto flags = Parameter<Smi>(Descriptor::kFlags);
  auto context = Parameter<Context>(Descriptor::kContext);

  // The CloneObjectIC_Slow implementation uses the same call interface as
  // CloneObjectIC, so that it can be tail called from it. However, the feedback
  // slot and vector are not used.

  // First try a fast case where we copy the properties with a CSA loop.
  Label try_fast_case(this), call_runtime(this, Label::kDeferred);

  // For SMIs and non JSObjects we use 0 in object properties.
  TVARIABLE(IntPtrT, number_of_properties, IntPtrConstant(0));
  GotoIf(TaggedIsSmi(source), &try_fast_case);
  {
    TNode<Map> source_map = LoadMap(CAST(source));
    // We still want to stay in the semi-fast case for oddballs, strings,
    // proxies and such. Therefore we continue here, but using 0 in object
    // properties.
    GotoIfNot(IsJSObjectMap(source_map), &try_fast_case);

    // At this point we don't know yet if ForEachEnumerableOwnProperty can
    // handle the source object. In case it is a dictionary mode object or has
    // non simple properties the latter will bail to `runtime_copy`. For code
    // compactness we don't check it here, assuming that the number of in-object
    // properties is set to 0 (or a reasonable value).
    number_of_properties = MapUsedInObjectProperties(source_map);
    GotoIf(IntPtrGreaterThanOrEqual(number_of_properties.value(),
                                    IntPtrConstant(JSObject::kMapCacheSize)),
           &call_runtime);
  }
  Goto(&try_fast_case);

  BIND(&try_fast_case);
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> initial_map = LoadCachedMap(
      native_context, number_of_properties.value(), &call_runtime);
  TNode<JSObject> result = AllocateJSObjectFromMap(initial_map);

  // Handle the case where the object literal overrides the prototype.
  {
    Label did_set_proto_if_needed(this);
    TNode<BoolT> is_null_proto = SmiNotEqual(
        SmiAnd(flags, SmiConstant(ObjectLiteral::kHasNullPrototype)),
        SmiConstant(Smi::zero()));
    GotoIfNot(is_null_proto, &did_set_proto_if_needed);

    CallRuntime(Runtime::kInternalSetPrototype, context, result,
                NullConstant());

    Goto(&did_set_proto_if_needed);
    BIND(&did_set_proto_if_needed);
  }

  // Early return for when we know there are no properties.
  ReturnIf(TaggedIsSmi(source), result);
  ReturnIf(IsNullOrUndefined(source), result);

  Label runtime_copy(this, Label::kDeferred);

  TNode<Map> source_map = LoadMap(CAST(source));
  GotoIfNot(IsJSObjectMap(source_map), &runtime_copy);
  // Takes care of objects with elements.
  GotoIfNot(IsEmptyFixedArray(LoadElements(CAST(source))), &runtime_copy);

  // TODO(olivf, chrome:1204540) This can still be several times slower than the
  // Babel translation. TF uses FastGetOwnValuesOrEntries -- should we do sth
  // similar here?
  ForEachEnumerableOwnProperty(
      context, source_map, CAST(source), kPropertyAdditionOrder,
      [=, this](TNode<Name> key, LazyNode<Object> value) {
        CreateDataProperty(context, result, key, value());
      },
      &runtime_copy);
  Return(result);

  // This is the fall-back case for the above fastcase, where we allocated an
  // object, but failed to copy the properties in CSA.
  BIND(&runtime_copy);
  CallRuntime(Runtime::kCopyDataProperties, context, result, source);
  Return(result);

  // Final fallback is to call into the runtime version.
  BIND(&call_runtime);
  Return(CallRuntime(Runtime::kCloneObjectIC_Slow, context, source, flags));
}

void AccessorAssembler::GenerateCloneObjectICBaseline() {
  using Descriptor = CloneObjectBaselineDescriptor;

  auto source = Parameter<Object>(Descriptor::kSource);
  auto flags = Parameter<Smi>(Descriptor::kFlags);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);

  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kCloneObjectIC, context, source, flags, slot,
                  vector);
}

void AccessorAssembler::GenerateCloneObjectIC() {
  using Descriptor = CloneObjectWithVectorDescriptor;
  auto source = Parameter<Object>(Descriptor::kSource);
  auto flags = Parameter<Smi>(Descriptor::kFlags);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto maybe_vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);
  TVARIABLE(Map, result_map);
  Label if_result_map(this, &result_map), if_empty_object(this),
      miss(this, Label::kDeferred), try_polymorphic(this, Label::kDeferred),
      try_megamorphic(this, Label::kDeferred), slow(this, Label::kDeferred);

  TNode<Map> source_map = LoadReceiverMap(source);
  GotoIf(IsDeprecatedMap(source_map), &miss);

  GotoIf(IsUndefined(maybe_vector), &miss);

  TNode<HeapObjectReference> feedback;
  TNode<HeapObjectReference> weak_source_map = MakeWeak(source_map);

  // Decide if monomorphic or polymorphic, then dispatch based on the handler.
  {
    TVARIABLE(MaybeObject, var_handler);
    Label if_handler(this, &var_handler);
    feedback = TryMonomorphicCase(slot, CAST(maybe_vector), weak_source_map,
                                  &if_handler, &var_handler, &try_polymorphic);

    BIND(&try_polymorphic);
    TNode<HeapObject> strong_feedback = GetHeapObjectIfStrong(feedback, &miss);
    {
      Comment("CloneObjectIC_try_polymorphic");
      GotoIfNot(IsWeakFixedArrayMap(LoadMap(strong_feedback)),
                &try_megamorphic);
      HandlePolymorphicCase(weak_source_map, CAST(strong_feedback), &if_handler,
                            &var_handler, &miss);
    }

    BIND(&try_megamorphic);
    {
      Comment("CloneObjectIC_try_megamorphic");
      CSA_DCHECK(
          this,
          Word32Or(TaggedEqual(strong_feedback, UninitializedSymbolConstant()),
                   TaggedEqual(strong_feedback, MegamorphicSymbolConstant())));
      GotoIfNot(TaggedEqual(strong_feedback, MegamorphicSymbolConstant()),
                &miss);
      Goto(&slow);
    }

    BIND(&if_handler);
    Comment("CloneObjectIC_if_handler");

    // When the result of cloning the object is an empty object literal we store
    // a Smi into the feedback.
    GotoIf(TaggedIsSmi(var_handler.value()), &if_empty_object);

    // Handlers for the CloneObjectIC stub are weak references to the Map of
    // a result object.
    result_map = CAST(GetHeapObjectAssumeWeak(var_handler.value(), &miss));
    GotoIf(IsDeprecatedMap(result_map.value()), &miss);
    Goto(&if_result_map);
  }

  // Cloning with a concrete result_map.
  {
    BIND(&if_result_map);
    Comment("CloneObjectIC_if_result_map");

    TNode<Object> object = FastCloneJSObject(
        CAST(source), source_map, result_map.value(),
        [&](TNode<Map> map, TNode<HeapObject> properties,
            TNode<FixedArray> elements) {
          return UncheckedCast<JSObject>(AllocateJSObjectFromMap(
              map, properties, elements, AllocationFlag::kNone,
              SlackTrackingMode::kDontInitializeInObjectProperties));
        },
        true /* target_is_new */);

    Return(object);
  }

  // Case for when the result is the empty object literal. Can't be shared with
  // the above since we must initialize the in-object properties.
  {
    BIND(&if_empty_object);
    Comment("CloneObjectIC_if_empty_object");
    TNode<NativeContext> native_context = LoadNativeContext(context);
    TNode<Map> initial_map = LoadObjectFunctionInitialMap(native_context);
    TNode<JSObject> object =
        UncheckedCast<JSObject>(AllocateJSObjectFromMap(initial_map, {}, {}));
    Return(object);
  }

  BIND(&slow);
  {
    TailCallBuiltin(Builtin::kCloneObjectIC_Slow, context, source, flags, slot,
                    maybe_vector);
  }

  BIND(&miss);
  {
    Comment("CloneObjectIC_miss");
    TNode<HeapObject> map_or_result =
        CAST(CallRuntime(Runtime::kCloneObjectIC_Miss, context, source, flags,
                         slot, maybe_vector));
    Label restart(this);
    GotoIf(IsMap(map_or_result), &restart);
    CSA_DCHECK(this, IsJSObject(map_or_result));
    Return(map_or_result);

    BIND(&restart);
    result_map = CAST(map_or_result);
    Goto(&if_result_map);
  }
}

void AccessorAssembler::GenerateKeyedHasIC() {
  using Descriptor = KeyedHasICWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  LoadICParameters p(context, receiver, name, slot, vector);
  KeyedLoadIC(&p, LoadAccessMode::kHas);
}

void AccessorAssembler::GenerateKeyedHasICBaseline() {
  using Descriptor = KeyedHasICBaselineDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  TNode<FeedbackVector> vector = LoadFeedbackVectorFromBaseline();
  TNode<Context> context = LoadContextFromBaseline();

  TailCallBuiltin(Builtin::kKeyedHasIC, context, receiver, name, slot, vector);
}

void AccessorAssembler::GenerateKeyedHasIC_Megamorphic() {
  using Descriptor = KeyedHasICWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto context = Parameter<Context>(Descriptor::kContext);
  // TODO(magardn): implement HasProperty handling in KeyedLoadICGeneric
  Return(HasProperty(context, receiver, name,
                     HasPropertyLookupMode::kHasProperty));
}

void AccessorAssembler::GenerateKeyedHasIC_PolymorphicName() {
  using Descriptor = LoadWithVectorDescriptor;

  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto name = Parameter<Object>(Descriptor::kName);
  auto slot = Parameter<TaggedIndex>(Descriptor::kSlot);
  auto vector = Parameter<HeapObject>(Descriptor::kVector);
  auto context = Parameter<Context>(Descriptor::kContext);

  LoadICParameters p(context, receiver, name, slot, vector);
  KeyedLoadICPolymorphicName(&p, LoadAccessMode::kHas);
}

void AccessorAssembler::BranchIfPrototypesHaveNoElements(
    TNode<Map> receiver_map, Label* definitely_no_elements,
    Label* possibly_elements) {
  TVARIABLE(Map, var_map, receiver_map);
  Label loop_body(this, &var_map);
  TNode<FixedArray> empty_fixed_array = EmptyFixedArrayConstant();
  TNode<NumberDictionary> empty_slow_element_dictionary =
      EmptySlowElementDictionaryConstant();
  Goto(&loop_body);

  BIND(&loop_body);
  {
    TNode<Map> map = var_map.value();
    TNode<HeapObject> prototype = LoadMapPrototype(map);
    GotoIf(IsNull(prototype), definitely_no_elements);
    TNode<Map> prototype_map = LoadMap(prototype);
    TNode<Uint16T> prototype_instance_type = LoadMapInstanceType(prototype_map);

    // Pessimistically assume elements if a Proxy, Special API Object,
    // or JSPrimitiveWrapper wrapper is found on the prototype chain. After this
    // instance type check, it's not necessary to check for interceptors or
    // access checks.
    Label if_custom(this, Label::kDeferred), if_notcustom(this);
    Branch(IsCustomElementsReceiverInstanceType(prototype_instance_type),
           &if_custom, &if_notcustom);

    BIND(&if_custom);
    {
      // For string JSPrimitiveWrapper wrappers we still support the checks as
      // long as they wrap the empty string.
      GotoIfNot(
          InstanceTypeEqual(prototype_instance_type, JS_PRIMITIVE_WRAPPER_TYPE),
          possibly_elements);
      TNode<Object> prototype_value =
          LoadJSPrimitiveWrapperValue(CAST(prototype));
      Branch(IsEmptyString(prototype_value), &if_notcustom, possibly_elements);
    }

    BIND(&if_notcustom);
    {
      TNode<FixedArrayBase> prototype_elements = LoadElements(CAST(prototype));
      var_map = prototype_map;
      GotoIf(TaggedEqual(prototype_elements, empty_fixed_array), &loop_body);
      Branch(TaggedEqual(prototype_elements, empty_slow_element_dictionary),
             &loop_body, possibly_elements);
    }
  }
}

#undef LOAD_KIND
#undef STORE_KIND

}  // namespace internal
}  // namespace v8
