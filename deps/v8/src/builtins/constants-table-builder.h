// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_CONSTANTS_TABLE_BUILDER_H_
#define V8_BUILTINS_CONSTANTS_TABLE_BUILDER_H_

#include "src/base/macros.h"
#include "src/utils/allocation.h"
#include "src/utils/identity-map.h"
#include "src/handles/handles.h"

namespace v8 {
namespace internal {

class Isolate;
class Object;
class ByteArray;

// Utility class to build the builtins constants table and store it on the root
// list. The constants table contains constants used by builtins, and is there
// to avoid directly embedding them into code objects, which would not be
// possible for off-heap (and thus immutable) code objects.
class BuiltinsConstantsTableBuilder final {
 public:
  explicit BuiltinsConstantsTableBuilder(Isolate* isolate);

  BuiltinsConstantsTableBuilder(const BuiltinsConstantsTableBuilder&) = delete;
  BuiltinsConstantsTableBuilder& operator=(
      const BuiltinsConstantsTableBuilder&) = delete;

  // Returns the index within the builtins constants table for the given
  // object, possibly adding the object to the table. Objects are deduplicated.
  uint32_t AddObject(Handle<Object> object);

  // Self-references during code generation start out by referencing a handle
  // with a temporary dummy object. Once the final InstructionStream object
  // exists, such entries in the constants map must be patched up.
  void PatchSelfReference(DirectHandle<Object> self_reference,
                          Handle<InstructionStream> code_object);

  // References to the array that stores basic block usage counters start out as
  // references to a unique oddball. Once the actual array has been allocated,
  // such entries in the constants map must be patched up.
  void PatchBasicBlockCountersReference(Handle<ByteArray> counters);

  // Should be called after all affected code (e.g. builtins and bytecode
  // handlers) has been generated.
  void Finalize();

 private:
  Isolate* isolate_;

  // Maps objects to corresponding indices within the constants list.
  using ConstantsMap = IdentityMap<uint32_t, FreeStoreAllocationPolicy>;
  ConstantsMap map_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_CONSTANTS_TABLE_BUILDER_H_
