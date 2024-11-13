// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_
#define V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_

#include <functional>

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-stub-assembler.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

template <typename TCallable, class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<TCallable> callable,
                                      ConvertReceiverMode mode,
                                      TNode<Object> receiver, TArgs... args) {
  static_assert(std::is_same<Object, TCallable>::value ||
                std::is_base_of<HeapObject, TCallable>::value);
  static_assert(!std::is_base_of<JSFunction, TCallable>::value,
                "Use CallFunction() when the callable is a JSFunction.");

  if (IsUndefinedConstant(receiver) || IsNullConstant(receiver)) {
    DCHECK_NE(mode, ConvertReceiverMode::kNotNullOrUndefined);
    return CallJS(Builtins::Call(ConvertReceiverMode::kNullOrUndefined),
                  context, callable, /* new_target */ {}, receiver, args...);
  }
  DCheckReceiver(mode, receiver);
  return CallJS(Builtins::Call(mode), context, callable,
                /* new_target */ {}, receiver, args...);
}

template <typename TCallable, class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<TCallable> callable,
                                      TNode<JSReceiver> receiver,
                                      TArgs... args) {
  return Call(context, callable, ConvertReceiverMode::kNotNullOrUndefined,
              receiver, args...);
}

template <typename TCallable, class... TArgs>
TNode<Object> CodeStubAssembler::Call(TNode<Context> context,
                                      TNode<TCallable> callable,
                                      TNode<Object> receiver, TArgs... args) {
  return Call(context, callable, ConvertReceiverMode::kAny, receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              ConvertReceiverMode mode,
                                              TNode<Object> receiver,
                                              TArgs... args) {
  if (IsUndefinedConstant(receiver) || IsNullConstant(receiver)) {
    DCHECK_NE(mode, ConvertReceiverMode::kNotNullOrUndefined);
    return CallJS(Builtins::CallFunction(ConvertReceiverMode::kNullOrUndefined),
                  context, callable, /* new_target */ {}, receiver, args...);
  }
  DCheckReceiver(mode, receiver);
  return CallJS(Builtins::CallFunction(mode), context, callable,
                /* new_target */ {}, receiver, args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              TNode<JSReceiver> receiver,
                                              TArgs... args) {
  return CallFunction(context, callable,
                      ConvertReceiverMode::kNotNullOrUndefined, receiver,
                      args...);
}

template <class... TArgs>
TNode<Object> CodeStubAssembler::CallFunction(TNode<Context> context,
                                              TNode<JSFunction> callable,
                                              TNode<Object> receiver,
                                              TArgs... args) {
  return CallFunction(context, callable, ConvertReceiverMode::kAny, receiver,
                      args...);
}

template <typename Function>
TNode<Object> CodeStubAssembler::FastCloneJSObject(
    TNode<HeapObject> object, TNode<Map> source_map, TNode<Map> target_map,
    const Function& materialize_target, bool target_is_new) {
  Label done_copy_properties(this), done_copy_elements(this);

  // This macro only suport JSObjects.
  CSA_DCHECK(this, InstanceTypeEqual(LoadInstanceType(object), JS_OBJECT_TYPE));
  CSA_DCHECK(this, IsStrong(TNode<MaybeObject>(target_map)));
  CSA_DCHECK(
      this, InstanceTypeEqual(LoadMapInstanceType(target_map), JS_OBJECT_TYPE));
  // We do not want to deal with slack-tracking here.
  CSA_DCHECK(this, IsNotSetWord32<Map::Bits3::ConstructionCounterBits>(
                       LoadMapBitField3(source_map)));
  CSA_DCHECK(this, IsNotSetWord32<Map::Bits3::ConstructionCounterBits>(
                       LoadMapBitField3(target_map)));

  TVARIABLE(HeapObject, var_properties, EmptyFixedArrayConstant());
  TVARIABLE(FixedArray, var_elements, EmptyFixedArrayConstant());

  // Copy the PropertyArray backing store. The source PropertyArray
  // must be either an Smi, or a PropertyArray.
  Comment("FastCloneJSObject: cloning properties");
  TNode<Object> source_properties =
      LoadObjectField(object, JSObject::kPropertiesOrHashOffset);
  {
    GotoIf(TaggedIsSmi(source_properties), &done_copy_properties);
    GotoIf(IsEmptyFixedArray(source_properties), &done_copy_properties);

    // This fastcase requires that the source object has fast properties.
    TNode<PropertyArray> source_property_array = CAST(source_properties);

    TNode<IntPtrT> length = LoadPropertyArrayLength(source_property_array);
    GotoIf(IntPtrEqual(length, IntPtrConstant(0)), &done_copy_properties);

    TNode<PropertyArray> property_array = AllocatePropertyArray(length);
    FillPropertyArrayWithUndefined(property_array, IntPtrConstant(0), length);
    CopyPropertyArrayValues(source_property_array, property_array, length,
                            SKIP_WRITE_BARRIER, DestroySource::kNo);
    var_properties = property_array;
  }

  Goto(&done_copy_properties);
  BIND(&done_copy_properties);

  Comment("FastCloneJSObject: cloning elements");
  TNode<FixedArrayBase> source_elements = LoadElements(CAST(object));
  GotoIf(TaggedEqual(source_elements, EmptyFixedArrayConstant()),
         &done_copy_elements);
  var_elements = CAST(CloneFixedArray(
      source_elements, ExtractFixedArrayFlag::kAllFixedArraysDontCopyCOW));

  Goto(&done_copy_elements);
  BIND(&done_copy_elements);

  Comment("FastCloneJSObject: initialize the target object");
  TNode<JSReceiver> target = materialize_target(
      target_map, var_properties.value(), var_elements.value());

  // Lastly, clone any in-object properties.
#ifdef DEBUG
  {
    TNode<IntPtrT> source_used_instance_size =
        MapUsedInstanceSizeInWords(source_map);
    TNode<IntPtrT> target_used_instance_size =
        MapUsedInstanceSizeInWords(target_map);
    TNode<IntPtrT> source_inobject_properties_start =
        LoadMapInobjectPropertiesStartInWords(source_map);
    TNode<IntPtrT> target_inobject_properties_start =
        LoadMapInobjectPropertiesStartInWords(target_map);
    CSA_DCHECK(this, IntPtrEqual(IntPtrSub(target_used_instance_size,
                                           target_inobject_properties_start),
                                 IntPtrSub(source_used_instance_size,
                                           source_inobject_properties_start)));
  }
#endif  // DEBUG

  // 1) Initialize unused in-object properties.
  Comment("FastCloneJSObject: initializing unused in-object properties");
  TNode<IntPtrT> target_used_payload_end =
      TimesTaggedSize(MapUsedInstanceSizeInWords(target_map));
  TNode<IntPtrT> target_payload_end =
      TimesTaggedSize(LoadMapInstanceSizeInWords(target_map));
  InitializeFieldsWithRoot(target, target_used_payload_end, target_payload_end,
                           RootIndex::kUndefinedValue);

  // 2) Copy all used in-object properties.
  Comment("FastCloneJSObject: copying used in-object properties");
  TNode<IntPtrT> source_payload_start =
      TimesTaggedSize(LoadMapInobjectPropertiesStartInWords(source_map));
  TNode<IntPtrT> target_payload_start =
      TimesTaggedSize(LoadMapInobjectPropertiesStartInWords(target_map));
  TNode<IntPtrT> field_offset_difference =
      IntPtrSub(source_payload_start, target_payload_start);

  Label done_copy_used(this);
  auto EmitCopyLoop = [&](bool write_barrier) {
    if (write_barrier) {
      Comment(
          "FastCloneJSObject: copying used in-object properties with write "
          "barrier");
    } else {
      Comment(
          "FastCloneJSObject: copying used in-object properties without write "
          "barrier");
    }
    BuildFastLoop<IntPtrT>(
        target_payload_start, target_used_payload_end,
        [&](TNode<IntPtrT> result_offset) {
          TNode<IntPtrT> source_offset =
              IntPtrSub(result_offset, field_offset_difference);
          if (write_barrier) {
            TNode<Object> field = LoadObjectField(object, source_offset);
            StoreObjectField(target, result_offset, field);
          } else {
            TNode<TaggedT> field =
                LoadObjectField<TaggedT>(object, source_offset);
            StoreObjectFieldNoWriteBarrier(target, result_offset, field);
          }
        },
        kTaggedSize, LoopUnrollingMode::kYes, IndexAdvanceMode::kPost);
  };

  if (!target_is_new) {
    Label if_no_write_barrier(this),
        if_needs_write_barrier(this, Label::kDeferred);

    TNode<BoolT> needs_write_barrier = IsPageFlagReset(
        BitcastTaggedToWord(target), MemoryChunk::kIsInYoungGenerationMask);
    Branch(needs_write_barrier, &if_needs_write_barrier, &if_no_write_barrier);

    BIND(&if_needs_write_barrier);
    EmitCopyLoop(true);

    Goto(&done_copy_used);
    BIND(&if_no_write_barrier);
  }

  EmitCopyLoop(false);
  Goto(&done_copy_used);

  BIND(&done_copy_used);

  // 3) Duplicate heap number boxes if needed.
  // We need to go through the {object} again here and properly clone
  // them. We use a second loop here to ensure that the GC (and heap
  // verifier) always sees properly initialized objects, i.e. never
  // hits undefined values in double fields.
  Comment("FastCloneJSObject: cloning heap numbers");
  ConstructorBuiltinsAssembler(state()).CopyMutableHeapNumbersInObject(
      target, target_payload_start, target_used_payload_end);

  return target;
}

}  // namespace internal
}  // namespace v8
#endif  // V8_CODEGEN_CODE_STUB_ASSEMBLER_INL_H_
