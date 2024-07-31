// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_COMPILATION_CACHE_TABLE_INL_H_
#define V8_OBJECTS_COMPILATION_CACHE_TABLE_INL_H_

#include "src/objects/compilation-cache-table.h"
#include "src/objects/name-inl.h"
#include "src/objects/script-inl.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/smi.h"
#include "src/objects/string.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

CompilationCacheTable::CompilationCacheTable(Address ptr)
    : HashTable<CompilationCacheTable, CompilationCacheShape>(ptr) {
  SLOW_DCHECK(IsCompilationCacheTable(*this));
}

NEVER_READ_ONLY_SPACE_IMPL(CompilationCacheTable)
CAST_ACCESSOR(CompilationCacheTable)

Tagged<Object> CompilationCacheTable::PrimaryValueAt(InternalIndex entry) {
  return get(EntryToIndex(entry) + 1);
}

void CompilationCacheTable::SetPrimaryValueAt(InternalIndex entry,
                                              Tagged<Object> value,
                                              WriteBarrierMode mode) {
  set(EntryToIndex(entry) + 1, value, mode);
}

Tagged<Object> CompilationCacheTable::EvalFeedbackValueAt(InternalIndex entry) {
  static_assert(CompilationCacheShape::kEntrySize == 3);
  return get(EntryToIndex(entry) + 2);
}

void CompilationCacheTable::SetEvalFeedbackValueAt(InternalIndex entry,
                                                   Tagged<Object> value,
                                                   WriteBarrierMode mode) {
  set(EntryToIndex(entry) + 2, value, mode);
}

// The key in a script cache is a WeakFixedArray containing a weak pointer to
// the Script. The corresponding value can be either the root SharedFunctionInfo
// or undefined. The purpose of storing the root SharedFunctionInfo as the value
// is to keep it alive, not to save a lookup on the Script. A newly added entry
// always contains the root SharedFunctionInfo. After the root
// SharedFunctionInfo has aged sufficiently, it is replaced with undefined. In
// this way, all strong references to large objects are dropped, but there is
// still a way to get the Script if it happens to still be alive.
class ScriptCacheKey : public HashTableKey {
 public:
  enum Index {
    kHash,
    kWeakScript,
    kEnd,
  };

  ScriptCacheKey(Handle<String> source, const ScriptDetails* script_details,
                 Isolate* isolate);
  ScriptCacheKey(Handle<String> source, MaybeHandle<Object> name,
                 int line_offset, int column_offset,
                 v8::ScriptOriginOptions origin_options,
                 MaybeHandle<Object> host_defined_options,
                 MaybeHandle<FixedArray> maybe_wrapped_arguments,
                 Isolate* isolate);

  bool IsMatch(Tagged<Object> other) override;
  bool MatchesScript(Tagged<Script> script);

  Handle<Object> AsHandle(Isolate* isolate, Handle<SharedFunctionInfo> shared);

  static base::Optional<Tagged<String>> SourceFromObject(Tagged<Object> obj) {
    DisallowGarbageCollection no_gc;
    DCHECK(IsWeakFixedArray(obj));
    Tagged<WeakFixedArray> array = WeakFixedArray::cast(obj);
    DCHECK_EQ(array->length(), kEnd);

    Tagged<MaybeObject> maybe_script = array->get(kWeakScript);
    if (Tagged<HeapObject> script; maybe_script.GetHeapObjectIfWeak(&script)) {
      Tagged<PrimitiveHeapObject> source_or_undefined =
          Script::cast(script)->source();
      // Scripts stored in the script cache should always have a source string.
      return String::cast(source_or_undefined);
    }

    DCHECK(maybe_script.IsCleared());
    return {};
  }

 private:
  Handle<String> source_;
  MaybeHandle<Object> name_;
  int line_offset_;
  int column_offset_;
  v8::ScriptOriginOptions origin_options_;
  MaybeHandle<Object> host_defined_options_;
  MaybeHandle<FixedArray> wrapped_arguments_;
  Isolate* isolate_;
};

uint32_t CompilationCacheShape::RegExpHash(Tagged<String> string,
                                           Tagged<Smi> flags) {
  return string->EnsureHash() + flags.value();
}

uint32_t CompilationCacheShape::EvalHash(Tagged<String> source,
                                         Tagged<SharedFunctionInfo> shared,
                                         LanguageMode language_mode,
                                         int position) {
  uint32_t hash = source->EnsureHash();
  if (shared->HasSourceCode()) {
    // Instead of using the SharedFunctionInfo pointer in the hash
    // code computation, we use a combination of the hash of the
    // script source code and the start position of the calling scope.
    // We do this to ensure that the cache entries can survive garbage
    // collection.
    Tagged<Script> script(Script::cast(shared->script()));
    hash ^= String::cast(script->source())->EnsureHash();
  }
  static_assert(LanguageModeSize == 2);
  if (is_strict(language_mode)) hash ^= 0x8000;
  hash += position;
  return hash;
}

uint32_t CompilationCacheShape::HashForObject(ReadOnlyRoots roots,
                                              Tagged<Object> object) {
  // Eval: The key field contains the hash as a Number.
  if (IsNumber(object)) return static_cast<uint32_t>(Object::Number(object));

  // Code: The key field contains the SFI key.
  if (IsSharedFunctionInfo(object)) {
    return SharedFunctionInfo::cast(object)->Hash();
  }

  // Script.
  if (IsWeakFixedArray(object)) {
    return static_cast<uint32_t>(Smi::ToInt(
        WeakFixedArray::cast(object)->get(ScriptCacheKey::kHash).ToSmi()));
  }

  // Eval: See EvalCacheKey::ToHandle for the encoding.
  Tagged<FixedArray> val = FixedArray::cast(object);
  if (val->map() == roots.fixed_cow_array_map()) {
    DCHECK_EQ(4, val->length());
    Tagged<String> source = String::cast(val->get(1));
    int language_unchecked = Smi::ToInt(val->get(2));
    DCHECK(is_valid_language_mode(language_unchecked));
    LanguageMode language_mode = static_cast<LanguageMode>(language_unchecked);
    int position = Smi::ToInt(val->get(3));
    Tagged<Object> shared = val->get(0);
    return EvalHash(source, SharedFunctionInfo::cast(shared), language_mode,
                    position);
  }

  // RegExp: The key field (and the value field) contains the
  // JSRegExp::data fixed array.
  DCHECK_GE(val->length(), JSRegExp::kMinDataArrayLength);
  return RegExpHash(String::cast(val->get(JSRegExp::kSourceIndex)),
                    Smi::cast(val->get(JSRegExp::kFlagsIndex)));
}

InfoCellPair::InfoCellPair(Isolate* isolate, Tagged<SharedFunctionInfo> shared,
                           Tagged<FeedbackCell> feedback_cell)
    : is_compiled_scope_(!shared.is_null() ? shared->is_compiled_scope(isolate)
                                           : IsCompiledScope()),
      shared_(shared),
      feedback_cell_(feedback_cell) {}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_COMPILATION_CACHE_TABLE_INL_H_
