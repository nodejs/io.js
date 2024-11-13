// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-collections-gen.h"

#include "src/builtins/builtins-constructor-gen.h"
#include "src/builtins/builtins-iterator-gen.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/execution/protectors.h"
#include "src/heap/factory-inl.h"
#include "src/heap/heap-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/objects/js-collection.h"
#include "src/objects/ordered-hash-table.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

template <class T>
using TVariable = compiler::TypedCodeAssemblerVariable<T>;

void BaseCollectionsAssembler::AddConstructorEntry(
    Variant variant, TNode<Context> context, TNode<Object> collection,
    TNode<Object> add_function, TNode<Object> key_value,
    Label* if_may_have_side_effects, Label* if_exception,
    TVariable<Object>* var_exception) {
  compiler::ScopedExceptionHandler handler(this, if_exception, var_exception);
  CSA_DCHECK(this, Word32BinaryNot(IsHashTableHole(key_value)));
  if (variant == kMap || variant == kWeakMap) {
    TorqueStructKeyValuePair pair =
        if_may_have_side_effects != nullptr
            ? LoadKeyValuePairNoSideEffects(context, key_value,
                                            if_may_have_side_effects)
            : LoadKeyValuePair(context, key_value);
    TNode<Object> key_n = pair.key;
    TNode<Object> value_n = pair.value;
    Call(context, add_function, collection, key_n, value_n);
  } else {
    DCHECK(variant == kSet || variant == kWeakSet);
    Call(context, add_function, collection, key_value);
  }
}

void BaseCollectionsAssembler::AddConstructorEntries(
    Variant variant, TNode<Context> context,
    TNode<NativeContext> native_context, TNode<HeapObject> collection,
    TNode<Object> initial_entries) {
  CSA_DCHECK(this, Word32BinaryNot(IsNullOrUndefined(initial_entries)));

  enum Mode { kSlow, kFastJSArray, kFastCollection };
  TVARIABLE(IntPtrT, var_at_least_space_for, IntPtrConstant(0));
  TVARIABLE(HeapObject, var_entries_table, UndefinedConstant());
  TVARIABLE(Int32T, var_mode, Int32Constant(kSlow));
  Label if_fast_js_array(this), allocate_table(this);

  // The slow path is taken if the initial add function is modified. This check
  // must precede the kSet fast path below, which has the side effect of
  // exhausting {initial_entries} if it is a JSSetIterator.
  GotoIfInitialAddFunctionModified(variant, native_context, collection,
                                   &allocate_table);

  GotoIf(IsFastJSArrayWithNoCustomIteration(context, initial_entries),
         &if_fast_js_array);
  if (variant == Variant::kSet) {
    GetEntriesIfFastCollectionOrIterable(
        variant, initial_entries, context, &var_entries_table,
        &var_at_least_space_for, &allocate_table);
    var_mode = Int32Constant(kFastCollection);
    Goto(&allocate_table);
  } else {
    Goto(&allocate_table);
  }
  BIND(&if_fast_js_array);
  {
    var_mode = Int32Constant(kFastJSArray);
    if (variant == kWeakSet || variant == kWeakMap) {
      var_at_least_space_for =
          PositiveSmiUntag(LoadFastJSArrayLength(CAST(initial_entries)));
    } else {
      // TODO(ishell): consider using array length for all collections
      static_assert(OrderedHashSet::kInitialCapacity ==
                    OrderedHashMap::kInitialCapacity);
      var_at_least_space_for = IntPtrConstant(OrderedHashSet::kInitialCapacity);
    }
    Goto(&allocate_table);
  }
  TVARIABLE(JSReceiver, var_iterator_object);
  TVARIABLE(Object, var_exception);
  Label exit(this), from_fast_jsarray(this), from_fast_collection(this),
      slow_loop(this, Label::kDeferred), if_exception(this, Label::kDeferred);
  BIND(&allocate_table);
  {
    TNode<HeapObject> table =
        AllocateTable(variant, var_at_least_space_for.value());
    StoreObjectField(collection, GetTableOffset(variant), table);
    if (variant == Variant::kSet) {
      GotoIf(Word32Equal(var_mode.value(), Int32Constant(kFastCollection)),
             &from_fast_collection);
    }
    Branch(Word32Equal(var_mode.value(), Int32Constant(kFastJSArray)),
           &from_fast_jsarray, &slow_loop);
  }
  BIND(&from_fast_jsarray);
  {
    Label if_exception_during_fast_iteration(this, Label::kDeferred);
    TVARIABLE(IntPtrT, var_index, IntPtrConstant(0));
    TNode<JSArray> initial_entries_jsarray =
        UncheckedCast<JSArray>(initial_entries);
#if DEBUG
    CSA_DCHECK(this, IsFastJSArrayWithNoCustomIteration(
                         context, initial_entries_jsarray));
    TNode<Map> original_initial_entries_map = LoadMap(initial_entries_jsarray);
#endif

    Label if_may_have_side_effects(this, Label::kDeferred);
    {
      compiler::ScopedExceptionHandler handler(
          this, &if_exception_during_fast_iteration, &var_exception);
      AddConstructorEntriesFromFastJSArray(
          variant, context, native_context, collection, initial_entries_jsarray,
          &if_may_have_side_effects, var_index);
    }
    Goto(&exit);

    if (variant == kMap || variant == kWeakMap) {
      BIND(&if_may_have_side_effects);
#if DEBUG
      {
        // Check that add/set function has not been modified.
        Label if_not_modified(this), if_modified(this);
        GotoIfInitialAddFunctionModified(variant, native_context, collection,
                                         &if_modified);
        Goto(&if_not_modified);
        BIND(&if_modified);
        Unreachable();
        BIND(&if_not_modified);
      }
      CSA_DCHECK(this, TaggedEqual(original_initial_entries_map,
                                   LoadMap(initial_entries_jsarray)));
#endif
      var_mode = Int32Constant(kSlow);
      Goto(&allocate_table);
    }
    BIND(&if_exception_during_fast_iteration);
    {
      // In case exception is thrown during collection population, materialize
      // the iteator and execute iterator closing protocol. It might be
      // non-trivial in case "return" callback is added somewhere in the
      // iterator's prototype chain.
      TNode<NativeContext> native_context = LoadNativeContext(context);
      TNode<IntPtrT> next_index =
          IntPtrAdd(var_index.value(), IntPtrConstant(1));
      var_iterator_object = CreateArrayIterator(
          native_context, UncheckedCast<JSArray>(initial_entries),
          IterationKind::kValues, SmiTag(next_index));
      Goto(&if_exception);
    }
  }
  if (variant == Variant::kSet) {
    BIND(&from_fast_collection);
    {
      AddConstructorEntriesFromFastCollection(variant, collection,
                                              var_entries_table.value());
      Goto(&exit);
    }
  }
  BIND(&slow_loop);
  {
    AddConstructorEntriesFromIterable(
        variant, context, native_context, collection, initial_entries,
        &if_exception, &var_iterator_object, &var_exception);
    Goto(&exit);
  }
  BIND(&if_exception);
  {
    TNode<HeapObject> message = GetPendingMessage();
    SetPendingMessage(TheHoleConstant());
    // iterator.next field is not used by IteratorCloseOnException.
    TorqueStructIteratorRecord iterator = {var_iterator_object.value(), {}};
    IteratorCloseOnException(context, iterator);
    CallRuntime(Runtime::kReThrowWithMessage, context, var_exception.value(),
                message);
    Unreachable();
  }
  BIND(&exit);
}

void BaseCollectionsAssembler::AddConstructorEntriesFromFastJSArray(
    Variant variant, TNode<Context> context, TNode<Context> native_context,
    TNode<Object> collection, TNode<JSArray> fast_jsarray,
    Label* if_may_have_side_effects, TVariable<IntPtrT>& var_current_index) {
  TNode<FixedArrayBase> elements = LoadElements(fast_jsarray);
  TNode<Int32T> elements_kind = LoadElementsKind(fast_jsarray);
  TNode<JSFunction> add_func = GetInitialAddFunction(variant, native_context);
  CSA_DCHECK(this,
             TaggedEqual(GetAddFunction(variant, native_context, collection),
                         add_func));
  CSA_DCHECK(this, IsFastJSArrayWithNoCustomIteration(context, fast_jsarray));
  TNode<IntPtrT> length = PositiveSmiUntag(LoadFastJSArrayLength(fast_jsarray));
  CSA_DCHECK(
      this, HasInitialCollectionPrototype(variant, native_context, collection));

#if DEBUG
  TNode<Map> original_collection_map = LoadMap(CAST(collection));
  TNode<Map> original_fast_js_array_map = LoadMap(fast_jsarray);
#endif
  Label exit(this), if_doubles(this), if_smiorobjects(this);
  GotoIf(IntPtrEqual(length, IntPtrConstant(0)), &exit);
  Branch(IsFastSmiOrTaggedElementsKind(elements_kind), &if_smiorobjects,
         &if_doubles);
  BIND(&if_smiorobjects);
  {
    auto set_entry = [&](TNode<IntPtrT> index) {
      TNode<Object> element =
          LoadAndNormalizeFixedArrayElement(CAST(elements), index);
      AddConstructorEntry(variant, context, collection, add_func, element,
                          if_may_have_side_effects);
    };

    // Instead of using the slower iteration protocol to iterate over the
    // elements, a fast loop is used.  This assumes that adding an element
    // to the collection does not call user code that could mutate the elements
    // or collection.
    BuildFastLoop<IntPtrT>(var_current_index, IntPtrConstant(0), length,
                           set_entry, 1, LoopUnrollingMode::kNo,
                           IndexAdvanceMode::kPost);
    Goto(&exit);
  }
  BIND(&if_doubles);
  {
    // A Map constructor requires entries to be arrays (ex. [key, value]),
    // so a FixedDoubleArray can never succeed.
    if (variant == kMap || variant == kWeakMap) {
      CSA_DCHECK(this, IntPtrGreaterThan(length, IntPtrConstant(0)));
      TNode<Object> element =
          LoadAndNormalizeFixedDoubleArrayElement(elements, IntPtrConstant(0));
      ThrowTypeError(context, MessageTemplate::kIteratorValueNotAnObject,
                     element);
    } else {
      DCHECK(variant == kSet || variant == kWeakSet);
      auto set_entry = [&](TNode<IntPtrT> index) {
        TNode<Object> entry = LoadAndNormalizeFixedDoubleArrayElement(
            elements, UncheckedCast<IntPtrT>(index));
        AddConstructorEntry(variant, context, collection, add_func, entry);
      };
      BuildFastLoop<IntPtrT>(var_current_index, IntPtrConstant(0), length,
                             set_entry, 1, LoopUnrollingMode::kNo,
                             IndexAdvanceMode::kPost);
      Goto(&exit);
    }
  }
  BIND(&exit);
#if DEBUG
  CSA_DCHECK(this,
             TaggedEqual(original_collection_map, LoadMap(CAST(collection))));
  CSA_DCHECK(this,
             TaggedEqual(original_fast_js_array_map, LoadMap(fast_jsarray)));
#endif
}

void BaseCollectionsAssembler::AddConstructorEntriesFromIterable(
    Variant variant, TNode<Context> context, TNode<Context> native_context,
    TNode<Object> collection, TNode<Object> iterable, Label* if_exception,
    TVariable<JSReceiver>* var_iterator_object,
    TVariable<Object>* var_exception) {
  Label exit(this), loop(this);
  CSA_DCHECK(this, Word32BinaryNot(IsNullOrUndefined(iterable)));
  TNode<Object> add_func = GetAddFunction(variant, context, collection);
  IteratorBuiltinsAssembler iterator_assembler(this->state());
  TorqueStructIteratorRecord iterator =
      iterator_assembler.GetIterator(context, iterable);
  *var_iterator_object = iterator.object;

  CSA_DCHECK(this, Word32BinaryNot(IsUndefined(iterator.object)));

  TNode<Map> fast_iterator_result_map = CAST(
      LoadContextElement(native_context, Context::ITERATOR_RESULT_MAP_INDEX));

  Goto(&loop);
  BIND(&loop);
  {
    TNode<JSReceiver> next = iterator_assembler.IteratorStep(
        context, iterator, &exit, fast_iterator_result_map);
    TNode<Object> next_value = iterator_assembler.IteratorValue(
        context, next, fast_iterator_result_map);
    AddConstructorEntry(variant, context, collection, add_func, next_value,
                        nullptr, if_exception, var_exception);
    Goto(&loop);
  }
  BIND(&exit);
}

RootIndex BaseCollectionsAssembler::GetAddFunctionNameIndex(Variant variant) {
  switch (variant) {
    case kMap:
    case kWeakMap:
      return RootIndex::kset_string;
    case kSet:
    case kWeakSet:
      return RootIndex::kadd_string;
  }
  UNREACHABLE();
}

void BaseCollectionsAssembler::GotoIfInitialAddFunctionModified(
    Variant variant, TNode<NativeContext> native_context,
    TNode<HeapObject> collection, Label* if_modified) {
  static_assert(JSCollection::kAddFunctionDescriptorIndex ==
                JSWeakCollection::kAddFunctionDescriptorIndex);

  // TODO(jgruber): Investigate if this should also fall back to full prototype
  // verification.
  static constexpr PrototypeCheckAssembler::Flags flags{
      PrototypeCheckAssembler::kCheckPrototypePropertyConstness};

  static constexpr int kNoContextIndex = -1;
  static_assert(
      (flags & PrototypeCheckAssembler::kCheckPrototypePropertyIdentity) == 0);

  using DescriptorIndexNameValue =
      PrototypeCheckAssembler::DescriptorIndexNameValue;

  DescriptorIndexNameValue property_to_check{
      JSCollection::kAddFunctionDescriptorIndex,
      GetAddFunctionNameIndex(variant), kNoContextIndex};

  PrototypeCheckAssembler prototype_check_assembler(
      state(), flags, native_context,
      GetInitialCollectionPrototype(variant, native_context),
      base::Vector<DescriptorIndexNameValue>(&property_to_check, 1));

  TNode<HeapObject> prototype = LoadMapPrototype(LoadMap(collection));
  Label if_unmodified(this);
  prototype_check_assembler.CheckAndBranch(prototype, &if_unmodified,
                                           if_modified);

  BIND(&if_unmodified);
}

TNode<JSObject> BaseCollectionsAssembler::AllocateJSCollection(
    TNode<Context> context, TNode<JSFunction> constructor,
    TNode<JSReceiver> new_target) {
  TNode<BoolT> is_target_unmodified = TaggedEqual(constructor, new_target);

  return Select<JSObject>(
      is_target_unmodified,
      [=, this] { return AllocateJSCollectionFast(constructor); },
      [=, this] {
        return AllocateJSCollectionSlow(context, constructor, new_target);
      });
}

TNode<JSObject> BaseCollectionsAssembler::AllocateJSCollectionFast(
    TNode<JSFunction> constructor) {
  CSA_DCHECK(this, IsConstructorMap(LoadMap(constructor)));
  TNode<Map> initial_map =
      CAST(LoadJSFunctionPrototypeOrInitialMap(constructor));
  return AllocateJSObjectFromMap(initial_map);
}

TNode<JSObject> BaseCollectionsAssembler::AllocateJSCollectionSlow(
    TNode<Context> context, TNode<JSFunction> constructor,
    TNode<JSReceiver> new_target) {
  ConstructorBuiltinsAssembler constructor_assembler(this->state());
  return constructor_assembler.FastNewObject(context, constructor, new_target);
}

void BaseCollectionsAssembler::GenerateConstructor(
    Variant variant, Handle<String> constructor_function_name,
    TNode<Object> new_target, TNode<IntPtrT> argc, TNode<Context> context) {
  const int kIterableArg = 0;
  CodeStubArguments args(this, argc);
  TNode<Object> iterable = args.GetOptionalArgumentValue(kIterableArg);

  Label if_undefined(this, Label::kDeferred);
  GotoIf(IsUndefined(new_target), &if_undefined);

  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<JSObject> collection = AllocateJSCollection(
      context, GetConstructor(variant, native_context), CAST(new_target));

  Label add_constructor_entries(this);

  // The empty case.
  //
  // This is handled specially to simplify AddConstructorEntries, which is
  // complex and contains multiple fast paths.
  GotoIfNot(IsNullOrUndefined(iterable), &add_constructor_entries);
  TNode<HeapObject> table = AllocateTable(variant, IntPtrConstant(0));
  StoreObjectField(collection, GetTableOffset(variant), table);
  Return(collection);

  BIND(&add_constructor_entries);
  AddConstructorEntries(variant, context, native_context, collection, iterable);
  Return(collection);

  BIND(&if_undefined);
  ThrowTypeError(context, MessageTemplate::kConstructorNotFunction,
                 HeapConstantNoHole(constructor_function_name));
}

TNode<Object> BaseCollectionsAssembler::GetAddFunction(
    Variant variant, TNode<Context> context, TNode<Object> collection) {
  Handle<String> add_func_name = (variant == kMap || variant == kWeakMap)
                                     ? isolate()->factory()->set_string()
                                     : isolate()->factory()->add_string();
  TNode<Object> add_func = GetProperty(context, collection, add_func_name);

  Label exit(this), if_notcallable(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(add_func), &if_notcallable);
  GotoIfNot(IsCallable(CAST(add_func)), &if_notcallable);
  Goto(&exit);

  BIND(&if_notcallable);
  ThrowTypeError(context, MessageTemplate::kPropertyNotFunction, add_func,
                 HeapConstantNoHole(add_func_name), collection);

  BIND(&exit);
  return add_func;
}

TNode<JSFunction> BaseCollectionsAssembler::GetConstructor(
    Variant variant, TNode<Context> native_context) {
  int index;
  switch (variant) {
    case kMap:
      index = Context::JS_MAP_FUN_INDEX;
      break;
    case kSet:
      index = Context::JS_SET_FUN_INDEX;
      break;
    case kWeakMap:
      index = Context::JS_WEAK_MAP_FUN_INDEX;
      break;
    case kWeakSet:
      index = Context::JS_WEAK_SET_FUN_INDEX;
      break;
  }
  return CAST(LoadContextElement(native_context, index));
}

TNode<JSFunction> BaseCollectionsAssembler::GetInitialAddFunction(
    Variant variant, TNode<Context> native_context) {
  int index;
  switch (variant) {
    case kMap:
      index = Context::MAP_SET_INDEX;
      break;
    case kSet:
      index = Context::SET_ADD_INDEX;
      break;
    case kWeakMap:
      index = Context::WEAKMAP_SET_INDEX;
      break;
    case kWeakSet:
      index = Context::WEAKSET_ADD_INDEX;
      break;
  }
  return CAST(LoadContextElement(native_context, index));
}

int BaseCollectionsAssembler::GetTableOffset(Variant variant) {
  switch (variant) {
    case kMap:
      return JSMap::kTableOffset;
    case kSet:
      return JSSet::kTableOffset;
    case kWeakMap:
      return JSWeakMap::kTableOffset;
    case kWeakSet:
      return JSWeakSet::kTableOffset;
  }
  UNREACHABLE();
}

// https://tc39.es/ecma262/#sec-canbeheldweakly
void BaseCollectionsAssembler::GotoIfCannotBeHeldWeakly(
    const TNode<Object> obj, Label* if_cannot_be_held_weakly) {
  Label check_symbol_key(this);
  Label end(this);
  GotoIf(TaggedIsSmi(obj), if_cannot_be_held_weakly);
  TNode<Uint16T> instance_type = LoadMapInstanceType(LoadMap(CAST(obj)));
  GotoIfNot(IsJSReceiverInstanceType(instance_type), &check_symbol_key);
  // TODO(v8:12547) Shared structs and arrays should only be able to point
  // to shared values in weak collections. For now, disallow them as weak
  // collection keys.
  GotoIf(IsAlwaysSharedSpaceJSObjectInstanceType(instance_type),
         if_cannot_be_held_weakly);
  Goto(&end);
  Bind(&check_symbol_key);
  GotoIfNot(IsSymbolInstanceType(instance_type), if_cannot_be_held_weakly);
  TNode<Uint32T> flags = LoadSymbolFlags(CAST(obj));
  GotoIf(Word32And(flags, Symbol::IsInPublicSymbolTableBit::kMask),
         if_cannot_be_held_weakly);
  Goto(&end);
  Bind(&end);
}

TNode<Map> BaseCollectionsAssembler::GetInitialCollectionPrototype(
    Variant variant, TNode<Context> native_context) {
  int initial_prototype_index;
  switch (variant) {
    case kMap:
      initial_prototype_index = Context::INITIAL_MAP_PROTOTYPE_MAP_INDEX;
      break;
    case kSet:
      initial_prototype_index = Context::INITIAL_SET_PROTOTYPE_MAP_INDEX;
      break;
    case kWeakMap:
      initial_prototype_index = Context::INITIAL_WEAKMAP_PROTOTYPE_MAP_INDEX;
      break;
    case kWeakSet:
      initial_prototype_index = Context::INITIAL_WEAKSET_PROTOTYPE_MAP_INDEX;
      break;
  }
  return CAST(LoadContextElement(native_context, initial_prototype_index));
}

TNode<BoolT> BaseCollectionsAssembler::HasInitialCollectionPrototype(
    Variant variant, TNode<Context> native_context, TNode<Object> collection) {
  TNode<Map> collection_proto_map =
      LoadMap(LoadMapPrototype(LoadMap(CAST(collection))));

  return TaggedEqual(collection_proto_map,
                     GetInitialCollectionPrototype(variant, native_context));
}

TNode<Object> BaseCollectionsAssembler::LoadAndNormalizeFixedArrayElement(
    TNode<FixedArray> elements, TNode<IntPtrT> index) {
  TNode<Object> element = UnsafeLoadFixedArrayElement(elements, index);
  return Select<Object>(
      IsTheHole(element), [=, this] { return UndefinedConstant(); },
      [=] { return element; });
}

TNode<Object> BaseCollectionsAssembler::LoadAndNormalizeFixedDoubleArrayElement(
    TNode<HeapObject> elements, TNode<IntPtrT> index) {
  TVARIABLE(Object, entry);
  Label if_hole(this, Label::kDeferred), next(this);
  TNode<Float64T> element =
      LoadFixedDoubleArrayElement(CAST(elements), index, &if_hole);
  {  // not hole
    entry = AllocateHeapNumberWithValue(element);
    Goto(&next);
  }
  BIND(&if_hole);
  {
    entry = UndefinedConstant();
    Goto(&next);
  }
  BIND(&next);
  return entry.value();
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::FindOrderedHashTableEntry(
    const TNode<CollectionType> table, const TNode<Uint32T> hash,
    const std::function<void(TNode<Object>, Label*, Label*)>& key_compare,
    TVariable<IntPtrT>* entry_start_position, Label* entry_found,
    Label* not_found) {
  // Get the index of the bucket.
  const TNode<Uint32T> number_of_buckets =
      PositiveSmiToUint32(CAST(UnsafeLoadFixedArrayElement(
          table, CollectionType::NumberOfBucketsIndex())));
  const TNode<Uint32T> bucket =
      Word32And(hash, Uint32Sub(number_of_buckets, Uint32Constant(1)));
  const TNode<IntPtrT> first_entry = SmiUntag(CAST(UnsafeLoadFixedArrayElement(
      table, Signed(ChangeUint32ToWord(bucket)),
      CollectionType::HashTableStartIndex() * kTaggedSize)));
  const TNode<IntPtrT> number_of_buckets_intptr =
      Signed(ChangeUint32ToWord(number_of_buckets));

  // Walk the bucket chain.
  TNode<IntPtrT> entry_start;
  Label if_key_found(this);
  {
    TVARIABLE(IntPtrT, var_entry, first_entry);
    Label loop(this, {&var_entry, entry_start_position}),
        continue_next_entry(this);
    Goto(&loop);
    BIND(&loop);

    // If the entry index is the not-found sentinel, we are done.
    GotoIf(IntPtrEqual(var_entry.value(),
                       IntPtrConstant(CollectionType::kNotFound)),
           not_found);

    // Make sure the entry index is within range.
    CSA_DCHECK(
        this,
        UintPtrLessThan(
            var_entry.value(),
            PositiveSmiUntag(SmiAdd(
                CAST(UnsafeLoadFixedArrayElement(
                    table, CollectionType::NumberOfElementsIndex())),
                CAST(UnsafeLoadFixedArrayElement(
                    table, CollectionType::NumberOfDeletedElementsIndex()))))));

    // Compute the index of the entry relative to kHashTableStartIndex.
    entry_start =
        IntPtrAdd(IntPtrMul(var_entry.value(),
                            IntPtrConstant(CollectionType::kEntrySize)),
                  number_of_buckets_intptr);

    // Load the key from the entry.
    const TNode<Object> candidate_key =
        UnsafeLoadKeyFromOrderedHashTableEntry(table, entry_start);

    key_compare(candidate_key, &if_key_found, &continue_next_entry);

    BIND(&continue_next_entry);
    // Load the index of the next entry in the bucket chain.
    var_entry = SmiUntag(CAST(UnsafeLoadFixedArrayElement(
        table, entry_start,
        (CollectionType::HashTableStartIndex() + CollectionType::kChainOffset) *
            kTaggedSize)));

    Goto(&loop);
  }

  BIND(&if_key_found);
  *entry_start_position = entry_start;
  Goto(entry_found);
}

// a helper function to unwrap a fast js collection and load its length.
// var_entries_table is a variable meant to store the unwrapped collection.
// var_number_of_elements is a variable meant to store the length of the
// unwrapped collection. the function jumps to if_not_fast_collection if the
// collection is not a fast js collection.
void CollectionsBuiltinsAssembler::GetEntriesIfFastCollectionOrIterable(
    Variant variant, TNode<Object> initial_entries, TNode<Context> context,
    TVariable<HeapObject>* var_entries_table,
    TVariable<IntPtrT>* var_number_of_elements, Label* if_not_fast_collection) {
  Label if_fast_js_set(this), exit(this);
  DCHECK_EQ(variant, kSet);
  BranchIfIterableWithOriginalValueSetIterator(
      initial_entries, context, &if_fast_js_set, if_not_fast_collection);
  BIND(&if_fast_js_set);
  {
    *var_entries_table = SetOrSetIteratorToSet(initial_entries);
    TNode<Smi> size_smi = LoadObjectField<Smi>(
        var_entries_table->value(), OrderedHashMap::NumberOfElementsOffset());
    *var_number_of_elements = PositiveSmiUntag(size_smi);
    Goto(&exit);
  }
  BIND(&exit);
}

void CollectionsBuiltinsAssembler::AddConstructorEntriesFromSet(
    TNode<JSSet> collection, TNode<OrderedHashSet> table) {
  TNode<OrderedHashSet> entry_table = LoadObjectField<OrderedHashSet>(
      collection, GetTableOffset(Variant::kSet));

  TNode<IntPtrT> number_of_buckets =
      PositiveSmiUntag(CAST(UnsafeLoadFixedArrayElement(
          table, OrderedHashSet::NumberOfBucketsIndex())));
  TNode<IntPtrT> number_of_elements = LoadAndUntagPositiveSmiObjectField(
      table, OrderedHashSet::NumberOfElementsOffset());
  TNode<IntPtrT> number_of_deleted_elements = PositiveSmiUntag(CAST(
      LoadObjectField(table, OrderedHashSet::NumberOfDeletedElementsOffset())));
  TNode<IntPtrT> used_capacity =
      IntPtrAdd(number_of_elements, number_of_deleted_elements);
  TNode<IntPtrT> loop_bound = IntPtrAdd(
      IntPtrMul(used_capacity, IntPtrConstant(OrderedHashSet::kEntrySize)),
      number_of_buckets);

  TNode<IntPtrT> number_of_buckets_entry_table =
      PositiveSmiUntag(CAST(UnsafeLoadFixedArrayElement(
          entry_table, OrderedHashSet::NumberOfBucketsIndex())));

  TVARIABLE(Object, entry_key);
  TVARIABLE(IntPtrT, var_entry_table_occupancy, IntPtrConstant(0));
  VariableList loop_vars({&var_entry_table_occupancy}, zone());
  Label exit(this);

  auto set_entry = [&](TNode<IntPtrT> index) {
    entry_key = UnsafeLoadKeyFromOrderedHashTableEntry(table, index);
    Label if_key_is_not_hole(this), continue_loop(this);
    Branch(IsHashTableHole(entry_key.value()), &continue_loop,
           &if_key_is_not_hole);
    BIND(&if_key_is_not_hole);
    {
      AddNewToOrderedHashSet(entry_table, entry_key.value(),
                             number_of_buckets_entry_table,
                             var_entry_table_occupancy.value());
      Increment(&var_entry_table_occupancy, 1);
      Goto(&continue_loop);
    }
    BIND(&continue_loop);
    return;
  };

  // Instead of using the slower iteration protocol to iterate over the
  // elements, a fast loop is used.  This assumes that adding an element
  // to the collection does not call user code that could mutate the elements
  // or collection. The iteration is based on the layout of the ordered hash
  // table.
  BuildFastLoop<IntPtrT>(loop_vars, number_of_buckets, loop_bound, set_entry,
                         OrderedHashSet::kEntrySize, LoopUnrollingMode::kNo,
                         IndexAdvanceMode::kPost);
  Goto(&exit);
  BIND(&exit);
}

void CollectionsBuiltinsAssembler::AddConstructorEntriesFromFastCollection(
    Variant variant, TNode<HeapObject> collection,
    TNode<HeapObject> source_table) {
  if (variant == kSet) {
    AddConstructorEntriesFromSet(CAST(collection), CAST(source_table));
    return;
  }
}

template <typename IteratorType>
TNode<HeapObject> CollectionsBuiltinsAssembler::AllocateJSCollectionIterator(
    const TNode<Context> context, int map_index,
    const TNode<HeapObject> collection) {
  const TNode<Object> table =
      LoadObjectField(collection, JSCollection::kTableOffset);
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Map> iterator_map =
      CAST(LoadContextElement(native_context, map_index));
  const TNode<HeapObject> iterator =
      AllocateInNewSpace(IteratorType::kHeaderSize);
  StoreMapNoWriteBarrier(iterator, iterator_map);
  StoreObjectFieldRoot(iterator, IteratorType::kPropertiesOrHashOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldRoot(iterator, IteratorType::kElementsOffset,
                       RootIndex::kEmptyFixedArray);
  StoreObjectFieldNoWriteBarrier(iterator, IteratorType::kTableOffset, table);
  StoreObjectFieldNoWriteBarrier(iterator, IteratorType::kIndexOffset,
                                 SmiConstant(0));
  return iterator;
}

TNode<HeapObject> CollectionsBuiltinsAssembler::AllocateTable(
    Variant variant, TNode<IntPtrT> at_least_space_for) {
  if (variant == kMap) {
    return AllocateOrderedHashMap();
  } else {
    DCHECK_EQ(variant, kSet);
    TNode<IntPtrT> capacity = HashTableComputeCapacity(at_least_space_for);
    return AllocateOrderedHashSet(capacity);
  }
}

TF_BUILTIN(MapConstructor, CollectionsBuiltinsAssembler) {
  auto new_target = Parameter<Object>(Descriptor::kJSNewTarget);
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  auto context = Parameter<Context>(Descriptor::kContext);

  GenerateConstructor(kMap, isolate()->factory()->Map_string(), new_target,
                      argc, context);
}

TF_BUILTIN(SetConstructor, CollectionsBuiltinsAssembler) {
  auto new_target = Parameter<Object>(Descriptor::kJSNewTarget);
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  auto context = Parameter<Context>(Descriptor::kContext);

  GenerateConstructor(kSet, isolate()->factory()->Set_string(), new_target,
                      argc, context);
}

TNode<Smi> CollectionsBuiltinsAssembler::CallGetOrCreateHashRaw(
    const TNode<HeapObject> key) {
  const TNode<ExternalReference> function_addr =
      ExternalConstant(ExternalReference::get_or_create_hash_raw());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());

  MachineType type_ptr = MachineType::Pointer();
  MachineType type_tagged = MachineType::AnyTagged();

  TNode<Smi> result = CAST(CallCFunction(function_addr, type_tagged,
                                         std::make_pair(type_ptr, isolate_ptr),
                                         std::make_pair(type_tagged, key)));

  return result;
}

TNode<Uint32T> CollectionsBuiltinsAssembler::CallGetHashRaw(
    const TNode<HeapObject> key) {
  const TNode<ExternalReference> function_addr =
      ExternalConstant(ExternalReference::orderedhashmap_gethash_raw());
  const TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());

  MachineType type_ptr = MachineType::Pointer();
  MachineType type_tagged = MachineType::AnyTagged();

  TNode<Smi> result = CAST(CallCFunction(function_addr, type_tagged,
                                         std::make_pair(type_ptr, isolate_ptr),
                                         std::make_pair(type_tagged, key)));
  return PositiveSmiToUint32(result);
}

TNode<Uint32T> CollectionsBuiltinsAssembler::GetHash(
    const TNode<HeapObject> key) {
  TVARIABLE(Uint32T, var_hash);
  Label if_receiver(this), if_other(this), done(this);
  Branch(IsJSReceiver(key), &if_receiver, &if_other);

  BIND(&if_receiver);
  {
    var_hash = LoadJSReceiverIdentityHash(CAST(key));
    Goto(&done);
  }

  BIND(&if_other);
  {
    var_hash = CallGetHashRaw(key);
    Goto(&done);
  }

  BIND(&done);
  return var_hash.value();
}

void CollectionsBuiltinsAssembler::SameValueZeroSmi(TNode<Smi> key_smi,
                                                    TNode<Object> candidate_key,
                                                    Label* if_same,
                                                    Label* if_not_same) {
  // If the key is the same, we are done.
  GotoIf(TaggedEqual(candidate_key, key_smi), if_same);

  // If the candidate key is smi, then it must be different (because
  // we already checked for equality above).
  GotoIf(TaggedIsSmi(candidate_key), if_not_same);

  // If the candidate key is not smi, we still have to check if it is a
  // heap number with the same value.
  GotoIfNot(IsHeapNumber(CAST(candidate_key)), if_not_same);

  const TNode<Float64T> candidate_key_number =
      LoadHeapNumberValue(CAST(candidate_key));
  const TNode<Float64T> key_number = SmiToFloat64(key_smi);

  GotoIf(Float64Equal(candidate_key_number, key_number), if_same);

  Goto(if_not_same);
}

void CollectionsBuiltinsAssembler::BranchIfMapIteratorProtectorValid(
    Label* if_true, Label* if_false) {
  TNode<PropertyCell> protector_cell = MapIteratorProtectorConstant();
  DCHECK(i::IsPropertyCell(isolate()->heap()->map_iterator_protector()));
  Branch(
      TaggedEqual(LoadObjectField(protector_cell, PropertyCell::kValueOffset),
                  SmiConstant(Protectors::kProtectorValid)),
      if_true, if_false);
}

void CollectionsBuiltinsAssembler::
    BranchIfIterableWithOriginalKeyOrValueMapIterator(TNode<Object> iterator,
                                                      TNode<Context> context,
                                                      Label* if_true,
                                                      Label* if_false) {
  Label if_key_or_value_iterator(this), extra_checks(this);

  // Check if iterator is a keys or values JSMapIterator.
  GotoIf(TaggedIsSmi(iterator), if_false);
  TNode<Map> iter_map = LoadMap(CAST(iterator));
  const TNode<Uint16T> instance_type = LoadMapInstanceType(iter_map);
  GotoIf(InstanceTypeEqual(instance_type, JS_MAP_KEY_ITERATOR_TYPE),
         &if_key_or_value_iterator);
  Branch(InstanceTypeEqual(instance_type, JS_MAP_VALUE_ITERATOR_TYPE),
         &if_key_or_value_iterator, if_false);

  BIND(&if_key_or_value_iterator);
  // Check that the iterator is not partially consumed.
  const TNode<Object> index =
      LoadObjectField(CAST(iterator), JSMapIterator::kIndexOffset);
  GotoIfNot(TaggedEqual(index, SmiConstant(0)), if_false);
  BranchIfMapIteratorProtectorValid(&extra_checks, if_false);

  BIND(&extra_checks);
  // Check if the iterator object has the original %MapIteratorPrototype%.
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> initial_map_iter_proto = LoadContextElement(
      native_context, Context::INITIAL_MAP_ITERATOR_PROTOTYPE_INDEX);
  const TNode<HeapObject> map_iter_proto = LoadMapPrototype(iter_map);
  GotoIfNot(TaggedEqual(map_iter_proto, initial_map_iter_proto), if_false);

  // Check if the original MapIterator prototype has the original
  // %IteratorPrototype%.
  const TNode<Object> initial_iter_proto = LoadContextElement(
      native_context, Context::INITIAL_ITERATOR_PROTOTYPE_INDEX);
  const TNode<HeapObject> iter_proto =
      LoadMapPrototype(LoadMap(map_iter_proto));
  Branch(TaggedEqual(iter_proto, initial_iter_proto), if_true, if_false);
}

void BranchIfIterableWithOriginalKeyOrValueMapIterator(
    compiler::CodeAssemblerState* state, TNode<Object> iterable,
    TNode<Context> context, compiler::CodeAssemblerLabel* if_true,
    compiler::CodeAssemblerLabel* if_false) {
  CollectionsBuiltinsAssembler assembler(state);
  assembler.BranchIfIterableWithOriginalKeyOrValueMapIterator(
      iterable, context, if_true, if_false);
}

void CollectionsBuiltinsAssembler::BranchIfSetIteratorProtectorValid(
    Label* if_true, Label* if_false) {
  const TNode<PropertyCell> protector_cell = SetIteratorProtectorConstant();
  DCHECK(i::IsPropertyCell(isolate()->heap()->set_iterator_protector()));
  Branch(
      TaggedEqual(LoadObjectField(protector_cell, PropertyCell::kValueOffset),
                  SmiConstant(Protectors::kProtectorValid)),
      if_true, if_false);
}

void CollectionsBuiltinsAssembler::BranchIfIterableWithOriginalValueSetIterator(
    TNode<Object> iterable, TNode<Context> context, Label* if_true,
    Label* if_false) {
  Label if_set(this), if_value_iterator(this), check_protector(this);
  TVARIABLE(BoolT, var_result);

  GotoIf(TaggedIsSmi(iterable), if_false);
  TNode<Map> iterable_map = LoadMap(CAST(iterable));
  const TNode<Uint16T> instance_type = LoadMapInstanceType(iterable_map);

  GotoIf(InstanceTypeEqual(instance_type, JS_SET_TYPE), &if_set);
  Branch(InstanceTypeEqual(instance_type, JS_SET_VALUE_ITERATOR_TYPE),
         &if_value_iterator, if_false);

  BIND(&if_set);
  // Check if the set object has the original Set prototype.
  const TNode<Object> initial_set_proto = LoadContextElement(
      LoadNativeContext(context), Context::INITIAL_SET_PROTOTYPE_INDEX);
  const TNode<HeapObject> set_proto = LoadMapPrototype(iterable_map);
  GotoIfNot(TaggedEqual(set_proto, initial_set_proto), if_false);
  Goto(&check_protector);

  BIND(&if_value_iterator);
  // Check that the iterator is not partially consumed.
  const TNode<Object> index =
      LoadObjectField(CAST(iterable), JSSetIterator::kIndexOffset);
  GotoIfNot(TaggedEqual(index, SmiConstant(0)), if_false);

  // Check if the iterator object has the original SetIterator prototype.
  const TNode<NativeContext> native_context = LoadNativeContext(context);
  const TNode<Object> initial_set_iter_proto = LoadContextElement(
      native_context, Context::INITIAL_SET_ITERATOR_PROTOTYPE_INDEX);
  const TNode<HeapObject> set_iter_proto = LoadMapPrototype(iterable_map);
  GotoIfNot(TaggedEqual(set_iter_proto, initial_set_iter_proto), if_false);

  // Check if the original SetIterator prototype has the original
  // %IteratorPrototype%.
  const TNode<Object> initial_iter_proto = LoadContextElement(
      native_context, Context::INITIAL_ITERATOR_PROTOTYPE_INDEX);
  const TNode<HeapObject> iter_proto =
      LoadMapPrototype(LoadMap(set_iter_proto));
  GotoIfNot(TaggedEqual(iter_proto, initial_iter_proto), if_false);
  Goto(&check_protector);

  BIND(&check_protector);
  BranchIfSetIteratorProtectorValid(if_true, if_false);
}

void BranchIfIterableWithOriginalValueSetIterator(
    compiler::CodeAssemblerState* state, TNode<Object> iterable,
    TNode<Context> context, compiler::CodeAssemblerLabel* if_true,
    compiler::CodeAssemblerLabel* if_false) {
  CollectionsBuiltinsAssembler assembler(state);
  assembler.BranchIfIterableWithOriginalValueSetIterator(iterable, context,
                                                         if_true, if_false);
}

// A helper function to help extract the {table} from either a Set or
// SetIterator. The function has a side effect of marking the
// SetIterator (if SetIterator is passed) as exhausted.
TNode<OrderedHashSet> CollectionsBuiltinsAssembler::SetOrSetIteratorToSet(
    TNode<Object> iterable) {
  TVARIABLE(OrderedHashSet, var_table);
  Label if_set(this), if_iterator(this), done(this);

  const TNode<Uint16T> instance_type = LoadInstanceType(CAST(iterable));
  Branch(InstanceTypeEqual(instance_type, JS_SET_TYPE), &if_set, &if_iterator);

  BIND(&if_set);
  {
    // {iterable} is a JSSet.
    var_table = LoadObjectField<OrderedHashSet>(CAST(iterable),
                                                GetTableOffset(Variant::kSet));
    Goto(&done);
  }

  BIND(&if_iterator);
  {
    // {iterable} is a JSSetIterator.
    // Transition the {iterable} table if necessary.
    TNode<JSSetIterator> iterator = CAST(iterable);
    TNode<OrderedHashSet> table;
    TNode<IntPtrT> index;
    std::tie(table, index) =
        TransitionAndUpdate<JSSetIterator, OrderedHashSet>(iterator);
    CSA_DCHECK(this, IntPtrEqual(index, IntPtrConstant(0)));
    var_table = table;
    // Set the {iterable} to exhausted if it's an iterator.
    StoreObjectFieldRoot(iterator, JSSetIterator::kTableOffset,
                         RootIndex::kEmptyOrderedHashSet);
    TNode<IntPtrT> number_of_elements = LoadAndUntagPositiveSmiObjectField(
        table, OrderedHashSet::NumberOfElementsOffset());
    StoreObjectFieldNoWriteBarrier(iterator, JSSetIterator::kIndexOffset,
                                   SmiTag(number_of_elements));
    Goto(&done);
  }

  BIND(&done);
  return var_table.value();
}

TNode<JSArray> CollectionsBuiltinsAssembler::MapIteratorToList(
    TNode<Context> context, TNode<JSMapIterator> iterator) {
  // Transition the {iterator} table if necessary.
  TNode<OrderedHashMap> table;
  TNode<IntPtrT> index;
  std::tie(table, index) =
      TransitionAndUpdate<JSMapIterator, OrderedHashMap>(iterator);
  CSA_DCHECK(this, IntPtrEqual(index, IntPtrConstant(0)));

  TNode<Smi> size_smi =
      LoadObjectField<Smi>(table, OrderedHashMap::NumberOfElementsOffset());
  TNode<IntPtrT> size = PositiveSmiUntag(size_smi);

  const ElementsKind kind = PACKED_ELEMENTS;
  TNode<Map> array_map =
      LoadJSArrayElementsMap(kind, LoadNativeContext(context));
  TNode<JSArray> array = AllocateJSArray(kind, array_map, size, size_smi);
  TNode<FixedArray> elements = CAST(LoadElements(array));

  const int first_element_offset = FixedArray::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> first_to_element_offset =
      ElementOffsetFromIndex(IntPtrConstant(0), kind, 0);
  TVARIABLE(
      IntPtrT, var_offset,
      IntPtrAdd(first_to_element_offset, IntPtrConstant(first_element_offset)));
  TVARIABLE(IntPtrT, var_index, index);
  VariableList vars({&var_index, &var_offset}, zone());
  Label done(this, {&var_index}), loop(this, vars), continue_loop(this, vars),
      write_key(this, vars), write_value(this, vars);

  Goto(&loop);

  BIND(&loop);
  {
    // Read the next entry from the {table}, skipping holes.
    TNode<Object> entry_key;
    TNode<IntPtrT> entry_start_position;
    TNode<IntPtrT> cur_index;
    std::tie(entry_key, entry_start_position, cur_index) =
        NextSkipHashTableHoles<OrderedHashMap>(table, var_index.value(), &done);

    // Decide to write key or value.
    Branch(
        InstanceTypeEqual(LoadInstanceType(iterator), JS_MAP_KEY_ITERATOR_TYPE),
        &write_key, &write_value);

    BIND(&write_key);
    {
      Store(elements, var_offset.value(), entry_key);
      Goto(&continue_loop);
    }

    BIND(&write_value);
    {
      CSA_DCHECK(this, InstanceTypeEqual(LoadInstanceType(iterator),
                                         JS_MAP_VALUE_ITERATOR_TYPE));
      TNode<Object> entry_value =
          UnsafeLoadValueFromOrderedHashMapEntry(table, entry_start_position);

      Store(elements, var_offset.value(), entry_value);
      Goto(&continue_loop);
    }

    BIND(&continue_loop);
    {
      // Increment the array offset and continue the loop to the next entry.
      var_index = cur_index;
      var_offset = IntPtrAdd(var_offset.value(), IntPtrConstant(kTaggedSize));
      Goto(&loop);
    }
  }

  BIND(&done);
  // Set the {iterator} to exhausted.
  StoreObjectFieldRoot(iterator, JSMapIterator::kTableOffset,
                       RootIndex::kEmptyOrderedHashMap);
  StoreObjectFieldNoWriteBarrier(iterator, JSMapIterator::kIndexOffset,
                                 SmiTag(var_index.value()));
  return UncheckedCast<JSArray>(array);
}

TF_BUILTIN(MapIteratorToList, CollectionsBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto iterator = Parameter<JSMapIterator>(Descriptor::kSource);
  Return(MapIteratorToList(context, iterator));
}

TNode<JSArray> CollectionsBuiltinsAssembler::SetOrSetIteratorToList(
    TNode<Context> context, TNode<HeapObject> iterable) {
  TNode<OrderedHashSet> table = SetOrSetIteratorToSet(iterable);
  TNode<Smi> size_smi =
      LoadObjectField<Smi>(table, OrderedHashMap::NumberOfElementsOffset());
  TNode<IntPtrT> size = PositiveSmiUntag(size_smi);

  const ElementsKind kind = PACKED_ELEMENTS;
  TNode<Map> array_map =
      LoadJSArrayElementsMap(kind, LoadNativeContext(context));
  TNode<JSArray> array = AllocateJSArray(kind, array_map, size, size_smi);
  TNode<FixedArray> elements = CAST(LoadElements(array));

  const int first_element_offset = FixedArray::kHeaderSize - kHeapObjectTag;
  TNode<IntPtrT> first_to_element_offset =
      ElementOffsetFromIndex(IntPtrConstant(0), kind, 0);
  TVARIABLE(
      IntPtrT, var_offset,
      IntPtrAdd(first_to_element_offset, IntPtrConstant(first_element_offset)));
  TVARIABLE(IntPtrT, var_index, IntPtrConstant(0));
  Label done(this), loop(this, {&var_index, &var_offset});

  Goto(&loop);

  BIND(&loop);
  {
    // Read the next entry from the {table}, skipping holes.
    TNode<Object> entry_key;
    TNode<IntPtrT> entry_start_position;
    TNode<IntPtrT> cur_index;
    std::tie(entry_key, entry_start_position, cur_index) =
        NextSkipHashTableHoles<OrderedHashSet>(table, var_index.value(), &done);

    Store(elements, var_offset.value(), entry_key);

    var_index = cur_index;
    var_offset = IntPtrAdd(var_offset.value(), IntPtrConstant(kTaggedSize));
    Goto(&loop);
  }

  BIND(&done);
  return UncheckedCast<JSArray>(array);
}

TF_BUILTIN(SetOrSetIteratorToList, CollectionsBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto object = Parameter<HeapObject>(Descriptor::kSource);
  Return(SetOrSetIteratorToList(context, object));
}

TNode<Word32T> CollectionsBuiltinsAssembler::ComputeUnseededHash(
    TNode<IntPtrT> key) {
  // See v8::internal::ComputeUnseededHash()
  TNode<Word32T> hash = TruncateIntPtrToInt32(key);
  hash = Int32Add(Word32Xor(hash, Int32Constant(0xFFFFFFFF)),
                  Word32Shl(hash, Int32Constant(15)));
  hash = Word32Xor(hash, Word32Shr(hash, Int32Constant(12)));
  hash = Int32Add(hash, Word32Shl(hash, Int32Constant(2)));
  hash = Word32Xor(hash, Word32Shr(hash, Int32Constant(4)));
  hash = Int32Mul(hash, Int32Constant(2057));
  hash = Word32Xor(hash, Word32Shr(hash, Int32Constant(16)));
  return Word32And(hash, Int32Constant(0x3FFFFFFF));
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::FindOrderedHashTableEntryForSmiKey(
    TNode<CollectionType> table, TNode<Smi> smi_key, TVariable<IntPtrT>* result,
    Label* entry_found, Label* not_found) {
  const TNode<IntPtrT> key_untagged = SmiUntag(smi_key);
  const TNode<Uint32T> hash = Unsigned(ComputeUnseededHash(key_untagged));
  *result = Signed(ChangeUint32ToWord(hash));
  FindOrderedHashTableEntry<CollectionType>(
      table, hash,
      [&](TNode<Object> other_key, Label* if_same, Label* if_not_same) {
        SameValueZeroSmi(smi_key, other_key, if_same, if_not_same);
      },
      result, entry_found, not_found);
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::FindOrderedHashTableEntryForStringKey(
    TNode<CollectionType> table, TNode<String> key_tagged,
    TVariable<IntPtrT>* result, Label* entry_found, Label* not_found) {
  const TNode<Uint32T> hash = ComputeStringHash(key_tagged);
  *result = Signed(ChangeUint32ToWord(hash));
  FindOrderedHashTableEntry<CollectionType>(
      table, hash,
      [&](TNode<Object> other_key, Label* if_same, Label* if_not_same) {
        SameValueZeroString(key_tagged, other_key, if_same, if_not_same);
      },
      result, entry_found, not_found);
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::FindOrderedHashTableEntryForHeapNumberKey(
    TNode<CollectionType> table, TNode<HeapNumber> key_heap_number,
    TVariable<IntPtrT>* result, Label* entry_found, Label* not_found) {
  const TNode<Uint32T> hash = CallGetHashRaw(key_heap_number);
  *result = Signed(ChangeUint32ToWord(hash));
  const TNode<Float64T> key_float = LoadHeapNumberValue(key_heap_number);
  FindOrderedHashTableEntry<CollectionType>(
      table, hash,
      [&](TNode<Object> other_key, Label* if_same, Label* if_not_same) {
        SameValueZeroHeapNumber(key_float, other_key, if_same, if_not_same);
      },
      result, entry_found, not_found);
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::FindOrderedHashTableEntryForBigIntKey(
    TNode<CollectionType> table, TNode<BigInt> key_big_int,
    TVariable<IntPtrT>* result, Label* entry_found, Label* not_found) {
  const TNode<Uint32T> hash = CallGetHashRaw(key_big_int);
  *result = Signed(ChangeUint32ToWord(hash));
  FindOrderedHashTableEntry<CollectionType>(
      table, hash,
      [&](TNode<Object> other_key, Label* if_same, Label* if_not_same) {
        SameValueZeroBigInt(key_big_int, other_key, if_same, if_not_same);
      },
      result, entry_found, not_found);
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::FindOrderedHashTableEntryForOtherKey(
    TNode<CollectionType> table, TNode<HeapObject> key_heap_object,
    TVariable<IntPtrT>* result, Label* entry_found, Label* not_found) {
  const TNode<Uint32T> hash = GetHash(key_heap_object);
  *result = Signed(ChangeUint32ToWord(hash));
  FindOrderedHashTableEntry<CollectionType>(
      table, hash,
      [&](TNode<Object> other_key, Label* if_same, Label* if_not_same) {
        Branch(TaggedEqual(key_heap_object, other_key), if_same, if_not_same);
      },
      result, entry_found, not_found);
}

TNode<Uint32T> CollectionsBuiltinsAssembler::ComputeStringHash(
    TNode<String> string_key) {
  TVARIABLE(Uint32T, var_result);

  Label hash_not_computed(this), done(this, &var_result);
  const TNode<Uint32T> hash = LoadNameHash(string_key, &hash_not_computed);
  var_result = hash;
  Goto(&done);

  BIND(&hash_not_computed);
  var_result = CallGetHashRaw(string_key);
  Goto(&done);

  BIND(&done);
  return var_result.value();
}

void CollectionsBuiltinsAssembler::SameValueZeroString(
    TNode<String> key_string, TNode<Object> candidate_key, Label* if_same,
    Label* if_not_same) {
  // If the candidate is not a string, the keys are not equal.
  GotoIf(TaggedIsSmi(candidate_key), if_not_same);
  GotoIfNot(IsString(CAST(candidate_key)), if_not_same);

  GotoIf(TaggedEqual(key_string, candidate_key), if_same);
  BranchIfStringEqual(key_string, CAST(candidate_key), if_same, if_not_same);
}

void CollectionsBuiltinsAssembler::SameValueZeroBigInt(
    TNode<BigInt> key, TNode<Object> candidate_key, Label* if_same,
    Label* if_not_same) {
  GotoIf(TaggedIsSmi(candidate_key), if_not_same);
  GotoIfNot(IsBigInt(CAST(candidate_key)), if_not_same);

  Branch(TaggedEqual(CallRuntime(Runtime::kBigIntEqualToBigInt,
                                 NoContextConstant(), key, candidate_key),
                     TrueConstant()),
         if_same, if_not_same);
}

void CollectionsBuiltinsAssembler::SameValueZeroHeapNumber(
    TNode<Float64T> key_float, TNode<Object> candidate_key, Label* if_same,
    Label* if_not_same) {
  Label if_smi(this), if_keyisnan(this);

  GotoIf(TaggedIsSmi(candidate_key), &if_smi);
  GotoIfNot(IsHeapNumber(CAST(candidate_key)), if_not_same);

  {
    // {candidate_key} is a heap number.
    const TNode<Float64T> candidate_float =
        LoadHeapNumberValue(CAST(candidate_key));
    GotoIf(Float64Equal(key_float, candidate_float), if_same);

    // SameValueZero needs to treat NaNs as equal. First check if {key_float}
    // is NaN.
    BranchIfFloat64IsNaN(key_float, &if_keyisnan, if_not_same);

    BIND(&if_keyisnan);
    {
      // Return true iff {candidate_key} is NaN.
      Branch(Float64Equal(candidate_float, candidate_float), if_not_same,
             if_same);
    }
  }

  BIND(&if_smi);
  {
    const TNode<Float64T> candidate_float = SmiToFloat64(CAST(candidate_key));
    Branch(Float64Equal(key_float, candidate_float), if_same, if_not_same);
  }
}

TF_BUILTIN(OrderedHashTableHealIndex, CollectionsBuiltinsAssembler) {
  auto table = Parameter<HeapObject>(Descriptor::kTable);
  auto index = Parameter<Smi>(Descriptor::kIndex);
  Label return_index(this), return_zero(this);

  // Check if we need to update the {index}.
  GotoIfNot(SmiLessThan(SmiConstant(0), index), &return_zero);

  // Check if the {table} was cleared.
  static_assert(OrderedHashMap::NumberOfDeletedElementsOffset() ==
                OrderedHashSet::NumberOfDeletedElementsOffset());
  TNode<Int32T> number_of_deleted_elements = LoadAndUntagToWord32ObjectField(
      table, OrderedHashMap::NumberOfDeletedElementsOffset());
  static_assert(OrderedHashMap::kClearedTableSentinel ==
                OrderedHashSet::kClearedTableSentinel);
  GotoIf(Word32Equal(number_of_deleted_elements,
                     Int32Constant(OrderedHashMap::kClearedTableSentinel)),
         &return_zero);

  TVARIABLE(Int32T, var_i, Int32Constant(0));
  TVARIABLE(Smi, var_index, index);
  Label loop(this, {&var_i, &var_index});
  Goto(&loop);
  BIND(&loop);
  {
    TNode<Int32T> i = var_i.value();
    GotoIfNot(Int32LessThan(i, number_of_deleted_elements), &return_index);
    static_assert(OrderedHashMap::RemovedHolesIndex() ==
                  OrderedHashSet::RemovedHolesIndex());
    TNode<Smi> removed_index = CAST(LoadFixedArrayElement(
        CAST(table), ChangeUint32ToWord(i),
        OrderedHashMap::RemovedHolesIndex() * kTaggedSize));
    GotoIf(SmiGreaterThanOrEqual(removed_index, index), &return_index);
    Decrement(&var_index);
    var_i = Int32Add(var_i.value(), Int32Constant(1));
    Goto(&loop);
  }

  BIND(&return_index);
  Return(var_index.value());

  BIND(&return_zero);
  Return(SmiConstant(0));
}

template <typename TableType>
std::pair<TNode<TableType>, TNode<IntPtrT>>
CollectionsBuiltinsAssembler::Transition(
    const TNode<TableType> table, const TNode<IntPtrT> index,
    UpdateInTransition<TableType> const& update_in_transition) {
  TVARIABLE(IntPtrT, var_index, index);
  TVARIABLE(TableType, var_table, table);
  Label if_done(this), if_transition(this, Label::kDeferred);
  Branch(TaggedIsSmi(
             LoadObjectField(var_table.value(), TableType::NextTableOffset())),
         &if_done, &if_transition);

  BIND(&if_transition);
  {
    Label loop(this, {&var_table, &var_index}), done_loop(this);
    Goto(&loop);
    BIND(&loop);
    {
      TNode<TableType> current_table = var_table.value();
      TNode<IntPtrT> current_index = var_index.value();

      TNode<Object> next_table =
          LoadObjectField(current_table, TableType::NextTableOffset());
      GotoIf(TaggedIsSmi(next_table), &done_loop);

      var_table = CAST(next_table);
      var_index = SmiUntag(CAST(CallBuiltin(Builtin::kOrderedHashTableHealIndex,
                                            NoContextConstant(), current_table,
                                            SmiTag(current_index))));
      Goto(&loop);
    }
    BIND(&done_loop);

    // Update with the new {table} and {index}.
    update_in_transition(var_table.value(), var_index.value());
    Goto(&if_done);
  }

  BIND(&if_done);
  return {var_table.value(), var_index.value()};
}

template <typename IteratorType, typename TableType>
std::pair<TNode<TableType>, TNode<IntPtrT>>
CollectionsBuiltinsAssembler::TransitionAndUpdate(
    const TNode<IteratorType> iterator) {
  return Transition<TableType>(
      CAST(LoadObjectField(iterator, IteratorType::kTableOffset)),
      LoadAndUntagPositiveSmiObjectField(iterator, IteratorType::kIndexOffset),
      [this, iterator](const TNode<TableType> table,
                       const TNode<IntPtrT> index) {
        // Update the {iterator} with the new state.
        StoreObjectField(iterator, IteratorType::kTableOffset, table);
        StoreObjectFieldNoWriteBarrier(iterator, IteratorType::kIndexOffset,
                                       SmiTag(index));
      });
}

TorqueStructOrderedHashSetIndexPair
CollectionsBuiltinsAssembler::TransitionOrderedHashSetNoUpdate(
    const TNode<OrderedHashSet> table_arg, const TNode<IntPtrT> index_arg) {
  TNode<OrderedHashSet> table;
  TNode<IntPtrT> index;
  std::tie(table, index) = Transition<OrderedHashSet>(
      table_arg, index_arg,
      [](const TNode<OrderedHashSet>, const TNode<IntPtrT>) {});
  return TorqueStructOrderedHashSetIndexPair{table, index};
}

template <typename TableType>
std::tuple<TNode<Object>, TNode<IntPtrT>, TNode<IntPtrT>>
CollectionsBuiltinsAssembler::NextSkipHashTableHoles(TNode<TableType> table,
                                                     TNode<IntPtrT> index,
                                                     Label* if_end) {
  // Compute the used capacity for the {table}.
  TNode<Int32T> number_of_buckets = LoadAndUntagToWord32ObjectField(
      table, TableType::NumberOfBucketsOffset());
  TNode<Int32T> number_of_elements = LoadAndUntagToWord32ObjectField(
      table, TableType::NumberOfElementsOffset());
  TNode<Int32T> number_of_deleted_elements = LoadAndUntagToWord32ObjectField(
      table, TableType::NumberOfDeletedElementsOffset());
  TNode<Int32T> used_capacity =
      Int32Add(number_of_elements, number_of_deleted_elements);

  return NextSkipHashTableHoles(table, number_of_buckets, used_capacity, index,
                                if_end);
}

template <typename TableType>
std::tuple<TNode<Object>, TNode<IntPtrT>, TNode<IntPtrT>>
CollectionsBuiltinsAssembler::NextSkipHashTableHoles(
    TNode<TableType> table, TNode<Int32T> number_of_buckets,
    TNode<Int32T> used_capacity, TNode<IntPtrT> index, Label* if_end) {
  CSA_DCHECK(this, Word32Equal(number_of_buckets,
                               LoadAndUntagToWord32ObjectField(
                                   table, TableType::NumberOfBucketsOffset())));
  CSA_DCHECK(
      this,
      Word32Equal(
          used_capacity,
          Int32Add(LoadAndUntagToWord32ObjectField(
                       table, TableType::NumberOfElementsOffset()),
                   LoadAndUntagToWord32ObjectField(
                       table, TableType::NumberOfDeletedElementsOffset()))));

  TNode<Object> entry_key;
  TNode<Int32T> entry_start_position;
  TVARIABLE(Int32T, var_index, TruncateIntPtrToInt32(index));
  Label loop(this, &var_index), done_loop(this);
  Goto(&loop);
  BIND(&loop);
  {
    GotoIfNot(Int32LessThan(var_index.value(), used_capacity), if_end);
    entry_start_position = Int32Add(
        Int32Mul(var_index.value(), Int32Constant(TableType::kEntrySize)),
        number_of_buckets);
    entry_key = UnsafeLoadKeyFromOrderedHashTableEntry(
        table, ChangePositiveInt32ToIntPtr(entry_start_position));
    var_index = Int32Add(var_index.value(), Int32Constant(1));
    Branch(IsHashTableHole(entry_key), &loop, &done_loop);
  }

  BIND(&done_loop);
  return std::tuple<TNode<Object>, TNode<IntPtrT>, TNode<IntPtrT>>{
      entry_key, ChangePositiveInt32ToIntPtr(entry_start_position),
      ChangePositiveInt32ToIntPtr(var_index.value())};
}

template <typename CollectionType>
TorqueStructKeyIndexPair
CollectionsBuiltinsAssembler::NextKeyIndexPairUnmodifiedTable(
    const TNode<CollectionType> table, const TNode<Int32T> number_of_buckets,
    const TNode<Int32T> used_capacity, const TNode<IntPtrT> index,
    Label* if_end) {
  // Unmodified tables do not have transitions.
  CSA_DCHECK(this, TaggedIsSmi(LoadObjectField(
                       table, CollectionType::NextTableOffset())));

  TNode<Object> key;
  TNode<IntPtrT> entry_start_position;
  TNode<IntPtrT> next_index;

  std::tie(key, entry_start_position, next_index) = NextSkipHashTableHoles(
      table, number_of_buckets, used_capacity, index, if_end);

  return TorqueStructKeyIndexPair{key, next_index};
}

template TorqueStructKeyIndexPair
CollectionsBuiltinsAssembler::NextKeyIndexPairUnmodifiedTable(
    const TNode<OrderedHashMap> table, const TNode<Int32T> number_of_buckets,
    const TNode<Int32T> used_capacity, const TNode<IntPtrT> index,
    Label* if_end);
template TorqueStructKeyIndexPair
CollectionsBuiltinsAssembler::NextKeyIndexPairUnmodifiedTable(
    const TNode<OrderedHashSet> table, const TNode<Int32T> number_of_buckets,
    const TNode<Int32T> used_capacity, const TNode<IntPtrT> index,
    Label* if_end);

template <typename CollectionType>
TorqueStructKeyIndexPair CollectionsBuiltinsAssembler::NextKeyIndexPair(
    const TNode<CollectionType> table, const TNode<IntPtrT> index,
    Label* if_end) {
  TNode<Object> key;
  TNode<IntPtrT> entry_start_position;
  TNode<IntPtrT> next_index;

  std::tie(key, entry_start_position, next_index) =
      NextSkipHashTableHoles<CollectionType>(table, index, if_end);

  return TorqueStructKeyIndexPair{key, next_index};
}

template TorqueStructKeyIndexPair
CollectionsBuiltinsAssembler::NextKeyIndexPair(
    const TNode<OrderedHashMap> table, const TNode<IntPtrT> index,
    Label* if_end);
template TorqueStructKeyIndexPair
CollectionsBuiltinsAssembler::NextKeyIndexPair(
    const TNode<OrderedHashSet> table, const TNode<IntPtrT> index,
    Label* if_end);

TorqueStructKeyValueIndexTuple
CollectionsBuiltinsAssembler::NextKeyValueIndexTupleUnmodifiedTable(
    const TNode<OrderedHashMap> table, const TNode<Int32T> number_of_buckets,
    const TNode<Int32T> used_capacity, const TNode<IntPtrT> index,
    Label* if_end) {
  TNode<Object> key;
  TNode<IntPtrT> entry_start_position;
  TNode<IntPtrT> next_index;

  std::tie(key, entry_start_position, next_index) = NextSkipHashTableHoles(
      table, number_of_buckets, used_capacity, index, if_end);

  TNode<Object> value =
      UnsafeLoadValueFromOrderedHashMapEntry(table, entry_start_position);

  return TorqueStructKeyValueIndexTuple{key, value, next_index};
}

TorqueStructKeyValueIndexTuple
CollectionsBuiltinsAssembler::NextKeyValueIndexTuple(
    const TNode<OrderedHashMap> table, const TNode<IntPtrT> index,
    Label* if_end) {
  TNode<Object> key;
  TNode<IntPtrT> entry_start_position;
  TNode<IntPtrT> next_index;

  std::tie(key, entry_start_position, next_index) =
      NextSkipHashTableHoles(table, index, if_end);

  TNode<Object> value =
      UnsafeLoadValueFromOrderedHashMapEntry(table, entry_start_position);

  return TorqueStructKeyValueIndexTuple{key, value, next_index};
}

TF_BUILTIN(MapPrototypeGet, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE, "Map.prototype.get");

  const TNode<Object> table =
      LoadObjectField<Object>(CAST(receiver), JSMap::kTableOffset);
  TNode<Smi> index =
      CAST(CallBuiltin(Builtin::kFindOrderedHashMapEntry, context, table, key));

  Label if_found(this), if_not_found(this);
  Branch(SmiGreaterThanOrEqual(index, SmiConstant(0)), &if_found,
         &if_not_found);

  BIND(&if_found);
  Return(LoadValueFromOrderedHashMapEntry(CAST(table), SmiUntag(index)));

  BIND(&if_not_found);
  Return(UndefinedConstant());
}

TF_BUILTIN(MapPrototypeHas, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE, "Map.prototype.has");

  const TNode<OrderedHashMap> table =
      CAST(LoadObjectField(CAST(receiver), JSMap::kTableOffset));

  Label if_found(this), if_not_found(this);
  Branch(TableHasKey(context, table, key), &if_found, &if_not_found);

  BIND(&if_found);
  Return(TrueConstant());

  BIND(&if_not_found);
  Return(FalseConstant());
}

TNode<BoolT> CollectionsBuiltinsAssembler::TableHasKey(
    const TNode<Object> context, TNode<OrderedHashMap> table,
    TNode<Object> key) {
  TNode<Smi> index =
      CAST(CallBuiltin(Builtin::kFindOrderedHashMapEntry, context, table, key));

  return SmiGreaterThanOrEqual(index, SmiConstant(0));
}

const TNode<Object> CollectionsBuiltinsAssembler::NormalizeNumberKey(
    const TNode<Object> key) {
  TVARIABLE(Object, result, key);
  Label done(this);

  GotoIf(TaggedIsSmi(key), &done);
  GotoIfNot(IsHeapNumber(CAST(key)), &done);
  const TNode<Float64T> number = LoadHeapNumberValue(CAST(key));
  GotoIfNot(Float64Equal(number, Float64Constant(0.0)), &done);
  // We know the value is zero, so we take the key to be Smi 0.
  // Another option would be to normalize to Smi here.
  result = SmiConstant(0);
  Goto(&done);

  BIND(&done);
  return result.value();
}

template <typename CollectionType>
TNode<CollectionType> CollectionsBuiltinsAssembler::AddToOrderedHashTable(
    const TNode<CollectionType> table, const TNode<Object> key,
    const GrowCollection<CollectionType>& grow,
    const StoreAtEntry<CollectionType>& store_at_new_entry,
    const StoreAtEntry<CollectionType>& store_at_existing_entry) {
  TVARIABLE(CollectionType, table_var, table);
  TVARIABLE(IntPtrT, entry_start_position_or_hash, IntPtrConstant(0));
  Label entry_found(this), not_found(this), done(this);

  TryLookupOrderedHashTableIndex<CollectionType>(
      table, key, &entry_start_position_or_hash, &entry_found, &not_found);

  BIND(&entry_found);
  {
    // If we found the entry, we just store the value there.
    store_at_existing_entry(table, entry_start_position_or_hash.value());
    Goto(&done);
  }

  Label no_hash(this), add_entry(this), store_new_entry(this);
  BIND(&not_found);
  {
    // If we have a hash code, we can start adding the new entry.
    GotoIf(IntPtrGreaterThan(entry_start_position_or_hash.value(),
                             IntPtrConstant(0)),
           &add_entry);

    // Otherwise, go to runtime to compute the hash code.
    entry_start_position_or_hash = SmiUntag(CallGetOrCreateHashRaw(CAST(key)));
    Goto(&add_entry);
  }

  BIND(&add_entry);
  TVARIABLE(IntPtrT, number_of_buckets);
  TVARIABLE(IntPtrT, occupancy);
  {
    // Check we have enough space for the entry.
    number_of_buckets = PositiveSmiUntag(CAST(UnsafeLoadFixedArrayElement(
        table, CollectionType::NumberOfBucketsIndex())));

    static_assert(CollectionType::kLoadFactor == 2);
    const TNode<WordT> capacity = WordShl(number_of_buckets.value(), 1);
    const TNode<IntPtrT> number_of_elements =
        LoadAndUntagPositiveSmiObjectField(
            table, CollectionType::NumberOfElementsOffset());
    const TNode<IntPtrT> number_of_deleted =
        PositiveSmiUntag(CAST(LoadObjectField(
            table, CollectionType::NumberOfDeletedElementsOffset())));
    occupancy = IntPtrAdd(number_of_elements, number_of_deleted);
    GotoIf(IntPtrLessThan(occupancy.value(), capacity), &store_new_entry);

    // We do not have enough space, grow the table and reload the relevant
    // fields.
    table_var = grow();
    number_of_buckets = PositiveSmiUntag(CAST(UnsafeLoadFixedArrayElement(
        table_var.value(), CollectionType::NumberOfBucketsIndex())));
    const TNode<IntPtrT> new_number_of_elements =
        LoadAndUntagPositiveSmiObjectField(
            table_var.value(), CollectionType::NumberOfElementsOffset());
    const TNode<IntPtrT> new_number_of_deleted = PositiveSmiUntag(
        CAST(LoadObjectField(table_var.value(),
                             CollectionType::NumberOfDeletedElementsOffset())));
    occupancy = IntPtrAdd(new_number_of_elements, new_number_of_deleted);
    Goto(&store_new_entry);
  }

  BIND(&store_new_entry);
  {
    StoreOrderedHashTableNewEntry(
        table_var.value(), entry_start_position_or_hash.value(),
        number_of_buckets.value(), occupancy.value(), store_at_new_entry);
    Goto(&done);
  }

  BIND(&done);
  return table_var.value();
}

TF_BUILTIN(MapPrototypeSet, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kKey);
  const auto value = Parameter<Object>(Descriptor::kValue);
  const auto context = Parameter<Context>(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE, "Map.prototype.set");

  key = NormalizeNumberKey(key);

  GrowCollection<OrderedHashMap> grow = [this, context, receiver]() {
    CallRuntime(Runtime::kMapGrow, context, receiver);
    return LoadObjectField<OrderedHashMap>(CAST(receiver), JSMap::kTableOffset);
  };

  StoreAtEntry<OrderedHashMap> store_at_new_entry =
      [this, key, value](const TNode<OrderedHashMap> table,
                         const TNode<IntPtrT> entry_start) {
        UnsafeStoreKeyValueInOrderedHashMapEntry(table, key, value,
                                                 entry_start);
      };

  StoreAtEntry<OrderedHashMap> store_at_existing_entry =
      [this, value](const TNode<OrderedHashMap> table,
                    const TNode<IntPtrT> entry_start) {
        UnsafeStoreValueInOrderedHashMapEntry(table, value, entry_start);
      };

  const TNode<OrderedHashMap> table =
      LoadObjectField<OrderedHashMap>(CAST(receiver), JSMap::kTableOffset);
  AddToOrderedHashTable(table, key, grow, store_at_new_entry,
                        store_at_existing_entry);
  Return(receiver);
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::StoreOrderedHashTableNewEntry(
    const TNode<CollectionType> table, const TNode<IntPtrT> hash,
    const TNode<IntPtrT> number_of_buckets, const TNode<IntPtrT> occupancy,
    const StoreAtEntry<CollectionType>& store_at_new_entry) {
  const TNode<IntPtrT> bucket =
      WordAnd(hash, IntPtrSub(number_of_buckets, IntPtrConstant(1)));
  TNode<Smi> bucket_entry = CAST(UnsafeLoadFixedArrayElement(
      table, bucket, CollectionType::HashTableStartIndex() * kTaggedSize));

  // Store the entry elements.
  const TNode<IntPtrT> entry_start = IntPtrAdd(
      IntPtrMul(occupancy, IntPtrConstant(CollectionType::kEntrySize)),
      number_of_buckets);
  store_at_new_entry(table, entry_start);

  // Connect the element to the bucket chain.
  UnsafeStoreFixedArrayElement(
      table, entry_start, bucket_entry,
      kTaggedSize * (CollectionType::HashTableStartIndex() +
                     CollectionType::kChainOffset));

  // Update the bucket head.
  UnsafeStoreFixedArrayElement(
      table, bucket, SmiTag(occupancy),
      CollectionType::HashTableStartIndex() * kTaggedSize);

  // Bump the elements count.
  const TNode<Smi> number_of_elements =
      CAST(LoadObjectField(table, CollectionType::NumberOfElementsOffset()));
  StoreObjectFieldNoWriteBarrier(table,
                                 CollectionType::NumberOfElementsOffset(),
                                 SmiAdd(number_of_elements, SmiConstant(1)));
}

// This is a helper function to add a new entry to an ordered hash table,
// when we are adding new entries from a Set.
template <typename CollectionType>
void CollectionsBuiltinsAssembler::AddNewToOrderedHashTable(
    const TNode<CollectionType> table, const TNode<Object> normalised_key,
    const TNode<IntPtrT> number_of_buckets, const TNode<IntPtrT> occupancy,
    const StoreAtEntry<CollectionType>& store_at_new_entry) {
  Label if_key_smi(this), if_key_string(this), if_key_heap_number(this),
      if_key_bigint(this), if_key_other(this), call_store(this);
  TVARIABLE(IntPtrT, hash, IntPtrConstant(0));

  GotoIf(TaggedIsSmi(normalised_key), &if_key_smi);
  TNode<Map> key_map = LoadMap(CAST(normalised_key));
  TNode<Uint16T> key_instance_type = LoadMapInstanceType(key_map);

  GotoIf(IsStringInstanceType(key_instance_type), &if_key_string);
  GotoIf(IsHeapNumberMap(key_map), &if_key_heap_number);
  GotoIf(IsBigIntInstanceType(key_instance_type), &if_key_bigint);
  Goto(&if_key_other);

  BIND(&if_key_other);
  {
    hash = Signed(ChangeUint32ToWord(GetHash(CAST(normalised_key))));
    Goto(&call_store);
  }

  BIND(&if_key_smi);
  {
    hash = ChangeInt32ToIntPtr(
        ComputeUnseededHash(SmiUntag(CAST(normalised_key))));
    Goto(&call_store);
  }

  BIND(&if_key_string);
  {
    hash = Signed(ChangeUint32ToWord(ComputeStringHash(CAST(normalised_key))));
    Goto(&call_store);
  }

  BIND(&if_key_heap_number);
  {
    hash = Signed(ChangeUint32ToWord(GetHash(CAST(normalised_key))));
    Goto(&call_store);
  }

  BIND(&if_key_bigint);
  {
    hash = Signed(ChangeUint32ToWord(GetHash(CAST(normalised_key))));
    Goto(&call_store);
  }

  BIND(&call_store);
  StoreOrderedHashTableNewEntry(table, hash.value(), number_of_buckets,
                                occupancy, store_at_new_entry);
}

void CollectionsBuiltinsAssembler::StoreValueInOrderedHashMapEntry(
    const TNode<OrderedHashMap> table, const TNode<Object> value,
    const TNode<IntPtrT> entry_start, CheckBounds check_bounds) {
  StoreFixedArrayElement(table, entry_start, value, UPDATE_WRITE_BARRIER,
                         kTaggedSize * (OrderedHashMap::HashTableStartIndex() +
                                        OrderedHashMap::kValueOffset),
                         check_bounds);
}

void CollectionsBuiltinsAssembler::StoreKeyValueInOrderedHashMapEntry(
    const TNode<OrderedHashMap> table, const TNode<Object> key,
    const TNode<Object> value, const TNode<IntPtrT> entry_start,
    CheckBounds check_bounds) {
  StoreFixedArrayElement(table, entry_start, key, UPDATE_WRITE_BARRIER,
                         kTaggedSize * OrderedHashMap::HashTableStartIndex(),
                         check_bounds);
  StoreValueInOrderedHashMapEntry(table, value, entry_start, check_bounds);
}

TF_BUILTIN(MapPrototypeDelete, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE,
                         "Map.prototype.delete");

  const TNode<OrderedHashMap> table =
      LoadObjectField<OrderedHashMap>(CAST(receiver), JSMap::kTableOffset);

  TVARIABLE(IntPtrT, entry_start_position_or_hash, IntPtrConstant(0));
  Label entry_found(this), not_found(this);

  TryLookupOrderedHashTableIndex<OrderedHashMap>(
      table, key, &entry_start_position_or_hash, &entry_found, &not_found);

  BIND(&not_found);
  Return(FalseConstant());

  BIND(&entry_found);
  // If we found the entry, mark the entry as deleted.
  StoreKeyValueInOrderedHashMapEntry(table, HashTableHoleConstant(),
                                     HashTableHoleConstant(),
                                     entry_start_position_or_hash.value());

  // Decrement the number of elements, increment the number of deleted elements.
  const TNode<Smi> number_of_elements = SmiSub(
      CAST(LoadObjectField(table, OrderedHashMap::NumberOfElementsOffset())),
      SmiConstant(1));
  StoreObjectFieldNoWriteBarrier(
      table, OrderedHashMap::NumberOfElementsOffset(), number_of_elements);
  const TNode<Smi> number_of_deleted =
      SmiAdd(CAST(LoadObjectField(
                 table, OrderedHashMap::NumberOfDeletedElementsOffset())),
             SmiConstant(1));
  StoreObjectFieldNoWriteBarrier(
      table, OrderedHashMap::NumberOfDeletedElementsOffset(),
      number_of_deleted);

  const TNode<Smi> number_of_buckets = CAST(
      LoadFixedArrayElement(table, OrderedHashMap::NumberOfBucketsIndex()));

  // If there fewer elements than #buckets / 2, shrink the table.
  Label shrink(this);
  GotoIf(SmiLessThan(SmiAdd(number_of_elements, number_of_elements),
                     number_of_buckets),
         &shrink);
  Return(TrueConstant());

  BIND(&shrink);
  CallRuntime(Runtime::kMapShrink, context, receiver);
  Return(TrueConstant());
}

TF_BUILTIN(SetPrototypeAdd, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE, "Set.prototype.add");

  key = NormalizeNumberKey(key);

  GrowCollection<OrderedHashSet> grow = [this, context, receiver]() {
    CallRuntime(Runtime::kSetGrow, context, receiver);
    return LoadObjectField<OrderedHashSet>(CAST(receiver), JSSet::kTableOffset);
  };

  StoreAtEntry<OrderedHashSet> store_at_new_entry =
      [this, key](const TNode<OrderedHashSet> table,
                  const TNode<IntPtrT> entry_start) {
        UnsafeStoreKeyInOrderedHashSetEntry(table, key, entry_start);
      };

  StoreAtEntry<OrderedHashSet> store_at_existing_entry =
      [](const TNode<OrderedHashSet>, const TNode<IntPtrT>) {
        // If the entry was found, there is nothing to do.
      };

  const TNode<OrderedHashSet> table =
      LoadObjectField<OrderedHashSet>(CAST(receiver), JSSet::kTableOffset);
  AddToOrderedHashTable(table, key, grow, store_at_new_entry,
                        store_at_existing_entry);
  Return(receiver);
}

TNode<OrderedHashSet> CollectionsBuiltinsAssembler::AddToSetTable(
    const TNode<Object> context, TNode<OrderedHashSet> table, TNode<Object> key,
    TNode<String> method_name) {
  key = NormalizeNumberKey(key);

  GrowCollection<OrderedHashSet> grow = [this, context, table, method_name]() {
    TNode<OrderedHashSet> new_table = Cast(
        CallRuntime(Runtime::kOrderedHashSetGrow, context, table, method_name));
    // TODO(v8:13556): check if the table is updated and remove pointer to the
    // new table.
    return new_table;
  };

  StoreAtEntry<OrderedHashSet> store_at_new_entry =
      [this, key](const TNode<OrderedHashSet> table,
                  const TNode<IntPtrT> entry_start) {
        UnsafeStoreKeyInOrderedHashSetEntry(table, key, entry_start);
      };

  StoreAtEntry<OrderedHashSet> store_at_existing_entry =
      [](const TNode<OrderedHashSet>, const TNode<IntPtrT>) {
        // If the entry was found, there is nothing to do.
      };

  return AddToOrderedHashTable(table, key, grow, store_at_new_entry,
                               store_at_existing_entry);
}

void CollectionsBuiltinsAssembler::StoreKeyInOrderedHashSetEntry(
    const TNode<OrderedHashSet> table, const TNode<Object> key,
    const TNode<IntPtrT> entry_start, CheckBounds check_bounds) {
  StoreFixedArrayElement(table, entry_start, key, UPDATE_WRITE_BARRIER,
                         kTaggedSize * OrderedHashSet::HashTableStartIndex(),
                         check_bounds);
}

template <typename CollectionType>
TNode<Object> CollectionsBuiltinsAssembler::LoadKeyFromOrderedHashTableEntry(
    const TNode<CollectionType> table, const TNode<IntPtrT> entry,
    CheckBounds check_bounds) {
  return LoadFixedArrayElement(
      table, entry, kTaggedSize * CollectionType::HashTableStartIndex(),
      check_bounds);
}

TNode<Object> CollectionsBuiltinsAssembler::LoadValueFromOrderedHashMapEntry(
    const TNode<OrderedHashMap> table, const TNode<IntPtrT> entry,
    CheckBounds check_bounds) {
  return LoadFixedArrayElement(
      table, entry,
      kTaggedSize * (OrderedHashMap::HashTableStartIndex() +
                     OrderedHashMap::kValueOffset),
      check_bounds);
}

TF_BUILTIN(SetPrototypeDelete, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE,
                         "Set.prototype.delete");

  // This check breaks a known exploitation technique. See crbug.com/1263462
  CSA_HOLE_SECURITY_CHECK(this, TaggedNotEqual(key, HashTableHoleConstant()));

  const TNode<OrderedHashSet> table =
      LoadObjectField<OrderedHashSet>(CAST(receiver), JSMap::kTableOffset);

  Label not_found(this);
  const TNode<Smi> number_of_elements =
      DeleteFromSetTable(context, table, key, &not_found);

  const TNode<Smi> number_of_buckets = CAST(
      LoadFixedArrayElement(table, OrderedHashSet::NumberOfBucketsIndex()));

  // If there fewer elements than #buckets / 2, shrink the table.
  Label shrink(this);
  GotoIf(SmiLessThan(SmiAdd(number_of_elements, number_of_elements),
                     number_of_buckets),
         &shrink);
  Return(TrueConstant());

  BIND(&shrink);
  CallRuntime(Runtime::kSetShrink, context, receiver);
  Return(TrueConstant());

  BIND(&not_found);
  Return(FalseConstant());
}

TNode<Smi> CollectionsBuiltinsAssembler::DeleteFromSetTable(
    const TNode<Object> context, TNode<OrderedHashSet> table, TNode<Object> key,
    Label* not_found) {
  TVARIABLE(IntPtrT, entry_start_position_or_hash, IntPtrConstant(0));
  Label entry_found(this);

  TryLookupOrderedHashTableIndex<OrderedHashSet>(
      table, key, &entry_start_position_or_hash, &entry_found, not_found);

  BIND(&entry_found);
  // If we found the entry, mark the entry as deleted.
  StoreKeyInOrderedHashSetEntry(table, HashTableHoleConstant(),
                                entry_start_position_or_hash.value());

  // Decrement the number of elements, increment the number of deleted elements.
  const TNode<Smi> number_of_elements = SmiSub(
      CAST(LoadObjectField(table, OrderedHashSet::NumberOfElementsOffset())),
      SmiConstant(1));
  StoreObjectFieldNoWriteBarrier(
      table, OrderedHashSet::NumberOfElementsOffset(), number_of_elements);
  const TNode<Smi> number_of_deleted =
      SmiAdd(CAST(LoadObjectField(
                 table, OrderedHashSet::NumberOfDeletedElementsOffset())),
             SmiConstant(1));
  StoreObjectFieldNoWriteBarrier(
      table, OrderedHashSet::NumberOfDeletedElementsOffset(),
      number_of_deleted);

  return number_of_elements;
}

TF_BUILTIN(MapPrototypeEntries, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);
  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE,
                         "Map.prototype.entries");
  Return(AllocateJSCollectionIterator<JSMapIterator>(
      context, Context::MAP_KEY_VALUE_ITERATOR_MAP_INDEX, CAST(receiver)));
}

TF_BUILTIN(MapPrototypeGetSize, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);
  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE,
                         "get Map.prototype.size");
  const TNode<OrderedHashMap> table =
      LoadObjectField<OrderedHashMap>(CAST(receiver), JSMap::kTableOffset);
  Return(LoadObjectField(table, OrderedHashMap::NumberOfElementsOffset()));
}

TF_BUILTIN(MapPrototypeForEach, CollectionsBuiltinsAssembler) {
  const char* const kMethodName = "Map.prototype.forEach";
  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  const auto context = Parameter<Context>(Descriptor::kContext);
  CodeStubArguments args(this, argc);
  const TNode<Object> receiver = args.GetReceiver();
  const TNode<Object> callback = args.GetOptionalArgumentValue(0);
  const TNode<Object> this_arg = args.GetOptionalArgumentValue(1);

  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE, kMethodName);

  // Ensure that {callback} is actually callable.
  Label callback_not_callable(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(callback), &callback_not_callable);
  GotoIfNot(IsCallable(CAST(callback)), &callback_not_callable);

  TVARIABLE(IntPtrT, var_index, IntPtrConstant(0));
  TVARIABLE(OrderedHashMap, var_table,
            CAST(LoadObjectField(CAST(receiver), JSMap::kTableOffset)));
  Label loop(this, {&var_index, &var_table}), done_loop(this);
  Goto(&loop);
  BIND(&loop);
  {
    // Transition {table} and {index} if there was any modification to
    // the {receiver} while we're iterating.
    TNode<IntPtrT> index = var_index.value();
    TNode<OrderedHashMap> table = var_table.value();
    std::tie(table, index) = Transition<OrderedHashMap>(
        table, index, [](const TNode<OrderedHashMap>, const TNode<IntPtrT>) {});

    // Read the next entry from the {table}, skipping holes.
    TNode<Object> entry_key;
    TNode<IntPtrT> entry_start_position;
    std::tie(entry_key, entry_start_position, index) =
        NextSkipHashTableHoles<OrderedHashMap>(table, index, &done_loop);

    // Load the entry value as well.
    TNode<Object> entry_value =
        LoadValueFromOrderedHashMapEntry(table, entry_start_position);

    // Invoke the {callback} passing the {entry_key}, {entry_value} and the
    // {receiver}.
    Call(context, callback, this_arg, entry_value, entry_key, receiver);

    // Continue with the next entry.
    var_index = index;
    var_table = table;
    Goto(&loop);
  }

  BIND(&done_loop);
  args.PopAndReturn(UndefinedConstant());

  BIND(&callback_not_callable);
  {
    CallRuntime(Runtime::kThrowCalledNonCallable, context, callback);
    Unreachable();
  }
}

TF_BUILTIN(MapPrototypeKeys, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);
  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE, "Map.prototype.keys");
  Return(AllocateJSCollectionIterator<JSMapIterator>(
      context, Context::MAP_KEY_ITERATOR_MAP_INDEX, CAST(receiver)));
}

TF_BUILTIN(MapPrototypeValues, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);
  ThrowIfNotInstanceType(context, receiver, JS_MAP_TYPE,
                         "Map.prototype.values");
  Return(AllocateJSCollectionIterator<JSMapIterator>(
      context, Context::MAP_VALUE_ITERATOR_MAP_INDEX, CAST(receiver)));
}

TF_BUILTIN(MapIteratorPrototypeNext, CollectionsBuiltinsAssembler) {
  const char* const kMethodName = "Map Iterator.prototype.next";
  const auto maybe_receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);

  // Ensure that {maybe_receiver} is actually a JSMapIterator.
  Label if_receiver_valid(this), if_receiver_invalid(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(maybe_receiver), &if_receiver_invalid);
  const TNode<Uint16T> receiver_instance_type =
      LoadInstanceType(CAST(maybe_receiver));
  GotoIf(
      InstanceTypeEqual(receiver_instance_type, JS_MAP_KEY_VALUE_ITERATOR_TYPE),
      &if_receiver_valid);
  GotoIf(InstanceTypeEqual(receiver_instance_type, JS_MAP_KEY_ITERATOR_TYPE),
         &if_receiver_valid);
  Branch(InstanceTypeEqual(receiver_instance_type, JS_MAP_VALUE_ITERATOR_TYPE),
         &if_receiver_valid, &if_receiver_invalid);
  BIND(&if_receiver_invalid);
  ThrowTypeError(context, MessageTemplate::kIncompatibleMethodReceiver,
                 StringConstant(kMethodName), maybe_receiver);
  BIND(&if_receiver_valid);
  TNode<JSMapIterator> receiver = CAST(maybe_receiver);

  // Check if the {receiver} is exhausted.
  TVARIABLE(Boolean, var_done, TrueConstant());
  TVARIABLE(Object, var_value, UndefinedConstant());
  Label return_value(this, {&var_done, &var_value}), return_entry(this),
      return_end(this, Label::kDeferred);

  // Transition the {receiver} table if necessary.
  TNode<OrderedHashMap> table;
  TNode<IntPtrT> index;
  std::tie(table, index) =
      TransitionAndUpdate<JSMapIterator, OrderedHashMap>(receiver);

  // Read the next entry from the {table}, skipping holes.
  TNode<Object> entry_key;
  TNode<IntPtrT> entry_start_position;
  std::tie(entry_key, entry_start_position, index) =
      NextSkipHashTableHoles<OrderedHashMap>(table, index, &return_end);
  StoreObjectFieldNoWriteBarrier(receiver, JSMapIterator::kIndexOffset,
                                 SmiTag(index));
  var_value = entry_key;
  var_done = FalseConstant();

  // Check how to return the {key} (depending on {receiver} type).
  GotoIf(InstanceTypeEqual(receiver_instance_type, JS_MAP_KEY_ITERATOR_TYPE),
         &return_value);
  var_value = LoadValueFromOrderedHashMapEntry(table, entry_start_position);
  Branch(InstanceTypeEqual(receiver_instance_type, JS_MAP_VALUE_ITERATOR_TYPE),
         &return_value, &return_entry);

  BIND(&return_entry);
  {
    TNode<JSObject> result =
        AllocateJSIteratorResultForEntry(context, entry_key, var_value.value());
    Return(result);
  }

  BIND(&return_value);
  {
    TNode<JSObject> result =
        AllocateJSIteratorResult(context, var_value.value(), var_done.value());
    Return(result);
  }

  BIND(&return_end);
  {
    StoreObjectFieldRoot(receiver, JSMapIterator::kTableOffset,
                         RootIndex::kEmptyOrderedHashMap);
    Goto(&return_value);
  }
}

TF_BUILTIN(SetPrototypeHas, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE, "Set.prototype.has");

  const TNode<OrderedHashSet> table =
      CAST(LoadObjectField(CAST(receiver), JSMap::kTableOffset));

  Label if_found(this), if_not_found(this);
  Branch(TableHasKey(context, table, key), &if_found, &if_not_found);

  BIND(&if_found);
  Return(TrueConstant());

  BIND(&if_not_found);
  Return(FalseConstant());
}

TNode<BoolT> CollectionsBuiltinsAssembler::TableHasKey(
    const TNode<Object> context, TNode<OrderedHashSet> table,
    TNode<Object> key) {
  TNode<Smi> index =
      CAST(CallBuiltin(Builtin::kFindOrderedHashSetEntry, context, table, key));

  return SmiGreaterThanOrEqual(index, SmiConstant(0));
}

TF_BUILTIN(SetPrototypeEntries, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);
  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE,
                         "Set.prototype.entries");
  Return(AllocateJSCollectionIterator<JSSetIterator>(
      context, Context::SET_KEY_VALUE_ITERATOR_MAP_INDEX, CAST(receiver)));
}

TF_BUILTIN(SetPrototypeGetSize, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);
  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE,
                         "get Set.prototype.size");
  const TNode<OrderedHashSet> table =
      LoadObjectField<OrderedHashSet>(CAST(receiver), JSSet::kTableOffset);
  Return(LoadObjectField(table, OrderedHashSet::NumberOfElementsOffset()));
}

TF_BUILTIN(SetPrototypeForEach, CollectionsBuiltinsAssembler) {
  const char* const kMethodName = "Set.prototype.forEach";
  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  const auto context = Parameter<Context>(Descriptor::kContext);
  CodeStubArguments args(this, argc);
  const TNode<Object> receiver = args.GetReceiver();
  const TNode<Object> callback = args.GetOptionalArgumentValue(0);
  const TNode<Object> this_arg = args.GetOptionalArgumentValue(1);

  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE, kMethodName);

  // Ensure that {callback} is actually callable.
  Label callback_not_callable(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(callback), &callback_not_callable);
  GotoIfNot(IsCallable(CAST(callback)), &callback_not_callable);

  TVARIABLE(IntPtrT, var_index, IntPtrConstant(0));
  TVARIABLE(OrderedHashSet, var_table,
            CAST(LoadObjectField(CAST(receiver), JSSet::kTableOffset)));
  Label loop(this, {&var_index, &var_table}), done_loop(this);
  Goto(&loop);
  BIND(&loop);
  {
    // Transition {table} and {index} if there was any modification to
    // the {receiver} while we're iterating.
    TNode<IntPtrT> index = var_index.value();
    TNode<OrderedHashSet> table = var_table.value();
    std::tie(table, index) = Transition<OrderedHashSet>(
        table, index, [](const TNode<OrderedHashSet>, const TNode<IntPtrT>) {});

    // Read the next entry from the {table}, skipping holes.
    TNode<Object> entry_key;
    TNode<IntPtrT> entry_start_position;
    std::tie(entry_key, entry_start_position, index) =
        NextSkipHashTableHoles<OrderedHashSet>(table, index, &done_loop);

    // Invoke the {callback} passing the {entry_key} (twice) and the {receiver}.
    Call(context, callback, this_arg, entry_key, entry_key, receiver);

    // Continue with the next entry.
    var_index = index;
    var_table = table;
    Goto(&loop);
  }

  BIND(&done_loop);
  args.PopAndReturn(UndefinedConstant());

  BIND(&callback_not_callable);
  {
    CallRuntime(Runtime::kThrowCalledNonCallable, context, callback);
    Unreachable();
  }
}

TF_BUILTIN(SetPrototypeValues, CollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);
  ThrowIfNotInstanceType(context, receiver, JS_SET_TYPE,
                         "Set.prototype.values");
  Return(AllocateJSCollectionIterator<JSSetIterator>(
      context, Context::SET_VALUE_ITERATOR_MAP_INDEX, CAST(receiver)));
}

TF_BUILTIN(SetIteratorPrototypeNext, CollectionsBuiltinsAssembler) {
  const char* const kMethodName = "Set Iterator.prototype.next";
  const auto maybe_receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto context = Parameter<Context>(Descriptor::kContext);

  // Ensure that {maybe_receiver} is actually a JSSetIterator.
  Label if_receiver_valid(this), if_receiver_invalid(this, Label::kDeferred);
  GotoIf(TaggedIsSmi(maybe_receiver), &if_receiver_invalid);
  const TNode<Uint16T> receiver_instance_type =
      LoadInstanceType(CAST(maybe_receiver));
  GotoIf(InstanceTypeEqual(receiver_instance_type, JS_SET_VALUE_ITERATOR_TYPE),
         &if_receiver_valid);
  Branch(
      InstanceTypeEqual(receiver_instance_type, JS_SET_KEY_VALUE_ITERATOR_TYPE),
      &if_receiver_valid, &if_receiver_invalid);
  BIND(&if_receiver_invalid);
  ThrowTypeError(context, MessageTemplate::kIncompatibleMethodReceiver,
                 StringConstant(kMethodName), maybe_receiver);
  BIND(&if_receiver_valid);

  TNode<JSSetIterator> receiver = CAST(maybe_receiver);

  // Check if the {receiver} is exhausted.
  TVARIABLE(Boolean, var_done, TrueConstant());
  TVARIABLE(Object, var_value, UndefinedConstant());
  Label return_value(this, {&var_done, &var_value}), return_entry(this),
      return_end(this, Label::kDeferred);

  // Transition the {receiver} table if necessary.
  TNode<OrderedHashSet> table;
  TNode<IntPtrT> index;
  std::tie(table, index) =
      TransitionAndUpdate<JSSetIterator, OrderedHashSet>(receiver);

  // Read the next entry from the {table}, skipping holes.
  TNode<Object> entry_key;
  TNode<IntPtrT> entry_start_position;
  std::tie(entry_key, entry_start_position, index) =
      NextSkipHashTableHoles<OrderedHashSet>(table, index, &return_end);
  StoreObjectFieldNoWriteBarrier(receiver, JSSetIterator::kIndexOffset,
                                 SmiTag(index));
  var_value = entry_key;
  var_done = FalseConstant();

  // Check how to return the {key} (depending on {receiver} type).
  Branch(InstanceTypeEqual(receiver_instance_type, JS_SET_VALUE_ITERATOR_TYPE),
         &return_value, &return_entry);

  BIND(&return_entry);
  {
    TNode<JSObject> result = AllocateJSIteratorResultForEntry(
        context, var_value.value(), var_value.value());
    Return(result);
  }

  BIND(&return_value);
  {
    TNode<JSObject> result =
        AllocateJSIteratorResult(context, var_value.value(), var_done.value());
    Return(result);
  }

  BIND(&return_end);
  {
    StoreObjectFieldRoot(receiver, JSSetIterator::kTableOffset,
                         RootIndex::kEmptyOrderedHashSet);
    Goto(&return_value);
  }
}

template <typename CollectionType>
void CollectionsBuiltinsAssembler::TryLookupOrderedHashTableIndex(
    const TNode<CollectionType> table, const TNode<Object> key,
    TVariable<IntPtrT>* result, Label* if_entry_found, Label* if_not_found) {
  Label if_key_smi(this), if_key_string(this), if_key_heap_number(this),
      if_key_bigint(this);

  GotoIf(TaggedIsSmi(key), &if_key_smi);

  TNode<Map> key_map = LoadMap(CAST(key));
  TNode<Uint16T> key_instance_type = LoadMapInstanceType(key_map);

  GotoIf(IsStringInstanceType(key_instance_type), &if_key_string);
  GotoIf(IsHeapNumberMap(key_map), &if_key_heap_number);
  GotoIf(IsBigIntInstanceType(key_instance_type), &if_key_bigint);

  FindOrderedHashTableEntryForOtherKey<CollectionType>(
      table, CAST(key), result, if_entry_found, if_not_found);

  BIND(&if_key_smi);
  {
    FindOrderedHashTableEntryForSmiKey<CollectionType>(
        table, CAST(key), result, if_entry_found, if_not_found);
  }

  BIND(&if_key_string);
  {
    FindOrderedHashTableEntryForStringKey<CollectionType>(
        table, CAST(key), result, if_entry_found, if_not_found);
  }

  BIND(&if_key_heap_number);
  {
    FindOrderedHashTableEntryForHeapNumberKey<CollectionType>(
        table, CAST(key), result, if_entry_found, if_not_found);
  }

  BIND(&if_key_bigint);
  {
    FindOrderedHashTableEntryForBigIntKey<CollectionType>(
        table, CAST(key), result, if_entry_found, if_not_found);
  }
}

TF_BUILTIN(FindOrderedHashMapEntry, CollectionsBuiltinsAssembler) {
  const auto table = Parameter<OrderedHashMap>(Descriptor::kTable);
  const auto key = Parameter<Object>(Descriptor::kKey);

  TVARIABLE(IntPtrT, entry_start_position, IntPtrConstant(0));
  Label entry_found(this), not_found(this);

  TryLookupOrderedHashTableIndex<OrderedHashMap>(
      table, key, &entry_start_position, &entry_found, &not_found);

  BIND(&entry_found);
  Return(SmiTag(entry_start_position.value()));

  BIND(&not_found);
  Return(SmiConstant(-1));
}

TF_BUILTIN(FindOrderedHashSetEntry, CollectionsBuiltinsAssembler) {
  const auto table = Parameter<OrderedHashSet>(Descriptor::kTable);
  const auto key = Parameter<Object>(Descriptor::kKey);

  TVARIABLE(IntPtrT, entry_start_position, IntPtrConstant(0));
  Label entry_found(this), not_found(this);

  TryLookupOrderedHashTableIndex<OrderedHashSet>(
      table, key, &entry_start_position, &entry_found, &not_found);

  BIND(&entry_found);
  Return(SmiTag(entry_start_position.value()));

  BIND(&not_found);
  Return(SmiConstant(-1));
}

const TNode<OrderedHashMap> CollectionsBuiltinsAssembler::AddValueToKeyedGroup(
    const TNode<OrderedHashMap> groups, const TNode<Object> key,
    const TNode<Object> value, const TNode<String> methodName) {
  GrowCollection<OrderedHashMap> grow = [this, groups, methodName]() {
    TNode<OrderedHashMap> new_groups = CAST(CallRuntime(
        Runtime::kOrderedHashMapGrow, NoContextConstant(), groups, methodName));
    // The groups OrderedHashMap is not escaped to user script while grouping
    // items, so there can't be live iterators. So we don't need to keep the
    // pointer from the old table to the new one.
    Label did_grow(this), done(this);
    Branch(TaggedEqual(groups, new_groups), &done, &did_grow);
    BIND(&did_grow);
    {
      StoreObjectFieldNoWriteBarrier(groups, OrderedHashMap::NextTableOffset(),
                                     SmiConstant(0));
      Goto(&done);
    }
    BIND(&done);
    return new_groups;
  };

  StoreAtEntry<OrderedHashMap> store_at_new_entry =
      [this, key, value](const TNode<OrderedHashMap> table,
                         const TNode<IntPtrT> entry_start) {
        TNode<ArrayList> array = AllocateArrayList(SmiConstant(1));
        ArrayListSet(array, SmiConstant(0), value);
        ArrayListSetLength(array, SmiConstant(1));
        StoreKeyValueInOrderedHashMapEntry(table, key, array, entry_start);
      };

  StoreAtEntry<OrderedHashMap> store_at_existing_entry =
      [this, key, value](const TNode<OrderedHashMap> table,
                         const TNode<IntPtrT> entry_start) {
        TNode<ArrayList> array =
            CAST(LoadValueFromOrderedHashMapEntry(table, entry_start));
        TNode<ArrayList> new_array = ArrayListAdd(array, value);
        StoreKeyValueInOrderedHashMapEntry(table, key, new_array, entry_start);
      };

  return AddToOrderedHashTable(groups, key, grow, store_at_new_entry,
                               store_at_existing_entry);
}

void WeakCollectionsBuiltinsAssembler::AddEntry(
    TNode<EphemeronHashTable> table, TNode<IntPtrT> key_index,
    TNode<Object> key, TNode<Object> value, TNode<Int32T> number_of_elements) {
  // See EphemeronHashTable::AddEntry().
  TNode<IntPtrT> value_index = ValueIndexFromKeyIndex(key_index);
  UnsafeStoreFixedArrayElement(table, key_index, key,
                               UPDATE_EPHEMERON_KEY_WRITE_BARRIER);
  UnsafeStoreFixedArrayElement(table, value_index, value);

  // See HashTableBase::ElementAdded().
  UnsafeStoreFixedArrayElement(table,
                               EphemeronHashTable::kNumberOfElementsIndex,
                               SmiFromInt32(number_of_elements));
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::GetHash(
    const TNode<HeapObject> key, Label* if_no_hash) {
  TVARIABLE(IntPtrT, var_hash);
  Label if_symbol(this);
  Label return_result(this);
  GotoIfNot(IsJSReceiver(key), &if_symbol);
  var_hash = Signed(
      ChangeUint32ToWord(LoadJSReceiverIdentityHash(CAST(key), if_no_hash)));
  Goto(&return_result);
  Bind(&if_symbol);
  CSA_DCHECK(this, IsSymbol(key));
  CSA_DCHECK(this, Word32BinaryNot(
                       Word32And(LoadSymbolFlags(CAST(key)),
                                 Symbol::IsInPublicSymbolTableBit::kMask)));
  var_hash = Signed(ChangeUint32ToWord(LoadNameHash(CAST(key), nullptr)));
  Goto(&return_result);
  Bind(&return_result);
  return var_hash.value();
}

TNode<HeapObject> WeakCollectionsBuiltinsAssembler::AllocateTable(
    Variant variant, TNode<IntPtrT> at_least_space_for) {
  // See HashTable::New().
  DCHECK(variant == kWeakSet || variant == kWeakMap);
  CSA_DCHECK(this,
             IntPtrLessThanOrEqual(IntPtrConstant(0), at_least_space_for));
  TNode<IntPtrT> capacity = HashTableComputeCapacity(at_least_space_for);

  // See HashTable::NewInternal().
  TNode<IntPtrT> length = KeyIndexFromEntry(capacity);
  TNode<FixedArray> table = CAST(AllocateFixedArray(HOLEY_ELEMENTS, length));

  TNode<Map> map =
      HeapConstantNoHole(EphemeronHashTable::GetMap(ReadOnlyRoots(isolate())));
  StoreMapNoWriteBarrier(table, map);
  StoreFixedArrayElement(table, EphemeronHashTable::kNumberOfElementsIndex,
                         SmiConstant(0), SKIP_WRITE_BARRIER);
  StoreFixedArrayElement(table,
                         EphemeronHashTable::kNumberOfDeletedElementsIndex,
                         SmiConstant(0), SKIP_WRITE_BARRIER);
  StoreFixedArrayElement(table, EphemeronHashTable::kCapacityIndex,
                         SmiFromIntPtr(capacity), SKIP_WRITE_BARRIER);

  TNode<IntPtrT> start = KeyIndexFromEntry(IntPtrConstant(0));
  FillFixedArrayWithValue(HOLEY_ELEMENTS, table, start, length,
                          RootIndex::kUndefinedValue);
  return table;
}

TNode<Smi> WeakCollectionsBuiltinsAssembler::CreateIdentityHash(
    TNode<Object> key) {
  TNode<ExternalReference> function_addr =
      ExternalConstant(ExternalReference::jsreceiver_create_identity_hash());
  TNode<ExternalReference> isolate_ptr =
      ExternalConstant(ExternalReference::isolate_address());

  MachineType type_ptr = MachineType::Pointer();
  MachineType type_tagged = MachineType::AnyTagged();

  return CAST(CallCFunction(function_addr, type_tagged,
                            std::make_pair(type_ptr, isolate_ptr),
                            std::make_pair(type_tagged, key)));
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::EntryMask(
    TNode<IntPtrT> capacity) {
  return IntPtrSub(capacity, IntPtrConstant(1));
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::FindKeyIndex(
    TNode<HeapObject> table, TNode<IntPtrT> key_hash, TNode<IntPtrT> entry_mask,
    const KeyComparator& key_compare) {
  // See HashTable::FirstProbe().
  TVARIABLE(IntPtrT, var_entry, WordAnd(key_hash, entry_mask));
  TVARIABLE(IntPtrT, var_count, IntPtrConstant(0));

  Label loop(this, {&var_count, &var_entry}), if_found(this);
  Goto(&loop);
  BIND(&loop);
  TNode<IntPtrT> key_index;
  {
    key_index = KeyIndexFromEntry(var_entry.value());
    TNode<Object> entry_key =
        UnsafeLoadFixedArrayElement(CAST(table), key_index);

    key_compare(entry_key, &if_found);

    // See HashTable::NextProbe().
    Increment(&var_count);
    var_entry =
        WordAnd(IntPtrAdd(var_entry.value(), var_count.value()), entry_mask);
    Goto(&loop);
  }

  BIND(&if_found);
  return key_index;
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::FindKeyIndexForInsertion(
    TNode<HeapObject> table, TNode<IntPtrT> key_hash,
    TNode<IntPtrT> entry_mask) {
  // See HashTable::FindInsertionEntry().
  auto is_not_live = [&](TNode<Object> entry_key, Label* if_found) {
    // This is the the negative form BaseShape::IsLive().
    GotoIf(Word32Or(IsTheHole(entry_key), IsUndefined(entry_key)), if_found);
  };
  return FindKeyIndex(table, key_hash, entry_mask, is_not_live);
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::FindKeyIndexForKey(
    TNode<HeapObject> table, TNode<Object> key, TNode<IntPtrT> hash,
    TNode<IntPtrT> entry_mask, Label* if_not_found) {
  // See HashTable::FindEntry().
  auto match_key_or_exit_on_empty = [&](TNode<Object> entry_key,
                                        Label* if_same) {
    GotoIf(IsUndefined(entry_key), if_not_found);
    GotoIf(TaggedEqual(entry_key, key), if_same);
  };
  return FindKeyIndex(table, hash, entry_mask, match_key_or_exit_on_empty);
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::KeyIndexFromEntry(
    TNode<IntPtrT> entry) {
  // See HashTable::KeyAt().
  // (entry * kEntrySize) + kElementsStartIndex + kEntryKeyIndex
  return IntPtrAdd(
      IntPtrMul(entry, IntPtrConstant(EphemeronHashTable::kEntrySize)),
      IntPtrConstant(EphemeronHashTable::kElementsStartIndex +
                     EphemeronHashTable::kEntryKeyIndex));
}

TNode<Int32T> WeakCollectionsBuiltinsAssembler::LoadNumberOfElements(
    TNode<EphemeronHashTable> table, int offset) {
  TNode<Int32T> number_of_elements =
      SmiToInt32(CAST(UnsafeLoadFixedArrayElement(
          table, EphemeronHashTable::kNumberOfElementsIndex)));
  return Int32Add(number_of_elements, Int32Constant(offset));
}

TNode<Int32T> WeakCollectionsBuiltinsAssembler::LoadNumberOfDeleted(
    TNode<EphemeronHashTable> table, int offset) {
  TNode<Int32T> number_of_deleted = SmiToInt32(CAST(UnsafeLoadFixedArrayElement(
      table, EphemeronHashTable::kNumberOfDeletedElementsIndex)));
  return Int32Add(number_of_deleted, Int32Constant(offset));
}

TNode<EphemeronHashTable> WeakCollectionsBuiltinsAssembler::LoadTable(
    TNode<JSWeakCollection> collection) {
  return CAST(LoadObjectField(collection, JSWeakCollection::kTableOffset));
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::LoadTableCapacity(
    TNode<EphemeronHashTable> table) {
  return PositiveSmiUntag(CAST(
      UnsafeLoadFixedArrayElement(table, EphemeronHashTable::kCapacityIndex)));
}

TNode<Word32T> WeakCollectionsBuiltinsAssembler::InsufficientCapacityToAdd(
    TNode<Int32T> capacity, TNode<Int32T> number_of_elements,
    TNode<Int32T> number_of_deleted) {
  // This is the negative form of HashTable::HasSufficientCapacityToAdd().
  // Return true if:
  //   - more than 50% of the available space are deleted elements
  //   - less than 50% will be available
  TNode<Int32T> available = Int32Sub(capacity, number_of_elements);
  TNode<Int32T> half_available = Signed(Word32Shr(available, 1));
  TNode<Int32T> needed_available = Signed(Word32Shr(number_of_elements, 1));
  return Word32Or(
      // deleted > half
      Int32GreaterThan(number_of_deleted, half_available),
      // elements + needed available > capacity
      Int32GreaterThan(Int32Add(number_of_elements, needed_available),
                       capacity));
}

void WeakCollectionsBuiltinsAssembler::RemoveEntry(
    TNode<EphemeronHashTable> table, TNode<IntPtrT> key_index,
    TNode<IntPtrT> number_of_elements) {
  // See EphemeronHashTable::RemoveEntry().
  TNode<IntPtrT> value_index = ValueIndexFromKeyIndex(key_index);
  StoreFixedArrayElement(table, key_index, TheHoleConstant());
  StoreFixedArrayElement(table, value_index, TheHoleConstant());

  // See HashTableBase::ElementRemoved().
  TNode<Int32T> number_of_deleted = LoadNumberOfDeleted(table, 1);
  StoreFixedArrayElement(table, EphemeronHashTable::kNumberOfElementsIndex,
                         SmiFromIntPtr(number_of_elements), SKIP_WRITE_BARRIER);
  StoreFixedArrayElement(table,
                         EphemeronHashTable::kNumberOfDeletedElementsIndex,
                         SmiFromInt32(number_of_deleted), SKIP_WRITE_BARRIER);
}

TNode<BoolT> WeakCollectionsBuiltinsAssembler::ShouldRehash(
    TNode<Int32T> number_of_elements, TNode<Int32T> number_of_deleted) {
  // Rehash if more than 33% of the entries are deleted.
  return Int32GreaterThanOrEqual(Word32Shl(number_of_deleted, 1),
                                 number_of_elements);
}

TNode<Word32T> WeakCollectionsBuiltinsAssembler::ShouldShrink(
    TNode<IntPtrT> capacity, TNode<IntPtrT> number_of_elements) {
  // See HashTable::Shrink().
  TNode<IntPtrT> quarter_capacity = WordShr(capacity, 2);
  return Word32And(
      // Shrink to fit the number of elements if only a quarter of the
      // capacity is filled with elements.
      IntPtrLessThanOrEqual(number_of_elements, quarter_capacity),

      // Allocate a new dictionary with room for at least the current
      // number of elements. The allocation method will make sure that
      // there is extra room in the dictionary for additions. Don't go
      // lower than room for 16 elements.
      IntPtrGreaterThanOrEqual(number_of_elements, IntPtrConstant(16)));
}

TNode<IntPtrT> WeakCollectionsBuiltinsAssembler::ValueIndexFromKeyIndex(
    TNode<IntPtrT> key_index) {
  return IntPtrAdd(
      key_index,
      IntPtrConstant(EphemeronHashTable::TodoShape::kEntryValueIndex -
                     EphemeronHashTable::kEntryKeyIndex));
}

TF_BUILTIN(WeakMapConstructor, WeakCollectionsBuiltinsAssembler) {
  auto new_target = Parameter<Object>(Descriptor::kJSNewTarget);
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  auto context = Parameter<Context>(Descriptor::kContext);

  GenerateConstructor(kWeakMap, isolate()->factory()->WeakMap_string(),
                      new_target, argc, context);
}

TF_BUILTIN(WeakSetConstructor, WeakCollectionsBuiltinsAssembler) {
  auto new_target = Parameter<Object>(Descriptor::kJSNewTarget);
  TNode<IntPtrT> argc = ChangeInt32ToIntPtr(
      UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount));
  auto context = Parameter<Context>(Descriptor::kContext);

  GenerateConstructor(kWeakSet, isolate()->factory()->WeakSet_string(),
                      new_target, argc, context);
}

TF_BUILTIN(WeakMapLookupHashIndex, WeakCollectionsBuiltinsAssembler) {
  auto table = Parameter<EphemeronHashTable>(Descriptor::kTable);
  auto key = Parameter<Object>(Descriptor::kKey);

  Label if_cannot_be_held_weakly(this);

  GotoIfCannotBeHeldWeakly(key, &if_cannot_be_held_weakly);

  TNode<IntPtrT> hash = GetHash(CAST(key), &if_cannot_be_held_weakly);
  TNode<IntPtrT> capacity = LoadTableCapacity(table);
  TNode<IntPtrT> key_index = FindKeyIndexForKey(
      table, key, hash, EntryMask(capacity), &if_cannot_be_held_weakly);
  Return(SmiTag(ValueIndexFromKeyIndex(key_index)));

  BIND(&if_cannot_be_held_weakly);
  Return(SmiConstant(-1));
}

TF_BUILTIN(WeakMapGet, WeakCollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  Label return_undefined(this);

  ThrowIfNotInstanceType(context, receiver, JS_WEAK_MAP_TYPE,
                         "WeakMap.prototype.get");

  const TNode<EphemeronHashTable> table = LoadTable(CAST(receiver));
  const TNode<Smi> index =
      CAST(CallBuiltin(Builtin::kWeakMapLookupHashIndex, context, table, key));

  GotoIf(TaggedEqual(index, SmiConstant(-1)), &return_undefined);

  Return(LoadFixedArrayElement(table, SmiUntag(index)));

  BIND(&return_undefined);
  Return(UndefinedConstant());
}

TF_BUILTIN(WeakMapPrototypeHas, WeakCollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  Label return_false(this);

  ThrowIfNotInstanceType(context, receiver, JS_WEAK_MAP_TYPE,
                         "WeakMap.prototype.has");

  const TNode<EphemeronHashTable> table = LoadTable(CAST(receiver));
  const TNode<Object> index =
      CallBuiltin(Builtin::kWeakMapLookupHashIndex, context, table, key);

  GotoIf(TaggedEqual(index, SmiConstant(-1)), &return_false);

  Return(TrueConstant());

  BIND(&return_false);
  Return(FalseConstant());
}

// Helper that removes the entry with a given key from the backing store
// (EphemeronHashTable) of a WeakMap or WeakSet.
TF_BUILTIN(WeakCollectionDelete, WeakCollectionsBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto collection = Parameter<JSWeakCollection>(Descriptor::kCollection);
  auto key = Parameter<Object>(Descriptor::kKey);

  Label call_runtime(this), if_cannot_be_held_weakly(this);

  GotoIfCannotBeHeldWeakly(key, &if_cannot_be_held_weakly);

  TNode<IntPtrT> hash = GetHash(CAST(key), &if_cannot_be_held_weakly);
  TNode<EphemeronHashTable> table = LoadTable(collection);
  TNode<IntPtrT> capacity = LoadTableCapacity(table);
  TNode<IntPtrT> key_index = FindKeyIndexForKey(
      table, key, hash, EntryMask(capacity), &if_cannot_be_held_weakly);
  TNode<Int32T> number_of_elements = LoadNumberOfElements(table, -1);
  GotoIf(ShouldShrink(capacity, ChangeInt32ToIntPtr(number_of_elements)),
         &call_runtime);

  RemoveEntry(table, key_index, ChangeInt32ToIntPtr(number_of_elements));
  Return(TrueConstant());

  BIND(&if_cannot_be_held_weakly);
  Return(FalseConstant());

  BIND(&call_runtime);
  Return(CallRuntime(Runtime::kWeakCollectionDelete, context, collection, key,
                     SmiTag(hash)));
}

// Helper that sets the key and value to the backing store (EphemeronHashTable)
// of a WeakMap or WeakSet.
TF_BUILTIN(WeakCollectionSet, WeakCollectionsBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto collection = Parameter<JSWeakCollection>(Descriptor::kCollection);
  auto key = Parameter<HeapObject>(Descriptor::kKey);
  auto value = Parameter<Object>(Descriptor::kValue);

  CSA_DCHECK(this, Word32Or(IsJSReceiver(key), IsSymbol(key)));

  Label call_runtime(this), if_no_hash(this), if_not_found(this);

  TNode<EphemeronHashTable> table = LoadTable(collection);
  TNode<IntPtrT> capacity = LoadTableCapacity(table);
  TNode<IntPtrT> entry_mask = EntryMask(capacity);

  TVARIABLE(IntPtrT, var_hash, GetHash(key, &if_no_hash));
  TNode<IntPtrT> key_index = FindKeyIndexForKey(table, key, var_hash.value(),
                                                entry_mask, &if_not_found);

  StoreFixedArrayElement(table, ValueIndexFromKeyIndex(key_index), value);
  Return(collection);

  BIND(&if_no_hash);
  {
    CSA_DCHECK(this, IsJSReceiver(key));
    var_hash = SmiUntag(CreateIdentityHash(key));
    Goto(&if_not_found);
  }
  BIND(&if_not_found);
  {
    TNode<Int32T> number_of_deleted = LoadNumberOfDeleted(table);
    TNode<Int32T> number_of_elements = LoadNumberOfElements(table, 1);

    CSA_DCHECK(this,
               IntPtrLessThanOrEqual(capacity, IntPtrConstant(INT32_MAX)));
    CSA_DCHECK(this,
               IntPtrGreaterThanOrEqual(capacity, IntPtrConstant(INT32_MIN)));
    // TODO(pwong): Port HashTable's Rehash() and EnsureCapacity() to CSA.
    GotoIf(Word32Or(ShouldRehash(number_of_elements, number_of_deleted),
                    InsufficientCapacityToAdd(TruncateIntPtrToInt32(capacity),
                                              number_of_elements,
                                              number_of_deleted)),
           &call_runtime);

    TNode<IntPtrT> insertion_key_index =
        FindKeyIndexForInsertion(table, var_hash.value(), entry_mask);
    AddEntry(table, insertion_key_index, key, value, number_of_elements);
    Return(collection);
  }
  BIND(&call_runtime);
  {
    CallRuntime(Runtime::kWeakCollectionSet, context, collection, key, value,
                SmiTag(var_hash.value()));
    Return(collection);
  }
}

TF_BUILTIN(WeakMapPrototypeDelete, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kKey);

  ThrowIfNotInstanceType(context, receiver, JS_WEAK_MAP_TYPE,
                         "WeakMap.prototype.delete");

  // This check breaks a known exploitation technique. See crbug.com/1263462
  CSA_HOLE_SECURITY_CHECK(this, TaggedNotEqual(key, TheHoleConstant()));

  Return(CallBuiltin(Builtin::kWeakCollectionDelete, context, receiver, key));
}

TF_BUILTIN(WeakMapPrototypeSet, WeakCollectionsBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto key = Parameter<Object>(Descriptor::kKey);
  auto value = Parameter<Object>(Descriptor::kValue);

  ThrowIfNotInstanceType(context, receiver, JS_WEAK_MAP_TYPE,
                         "WeakMap.prototype.set");

  Label throw_invalid_key(this);
  GotoIfCannotBeHeldWeakly(key, &throw_invalid_key);

  Return(
      CallBuiltin(Builtin::kWeakCollectionSet, context, receiver, key, value));

  BIND(&throw_invalid_key);
  ThrowTypeError(context, MessageTemplate::kInvalidWeakMapKey, key);
}

TF_BUILTIN(WeakSetPrototypeAdd, WeakCollectionsBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto value = Parameter<Object>(Descriptor::kValue);

  ThrowIfNotInstanceType(context, receiver, JS_WEAK_SET_TYPE,
                         "WeakSet.prototype.add");

  Label throw_invalid_value(this);
  GotoIfCannotBeHeldWeakly(value, &throw_invalid_value);

  Return(CallBuiltin(Builtin::kWeakCollectionSet, context, receiver, value,
                     TrueConstant()));

  BIND(&throw_invalid_value);
  ThrowTypeError(context, MessageTemplate::kInvalidWeakSetValue, value);
}

TF_BUILTIN(WeakSetPrototypeDelete, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto receiver = Parameter<Object>(Descriptor::kReceiver);
  auto value = Parameter<Object>(Descriptor::kValue);

  ThrowIfNotInstanceType(context, receiver, JS_WEAK_SET_TYPE,
                         "WeakSet.prototype.delete");

  // This check breaks a known exploitation technique. See crbug.com/1263462
  CSA_HOLE_SECURITY_CHECK(this, TaggedNotEqual(value, TheHoleConstant()));

  Return(CallBuiltin(Builtin::kWeakCollectionDelete, context, receiver, value));
}

TF_BUILTIN(WeakSetPrototypeHas, WeakCollectionsBuiltinsAssembler) {
  const auto receiver = Parameter<Object>(Descriptor::kReceiver);
  const auto key = Parameter<Object>(Descriptor::kKey);
  const auto context = Parameter<Context>(Descriptor::kContext);

  Label return_false(this);

  ThrowIfNotInstanceType(context, receiver, JS_WEAK_SET_TYPE,
                         "WeakSet.prototype.has");

  const TNode<EphemeronHashTable> table = LoadTable(CAST(receiver));
  const TNode<Object> index =
      CallBuiltin(Builtin::kWeakMapLookupHashIndex, context, table, key);

  GotoIf(TaggedEqual(index, SmiConstant(-1)), &return_false);

  Return(TrueConstant());

  BIND(&return_false);
  Return(FalseConstant());
}

}  // namespace internal
}  // namespace v8
