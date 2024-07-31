// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_COLLECTIONS_GEN_H_
#define V8_BUILTINS_BUILTINS_COLLECTIONS_GEN_H_

#include "src/codegen/code-stub-assembler.h"

namespace v8 {
namespace internal {

void BranchIfIterableWithOriginalKeyOrValueMapIterator(
    compiler::CodeAssemblerState* state, TNode<Object> iterable,
    TNode<Context> context, compiler::CodeAssemblerLabel* if_true,
    compiler::CodeAssemblerLabel* if_false);

void BranchIfIterableWithOriginalValueSetIterator(
    compiler::CodeAssemblerState* state, TNode<Object> iterable,
    TNode<Context> context, compiler::CodeAssemblerLabel* if_true,
    compiler::CodeAssemblerLabel* if_false);

class BaseCollectionsAssembler : public CodeStubAssembler {
 public:
  explicit BaseCollectionsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  virtual ~BaseCollectionsAssembler() = default;

  void GotoIfCannotBeHeldWeakly(const TNode<Object> obj,
                                Label* if_cannot_be_held_weakly);

 protected:
  enum Variant { kMap, kSet, kWeakMap, kWeakSet };

  // Adds an entry to a collection.  For Maps, properly handles extracting the
  // key and value from the entry (see LoadKeyValue()).
  void AddConstructorEntry(Variant variant, TNode<Context> context,
                           TNode<Object> collection, TNode<Object> add_function,
                           TNode<Object> key_value,
                           Label* if_may_have_side_effects = nullptr,
                           Label* if_exception = nullptr,
                           TVariable<Object>* var_exception = nullptr);

  virtual void GetEntriesIfFastCollectionOrIterable(
      Variant variant, TNode<Object> initial_entries, TNode<Context> context,
      TVariable<HeapObject>* var_entries_table,
      TVariable<IntPtrT>* var_number_of_elements,
      Label* if_not_fast_collection) = 0;

  // Adds constructor entries to a collection.  Choosing a fast path when
  // possible.
  void AddConstructorEntries(Variant variant, TNode<Context> context,
                             TNode<NativeContext> native_context,
                             TNode<HeapObject> collection,
                             TNode<Object> initial_entries);

  // Fast path for adding constructor entries.  Assumes the entries are a fast
  // JS array (see CodeStubAssembler::BranchIfFastJSArray()).
  void AddConstructorEntriesFromFastJSArray(
      Variant variant, TNode<Context> context, TNode<Context> native_context,
      TNode<Object> collection, TNode<JSArray> fast_jsarray,
      Label* if_may_have_side_effects, TVariable<IntPtrT>& var_current_index);

  // Adds constructor entries to a collection using the iterator protocol.
  void AddConstructorEntriesFromIterable(
      Variant variant, TNode<Context> context, TNode<Context> native_context,
      TNode<Object> collection, TNode<Object> iterable, Label* if_exception,
      TVariable<JSReceiver>* var_iterator, TVariable<Object>* var_exception);

  virtual void AddConstructorEntriesFromFastCollection(
      Variant variant, TNode<HeapObject> collection,
      TNode<HeapObject> source_table) = 0;

  // Constructs a collection instance. Choosing a fast path when possible.
  TNode<JSObject> AllocateJSCollection(TNode<Context> context,
                                       TNode<JSFunction> constructor,
                                       TNode<JSReceiver> new_target);

  // Fast path for constructing a collection instance if the constructor
  // function has not been modified.
  TNode<JSObject> AllocateJSCollectionFast(TNode<JSFunction> constructor);

  // Fallback for constructing a collection instance if the constructor function
  // has been modified.
  TNode<JSObject> AllocateJSCollectionSlow(TNode<Context> context,
                                           TNode<JSFunction> constructor,
                                           TNode<JSReceiver> new_target);

  // Allocates the backing store for a collection.
  virtual TNode<HeapObject> AllocateTable(
      Variant variant, TNode<IntPtrT> at_least_space_for) = 0;

  // Main entry point for a collection constructor builtin.
  void GenerateConstructor(Variant variant,
                           Handle<String> constructor_function_name,
                           TNode<Object> new_target, TNode<IntPtrT> argc,
                           TNode<Context> context);

  // Retrieves the collection function that adds an entry. `set` for Maps and
  // `add` for Sets.
  TNode<Object> GetAddFunction(Variant variant, TNode<Context> context,
                               TNode<Object> collection);

  // Retrieves the collection constructor function.
  TNode<JSFunction> GetConstructor(Variant variant,
                                   TNode<Context> native_context);

  // Retrieves the initial collection function that adds an entry. Should only
  // be called when it is certain that a collection prototype's map hasn't been
  // changed.
  TNode<JSFunction> GetInitialAddFunction(Variant variant,
                                          TNode<Context> native_context);

  // Checks whether {collection}'s initial add/set function has been modified
  // (depending on {variant}, loaded from {native_context}).
  void GotoIfInitialAddFunctionModified(Variant variant,
                                        TNode<NativeContext> native_context,
                                        TNode<HeapObject> collection,
                                        Label* if_modified);

  // Gets root index for the name of the add/set function.
  RootIndex GetAddFunctionNameIndex(Variant variant);

  // Retrieves the offset to access the backing table from the collection.
  int GetTableOffset(Variant variant);

  // Determines whether the collection's prototype has been modified.
  TNode<BoolT> HasInitialCollectionPrototype(Variant variant,
                                             TNode<Context> native_context,
                                             TNode<Object> collection);

  // Gets the initial prototype map for given collection {variant}.
  TNode<Map> GetInitialCollectionPrototype(Variant variant,
                                           TNode<Context> native_context);

  // Loads an element from a fixed array.  If the element is the hole, returns
  // `undefined`.
  TNode<Object> LoadAndNormalizeFixedArrayElement(TNode<FixedArray> elements,
                                                  TNode<IntPtrT> index);

  // Loads an element from a fixed double array.  If the element is the hole,
  // returns `undefined`.
  TNode<Object> LoadAndNormalizeFixedDoubleArrayElement(
      TNode<HeapObject> elements, TNode<IntPtrT> index);
};

class CollectionsBuiltinsAssembler : public BaseCollectionsAssembler {
 public:
  explicit CollectionsBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : BaseCollectionsAssembler(state) {}

  // Check whether |iterable| is a JS_MAP_KEY_ITERATOR_TYPE or
  // JS_MAP_VALUE_ITERATOR_TYPE object that is not partially consumed and still
  // has original iteration behavior.
  void BranchIfIterableWithOriginalKeyOrValueMapIterator(TNode<Object> iterable,
                                                         TNode<Context> context,
                                                         Label* if_true,
                                                         Label* if_false);

  // Check whether |iterable| is a JS_SET_TYPE or JS_SET_VALUE_ITERATOR_TYPE
  // object that still has original iteration behavior. In case of the iterator,
  // the iterator also must not have been partially consumed.
  void BranchIfIterableWithOriginalValueSetIterator(TNode<Object> iterable,
                                                    TNode<Context> context,
                                                    Label* if_true,
                                                    Label* if_false);

  // Adds an element to a set if the element is not already in the set.
  TNode<OrderedHashSet> AddToSetTable(TNode<Object> context,
                                      TNode<OrderedHashSet> table,
                                      TNode<Object> key,
                                      TNode<String> method_name);
  // Direct iteration helpers.
  template <typename CollectionType>
  TorqueStructKeyIndexPair NextKeyIndexPairUnmodifiedTable(
      const TNode<CollectionType> table, const TNode<Int32T> number_of_buckets,
      const TNode<Int32T> used_capacity, const TNode<IntPtrT> index,
      Label* if_end);

  template <typename CollectionType>
  TorqueStructKeyIndexPair NextKeyIndexPair(const TNode<CollectionType> table,
                                            const TNode<IntPtrT> index,
                                            Label* if_end);

  TorqueStructKeyValueIndexTuple NextKeyValueIndexTupleUnmodifiedTable(
      const TNode<OrderedHashMap> table, const TNode<Int32T> number_of_buckets,
      const TNode<Int32T> used_capacity, const TNode<IntPtrT> index,
      Label* if_end);

  TorqueStructKeyValueIndexTuple NextKeyValueIndexTuple(
      const TNode<OrderedHashMap> table, const TNode<IntPtrT> index,
      Label* if_end);

  // Checks if the set/map contains a key.
  TNode<BoolT> TableHasKey(const TNode<Object> context,
                           TNode<OrderedHashSet> table, TNode<Object> key);
  TNode<BoolT> TableHasKey(const TNode<Object> context,
                           TNode<OrderedHashMap> table, TNode<Object> key);

  // Adds {value} to a FixedArray keyed by {key} in {groups}.
  //
  // Utility used by Object.groupBy and Map.groupBy.
  const TNode<OrderedHashMap> AddValueToKeyedGroup(
      const TNode<OrderedHashMap> groups, const TNode<Object> key,
      const TNode<Object> value, const TNode<String> methodName);

  // Normalizes -0 to +0.
  const TNode<Object> NormalizeNumberKey(const TNode<Object> key);

  // Methods after this point should really be protected but are exposed for
  // Torque.
  void UnsafeStoreValueInOrderedHashMapEntry(const TNode<OrderedHashMap> table,
                                             const TNode<Object> value,
                                             const TNode<IntPtrT> entry) {
    return StoreValueInOrderedHashMapEntry(table, value, entry,
                                           CheckBounds::kDebugOnly);
  }

  TNode<Smi> DeleteFromSetTable(const TNode<Object> context,
                                TNode<OrderedHashSet> table, TNode<Object> key,
                                Label* not_found);

  TorqueStructOrderedHashSetIndexPair TransitionOrderedHashSetNoUpdate(
      const TNode<OrderedHashSet> table, const TNode<IntPtrT> index);

 protected:
  template <typename IteratorType>
  TNode<HeapObject> AllocateJSCollectionIterator(
      const TNode<Context> context, int map_index,
      const TNode<HeapObject> collection);
  TNode<HeapObject> AllocateTable(Variant variant,
                                  TNode<IntPtrT> at_least_space_for) override;
  TNode<Uint32T> GetHash(const TNode<HeapObject> key);
  TNode<Uint32T> CallGetHashRaw(const TNode<HeapObject> key);
  TNode<Smi> CallGetOrCreateHashRaw(const TNode<HeapObject> key);

  // Transitions the iterator to the non obsolete backing store.
  // This is a NOP if the [table] is not obsolete.
  template <typename TableType>
  using UpdateInTransition = std::function<void(const TNode<TableType> table,
                                                const TNode<IntPtrT> index)>;
  template <typename TableType>
  std::pair<TNode<TableType>, TNode<IntPtrT>> Transition(
      const TNode<TableType> table, const TNode<IntPtrT> index,
      UpdateInTransition<TableType> const& update_in_transition);
  template <typename IteratorType, typename TableType>
  std::pair<TNode<TableType>, TNode<IntPtrT>> TransitionAndUpdate(
      const TNode<IteratorType> iterator);

  template <typename TableType>
  std::tuple<TNode<Object>, TNode<IntPtrT>, TNode<IntPtrT>>
  NextSkipHashTableHoles(TNode<TableType> table, TNode<IntPtrT> index,
                         Label* if_end);
  template <typename TableType>
  std::tuple<TNode<Object>, TNode<IntPtrT>, TNode<IntPtrT>>
  NextSkipHashTableHoles(TNode<TableType> table,
                         TNode<Int32T> number_of_buckets,
                         TNode<Int32T> used_capacity, TNode<IntPtrT> index,
                         Label* if_end);

  // A helper function to help extract the {table} from either a Set or
  // SetIterator. The function has a side effect of marking the
  // SetIterator (if SetIterator is passed) as exhausted.
  TNode<OrderedHashSet> SetOrSetIteratorToSet(TNode<Object> iterator);

  // Adds constructor entries to a collection when constructing from a Set
  void AddConstructorEntriesFromSet(TNode<JSSet> collection,
                                    TNode<OrderedHashSet> table);

  // a helper function to unwrap a fast js collection and load its length.
  // var_entries_table is a variable meant to store the unwrapped collection.
  // var_number_of_elements is a variable meant to store the length of the
  // unwrapped collection. the function jumps to if_not_fast_collection if the
  // collection is not a fast js collection.
  void GetEntriesIfFastCollectionOrIterable(
      Variant variant, TNode<Object> initial_entries, TNode<Context> context,
      TVariable<HeapObject>* var_entries_table,
      TVariable<IntPtrT>* var_number_of_elements,
      Label* if_not_fast_collection) override;

  // a helper to load constructor entries from a fast js collection.
  void AddConstructorEntriesFromFastCollection(
      Variant variant, TNode<HeapObject> collection,
      TNode<HeapObject> source_table) override;

  // Specialization for Smi.
  // The {result} variable will contain the entry index if the key was found,
  // or the hash code otherwise.
  template <typename CollectionType>
  void FindOrderedHashTableEntryForSmiKey(TNode<CollectionType> table,
                                          TNode<Smi> key_tagged,
                                          TVariable<IntPtrT>* result,
                                          Label* entry_found, Label* not_found);
  void SameValueZeroSmi(TNode<Smi> key_smi, TNode<Object> candidate_key,
                        Label* if_same, Label* if_not_same);

  // Specialization for heap numbers.
  // The {result} variable will contain the entry index if the key was found,
  // or the hash code otherwise.
  void SameValueZeroHeapNumber(TNode<Float64T> key_float,
                               TNode<Object> candidate_key, Label* if_same,
                               Label* if_not_same);
  template <typename CollectionType>
  void FindOrderedHashTableEntryForHeapNumberKey(
      TNode<CollectionType> table, TNode<HeapNumber> key_heap_number,
      TVariable<IntPtrT>* result, Label* entry_found, Label* not_found);

  // Specialization for bigints.
  // The {result} variable will contain the entry index if the key was found,
  // or the hash code otherwise.
  void SameValueZeroBigInt(TNode<BigInt> key, TNode<Object> candidate_key,
                           Label* if_same, Label* if_not_same);
  template <typename CollectionType>
  void FindOrderedHashTableEntryForBigIntKey(TNode<CollectionType> table,
                                             TNode<BigInt> key_big_int,
                                             TVariable<IntPtrT>* result,
                                             Label* entry_found,
                                             Label* not_found);

  // Specialization for string.
  // The {result} variable will contain the entry index if the key was found,
  // or the hash code otherwise.
  template <typename CollectionType>
  void FindOrderedHashTableEntryForStringKey(TNode<CollectionType> table,
                                             TNode<String> key_tagged,
                                             TVariable<IntPtrT>* result,
                                             Label* entry_found,
                                             Label* not_found);
  TNode<Uint32T> ComputeStringHash(TNode<String> string_key);
  void SameValueZeroString(TNode<String> key_string,
                           TNode<Object> candidate_key, Label* if_same,
                           Label* if_not_same);

  // Specialization for non-strings, non-numbers. For those we only need
  // reference equality to compare the keys.
  // The {result} variable will contain the entry index if the key was found,
  // or the hash code otherwise. If the hash-code has not been computed, it
  // should be Smi -1.
  template <typename CollectionType>
  void FindOrderedHashTableEntryForOtherKey(TNode<CollectionType> table,
                                            TNode<HeapObject> key_heap_object,
                                            TVariable<IntPtrT>* result,
                                            Label* entry_found,
                                            Label* not_found);

  // Generates code to add an entry keyed by {key} to an instance of
  // OrderedHashTable subclass {table}.
  //
  // Takes 3 functions:
  //   - {grow} generates code to return a OrderedHashTable subclass instance
  //     with space to store the entry.
  //   - {store_new_entry} generates code to store into a new entry, for the
  //     case when {table} didn't already have an entry keyed by {key}.
  //   - {store_existing_entry} generates code to store into an existing entry,
  //     for the case when {table} already has an entry keyed by {key}.
  //
  // Both {store_new_entry} and {store_existing_entry} take the table and an
  // offset to the entry as parameters.
  template <typename CollectionType>
  using GrowCollection = std::function<const TNode<CollectionType>()>;
  template <typename CollectionType>
  using StoreAtEntry = std::function<void(const TNode<CollectionType> table,
                                          const TNode<IntPtrT> entry_start)>;
  template <typename CollectionType>
  TNode<CollectionType> AddToOrderedHashTable(
      const TNode<CollectionType> table, const TNode<Object> key,
      const GrowCollection<CollectionType>& grow,
      const StoreAtEntry<CollectionType>& store_at_new_entry,
      const StoreAtEntry<CollectionType>& store_at_existing_entry);

  template <typename CollectionType>
  void TryLookupOrderedHashTableIndex(const TNode<CollectionType> table,
                                      const TNode<Object> key,
                                      TVariable<IntPtrT>* result,
                                      Label* if_entry_found,
                                      Label* if_not_found);

  // Helper function to store a new entry when constructing sets from sets.
  template <typename CollectionType>
  void AddNewToOrderedHashTable(
      const TNode<CollectionType> table, const TNode<Object> normalised_key,
      const TNode<IntPtrT> number_of_buckets, const TNode<IntPtrT> occupancy,
      const StoreAtEntry<CollectionType>& store_at_new_entry);

  void AddNewToOrderedHashSet(const TNode<OrderedHashSet> table,
                              const TNode<Object> key,
                              const TNode<IntPtrT> number_of_buckets,
                              const TNode<IntPtrT> occupancy) {
    TNode<Object> normalised_key = NormalizeNumberKey(key);
    StoreAtEntry<OrderedHashSet> store_at_new_entry =
        [this, normalised_key](const TNode<OrderedHashSet> table,
                               const TNode<IntPtrT> entry_start) {
          UnsafeStoreKeyInOrderedHashSetEntry(table, normalised_key,
                                              entry_start);
        };
    AddNewToOrderedHashTable<OrderedHashSet>(table, normalised_key,
                                             number_of_buckets, occupancy,
                                             store_at_new_entry);
  }

  // Generates code to store a new entry into {table}, connecting to the bucket
  // chain, and updating the bucket head. {store_new_entry} is called to
  // generate the code to store the payload (e.g., the key and value for
  // OrderedHashMap).
  template <typename CollectionType>
  void StoreOrderedHashTableNewEntry(
      const TNode<CollectionType> table, const TNode<IntPtrT> hash,
      const TNode<IntPtrT> number_of_buckets, const TNode<IntPtrT> occupancy,
      const StoreAtEntry<CollectionType>& store_at_new_entry);

  // Store payload (key, value, or both) in {table} at {entry}. Does not connect
  // the bucket chain and update the bucket head.
  void StoreValueInOrderedHashMapEntry(
      const TNode<OrderedHashMap> table, const TNode<Object> value,
      const TNode<IntPtrT> entry,
      CheckBounds check_bounds = CheckBounds::kAlways);
  void StoreKeyValueInOrderedHashMapEntry(
      const TNode<OrderedHashMap> table, const TNode<Object> key,
      const TNode<Object> value, const TNode<IntPtrT> entry,
      CheckBounds check_bounds = CheckBounds::kAlways);
  void StoreKeyInOrderedHashSetEntry(
      const TNode<OrderedHashSet> table, const TNode<Object> key,
      const TNode<IntPtrT> entry,
      CheckBounds check_bounds = CheckBounds::kAlways);

  void UnsafeStoreKeyValueInOrderedHashMapEntry(
      const TNode<OrderedHashMap> table, const TNode<Object> key,
      const TNode<Object> value, const TNode<IntPtrT> entry) {
    return StoreKeyValueInOrderedHashMapEntry(table, key, value, entry,
                                              CheckBounds::kDebugOnly);
  }
  void UnsafeStoreKeyInOrderedHashSetEntry(const TNode<OrderedHashSet> table,
                                           const TNode<Object> key,
                                           const TNode<IntPtrT> entry) {
    return StoreKeyInOrderedHashSetEntry(table, key, entry,
                                         CheckBounds::kDebugOnly);
  }

  TNode<Object> LoadValueFromOrderedHashMapEntry(
      const TNode<OrderedHashMap> table, const TNode<IntPtrT> entry,
      CheckBounds check_bounds = CheckBounds::kAlways);

  TNode<Object> UnsafeLoadValueFromOrderedHashMapEntry(
      const TNode<OrderedHashMap> table, const TNode<IntPtrT> entry) {
    return LoadValueFromOrderedHashMapEntry(table, entry,
                                            CheckBounds::kDebugOnly);
  }

  // Load payload (key or value) from {table} at {entry}.
  template <typename CollectionType>
  TNode<Object> LoadKeyFromOrderedHashTableEntry(
      const TNode<CollectionType> table, const TNode<IntPtrT> entry,
      CheckBounds check_bounds = CheckBounds::kAlways);

  template <typename CollectionType>
  TNode<Object> UnsafeLoadKeyFromOrderedHashTableEntry(
      const TNode<CollectionType> table, const TNode<IntPtrT> entry) {
    return LoadKeyFromOrderedHashTableEntry(table, entry,
                                            CheckBounds::kDebugOnly);
  }

  // Create a JSArray with PACKED_ELEMENTS kind from a Map.prototype.keys() or
  // Map.prototype.values() iterator. The iterator is assumed to satisfy
  // IterableWithOriginalKeyOrValueMapIterator. This function will skip the
  // iterator and iterate directly on the underlying hash table. In the end it
  // will update the state of the iterator to 'exhausted'.
  TNode<JSArray> MapIteratorToList(TNode<Context> context,
                                   TNode<JSMapIterator> iterator);

  // Create a JSArray with PACKED_ELEMENTS kind from a Set.prototype.keys() or
  // Set.prototype.values() iterator, or a Set. The |iterable| is assumed to
  // satisfy IterableWithOriginalValueSetIterator. This function will skip the
  // iterator and iterate directly on the underlying hash table. In the end, if
  // |iterable| is an iterator, it will update the state of the iterator to
  // 'exhausted'.
  TNode<JSArray> SetOrSetIteratorToList(TNode<Context> context,
                                        TNode<HeapObject> iterable);

  void BranchIfMapIteratorProtectorValid(Label* if_true, Label* if_false);
  void BranchIfSetIteratorProtectorValid(Label* if_true, Label* if_false);

  // Builds code that finds OrderedHashTable entry for a key with hash code
  // {hash} with using the comparison code generated by {key_compare}. The code
  // jumps to {entry_found} if the key is found, or to {not_found} if the key
  // was not found. In the {entry_found} branch, the variable
  // entry_start_position will be bound to the index of the entry (relative to
  // OrderedHashTable::kHashTableStartIndex).
  //
  // The {CollectionType} template parameter stands for the particular instance
  // of OrderedHashTable, it should be OrderedHashMap or OrderedHashSet.
  template <typename CollectionType>
  void FindOrderedHashTableEntry(
      const TNode<CollectionType> table, const TNode<Uint32T> hash,
      const std::function<void(TNode<Object>, Label*, Label*)>& key_compare,
      TVariable<IntPtrT>* entry_start_position, Label* entry_found,
      Label* not_found);

  TNode<Word32T> ComputeUnseededHash(TNode<IntPtrT> key);
};

class WeakCollectionsBuiltinsAssembler : public BaseCollectionsAssembler {
 public:
  explicit WeakCollectionsBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : BaseCollectionsAssembler(state) {}

 protected:
  void AddEntry(TNode<EphemeronHashTable> table, TNode<IntPtrT> key_index,
                TNode<Object> key, TNode<Object> value,
                TNode<Int32T> number_of_elements);

  TNode<HeapObject> AllocateTable(Variant variant,
                                  TNode<IntPtrT> at_least_space_for) override;

  TNode<IntPtrT> GetHash(const TNode<HeapObject> key, Label* if_no_hash);
  // Generates and sets the identity for a JSRececiver.
  TNode<Smi> CreateIdentityHash(TNode<Object> receiver);
  TNode<IntPtrT> EntryMask(TNode<IntPtrT> capacity);

  // Builds code that finds the EphemeronHashTable entry for a {key} using the
  // comparison code generated by {key_compare}. The key index is returned if
  // the {key} is found.
  using KeyComparator =
      std::function<void(TNode<Object> entry_key, Label* if_same)>;
  TNode<IntPtrT> FindKeyIndex(TNode<HeapObject> table, TNode<IntPtrT> key_hash,
                              TNode<IntPtrT> entry_mask,
                              const KeyComparator& key_compare);

  // Builds code that finds an EphemeronHashTable entry available for a new
  // entry.
  TNode<IntPtrT> FindKeyIndexForInsertion(TNode<HeapObject> table,
                                          TNode<IntPtrT> key_hash,
                                          TNode<IntPtrT> entry_mask);

  // Builds code that finds the EphemeronHashTable entry with key that matches
  // {key} and returns the entry's key index. If {key} cannot be found, jumps to
  // {if_not_found}.
  TNode<IntPtrT> FindKeyIndexForKey(TNode<HeapObject> table, TNode<Object> key,
                                    TNode<IntPtrT> hash,
                                    TNode<IntPtrT> entry_mask,
                                    Label* if_not_found);

  TNode<Word32T> InsufficientCapacityToAdd(TNode<Int32T> capacity,
                                           TNode<Int32T> number_of_elements,
                                           TNode<Int32T> number_of_deleted);
  TNode<IntPtrT> KeyIndexFromEntry(TNode<IntPtrT> entry);

  TNode<Int32T> LoadNumberOfElements(TNode<EphemeronHashTable> table,
                                     int offset);
  TNode<Int32T> LoadNumberOfDeleted(TNode<EphemeronHashTable> table,
                                    int offset = 0);
  TNode<EphemeronHashTable> LoadTable(TNode<JSWeakCollection> collection);
  TNode<IntPtrT> LoadTableCapacity(TNode<EphemeronHashTable> table);

  void RemoveEntry(TNode<EphemeronHashTable> table, TNode<IntPtrT> key_index,
                   TNode<IntPtrT> number_of_elements);
  TNode<BoolT> ShouldRehash(TNode<Int32T> number_of_elements,
                            TNode<Int32T> number_of_deleted);
  TNode<Word32T> ShouldShrink(TNode<IntPtrT> capacity,
                              TNode<IntPtrT> number_of_elements);
  TNode<IntPtrT> ValueIndexFromKeyIndex(TNode<IntPtrT> key_index);

  void GetEntriesIfFastCollectionOrIterable(
      Variant variant, TNode<Object> initial_entries, TNode<Context> context,
      TVariable<HeapObject>* var_entries_table,
      TVariable<IntPtrT>* var_number_of_elements,
      Label* if_not_fast_collection) override {
    UNREACHABLE();
  }

  void AddConstructorEntriesFromFastCollection(
      Variant variant, TNode<HeapObject> collection,
      TNode<HeapObject> source_table) override {
    UNREACHABLE();
  }
};

// Controls the key coercion behavior for Object.groupBy and Map.groupBy.
enum class GroupByCoercionMode { kZero, kProperty };

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_COLLECTIONS_GEN_H_
