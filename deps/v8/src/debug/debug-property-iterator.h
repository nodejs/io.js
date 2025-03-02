// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_PROPERTY_ITERATOR_H_
#define V8_DEBUG_DEBUG_PROPERTY_ITERATOR_H_

#include "include/v8-local-handle.h"
#include "include/v8-maybe.h"
#include "include/v8-object.h"
#include "src/debug/debug-interface.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/prototype.h"

namespace v8 {

class Name;

namespace internal {

class JSReceiver;

class DebugPropertyIterator final : public debug::PropertyIterator {
 public:
  V8_WARN_UNUSED_RESULT static std::unique_ptr<DebugPropertyIterator> Create(
      Isolate* isolate, DirectHandle<JSReceiver> receiver, bool skip_indices);
  ~DebugPropertyIterator() override = default;
  DebugPropertyIterator(const DebugPropertyIterator&) = delete;
  DebugPropertyIterator& operator=(const DebugPropertyIterator&) = delete;

  bool Done() const override;
  V8_WARN_UNUSED_RESULT Maybe<bool> Advance() override;

  v8::Local<v8::Name> name() const override;
  bool is_native_accessor() override;
  bool has_native_getter() override;
  bool has_native_setter() override;
  v8::Maybe<v8::PropertyAttribute> attributes() override;
  v8::Maybe<v8::debug::PropertyDescriptor> descriptor() override;

  bool is_own() override;
  bool is_array_index() override;

 private:
  DebugPropertyIterator(Isolate* isolate, DirectHandle<JSReceiver> receiver,
                        bool skip_indices);

  V8_WARN_UNUSED_RESULT bool FillKeysForCurrentPrototypeAndStage();
  bool should_move_to_next_stage() const;
  void CalculateNativeAccessorFlags();
  DirectHandle<Name> raw_name() const;
  void AdvanceToPrototype();
  V8_WARN_UNUSED_RESULT bool AdvanceInternal();

  Isolate* isolate_;
  PrototypeIterator prototype_iterator_;
  enum {
    kExoticIndices = 0,
    kEnumerableStrings = 1,
    kAllProperties = 2
  } stage_ = kExoticIndices;
  bool skip_indices_;

  size_t current_key_index_;
  Handle<FixedArray> current_keys_;
  size_t current_keys_length_;

  bool calculated_native_accessor_flags_ = false;
  int native_accessor_flags_ = 0;
  bool is_own_ = true;
  bool is_done_ = false;
};
}  // namespace internal
}  // namespace v8

#endif  // V8_DEBUG_DEBUG_PROPERTY_ITERATOR_H_
