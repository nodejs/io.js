// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/constants-table-builder.h"

#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"
#include "src/objects/oddball-inl.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {

BuiltinsConstantsTableBuilder::BuiltinsConstantsTableBuilder(Isolate* isolate)
    : isolate_(isolate), map_(isolate->heap()) {
  // Ensure this is only called once per Isolate.
  DCHECK_EQ(ReadOnlyRoots(isolate_).empty_fixed_array(),
            isolate_->heap()->builtins_constants_table());

  // And that the initial value of the builtins constants table can be treated
  // as a constant, which means that codegen will load it using the root
  // register.
  DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kEmptyFixedArray));
}

uint32_t BuiltinsConstantsTableBuilder::AddObject(Handle<Object> object) {
#ifdef DEBUG
  // Roots must not be inserted into the constants table as they are already
  // accessibly from the root list.
  RootIndex root_list_index;
  DCHECK(!isolate_->roots_table().IsRootHandle(object, &root_list_index));
  DCHECK_IMPLIES(IsMap(*object), !InReadOnlySpace(HeapObject::cast(*object)));

  // Not yet finalized.
  DCHECK_EQ(ReadOnlyRoots(isolate_).empty_fixed_array(),
            isolate_->heap()->builtins_constants_table());

  // Must be on the main thread.
  DCHECK_EQ(ThreadId::Current(), isolate_->thread_id());

  // Must be generating embedded builtin code.
  DCHECK(isolate_->IsGeneratingEmbeddedBuiltins());

  // All code objects should be loaded through the root register or use
  // pc-relative addressing.
  DCHECK(!IsInstructionStream(*object));
#endif

  auto find_result = map_.FindOrInsert(object);
  if (!find_result.already_exists) {
    DCHECK(IsHeapObject(*object));
    *find_result.entry = map_.size() - 1;
  }
  return *find_result.entry;
}

namespace {
void CheckPreconditionsForPatching(Isolate* isolate,
                                   Handle<Object> replacement_object) {
  // Roots must not be inserted into the constants table as they are already
  // accessible from the root list.
  RootIndex root_list_index;
  DCHECK(!isolate->roots_table().IsRootHandle(replacement_object,
                                              &root_list_index));
  USE(root_list_index);

  // Not yet finalized.
  DCHECK_EQ(ReadOnlyRoots(isolate).empty_fixed_array(),
            isolate->heap()->builtins_constants_table());

  DCHECK(isolate->IsGeneratingEmbeddedBuiltins());
}
}  // namespace

void BuiltinsConstantsTableBuilder::PatchSelfReference(
    Handle<Object> self_reference, Handle<InstructionStream> code_object) {
  CheckPreconditionsForPatching(isolate_, code_object);
  DCHECK_EQ(*self_reference, ReadOnlyRoots(isolate_).self_reference_marker());

  uint32_t key;
  if (map_.Delete(self_reference, &key)) {
    DCHECK(IsInstructionStream(*code_object));
    map_.Insert(code_object, key);
  }
}

void BuiltinsConstantsTableBuilder::PatchBasicBlockCountersReference(
    Handle<ByteArray> counters) {
  CheckPreconditionsForPatching(isolate_, counters);

  uint32_t key;
  if (map_.Delete(ReadOnlyRoots(isolate_).basic_block_counters_marker(),
                  &key)) {
    map_.Insert(counters, key);
  }
}

void BuiltinsConstantsTableBuilder::Finalize() {
  HandleScope handle_scope(isolate_);

  DCHECK_EQ(ReadOnlyRoots(isolate_).empty_fixed_array(),
            isolate_->heap()->builtins_constants_table());
  DCHECK(isolate_->IsGeneratingEmbeddedBuiltins());

  // An empty map means there's nothing to do.
  if (map_.empty()) return;

  Handle<FixedArray> table =
      isolate_->factory()->NewFixedArray(map_.size(), AllocationType::kOld);

  Builtins* builtins = isolate_->builtins();
  ConstantsMap::IteratableScope it_scope(&map_);
  for (auto it = it_scope.begin(); it != it_scope.end(); ++it) {
    uint32_t index = *it.entry();
    Tagged<Object> value = it.key();
    if (IsCode(value) && Code::cast(value)->kind() == CodeKind::BUILTIN) {
      // Replace placeholder code objects with the real builtin.
      // See also: SetupIsolateDelegate::PopulateWithPlaceholders.
      // TODO(jgruber): Deduplicate placeholders and their corresponding
      // builtin.
      value = builtins->code(Code::cast(value)->builtin_id());
    }
    DCHECK(IsHeapObject(value));
    table->set(index, value);
  }

#ifdef DEBUG
  for (int i = 0; i < map_.size(); i++) {
    DCHECK(IsHeapObject(table->get(i)));
    DCHECK_NE(ReadOnlyRoots(isolate_).undefined_value(), table->get(i));
    DCHECK_NE(ReadOnlyRoots(isolate_).self_reference_marker(), table->get(i));
    DCHECK_NE(ReadOnlyRoots(isolate_).basic_block_counters_marker(),
              table->get(i));
  }
#endif

  isolate_->heap()->SetBuiltinsConstantsTable(*table);
}

}  // namespace internal
}  // namespace v8
