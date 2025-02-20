// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-property-iterator.h"

#include "src/api/api-inl.h"
#include "src/base/flags.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/keys.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/property-details.h"

namespace v8 {
namespace internal {

std::unique_ptr<DebugPropertyIterator> DebugPropertyIterator::Create(
    Isolate* isolate, DirectHandle<JSReceiver> receiver, bool skip_indices) {
  // Can't use std::make_unique as Ctor is private.
  auto iterator = std::unique_ptr<DebugPropertyIterator>(
      new DebugPropertyIterator(isolate, receiver, skip_indices));

  if (IsJSProxy(*receiver)) {
    iterator->AdvanceToPrototype();
  }

  if (!iterator->FillKeysForCurrentPrototypeAndStage()) return nullptr;
  if (iterator->should_move_to_next_stage() && !iterator->AdvanceInternal()) {
    return nullptr;
  }

  return iterator;
}

DebugPropertyIterator::DebugPropertyIterator(Isolate* isolate,
                                             DirectHandle<JSReceiver> receiver,
                                             bool skip_indices)
    : isolate_(isolate),
      prototype_iterator_(isolate, receiver, kStartAtReceiver,
                          PrototypeIterator::END_AT_NULL),
      skip_indices_(skip_indices),
      current_key_index_(0),
      current_keys_(isolate_->factory()->empty_fixed_array()),
      current_keys_length_(0) {}

bool DebugPropertyIterator::Done() const { return is_done_; }

void DebugPropertyIterator::AdvanceToPrototype() {
  stage_ = kExoticIndices;
  is_own_ = false;
  if (!prototype_iterator_.HasAccess()) is_done_ = true;
  prototype_iterator_.AdvanceIgnoringProxies();
  if (prototype_iterator_.IsAtEnd()) is_done_ = true;
}

bool DebugPropertyIterator::AdvanceInternal() {
  ++current_key_index_;
  calculated_native_accessor_flags_ = false;
  while (should_move_to_next_stage()) {
    switch (stage_) {
      case kExoticIndices:
        stage_ = kEnumerableStrings;
        break;
      case kEnumerableStrings:
        stage_ = kAllProperties;
        break;
      case kAllProperties:
        AdvanceToPrototype();
        break;
    }
    if (!FillKeysForCurrentPrototypeAndStage()) return false;
  }
  return true;
}

bool DebugPropertyIterator::is_native_accessor() {
  CalculateNativeAccessorFlags();
  return native_accessor_flags_;
}

bool DebugPropertyIterator::has_native_getter() {
  CalculateNativeAccessorFlags();
  return native_accessor_flags_ &
         static_cast<int>(debug::NativeAccessorType::HasGetter);
}

bool DebugPropertyIterator::has_native_setter() {
  CalculateNativeAccessorFlags();
  return native_accessor_flags_ &
         static_cast<int>(debug::NativeAccessorType::HasSetter);
}

DirectHandle<Name> DebugPropertyIterator::raw_name() const {
  DCHECK(!Done());
  if (stage_ == kExoticIndices) {
    return isolate_->factory()->SizeToString(current_key_index_);
  } else {
    return Cast<Name>(direct_handle(
        current_keys_->get(static_cast<int>(current_key_index_)), isolate_));
  }
}

v8::Local<v8::Name> DebugPropertyIterator::name() const {
  return Utils::ToLocal(raw_name());
}

v8::Maybe<v8::PropertyAttribute> DebugPropertyIterator::attributes() {
  DirectHandle<JSReceiver> receiver =
      PrototypeIterator::GetCurrent<JSReceiver>(prototype_iterator_);
  auto result =
      JSReceiver::GetPropertyAttributes(isolate_, receiver, raw_name());
  if (result.IsNothing()) return Nothing<v8::PropertyAttribute>();
  // This should almost never happen, however we have seen cases where we do
  // trigger this check. In these rare events, it typically is a
  // misconfiguration by an embedder (such as Blink) in how the embedder
  // processes properities.
  //
  // In the case of crbug.com/1262066 we discovered that Blink was returning
  // a list of properties to contain in an object, after which V8 queries each
  // property individually. But, Blink incorrectly claimed that the property
  // in question did *not* exist. As such, V8 is instructed to process a
  // property, requests the embedder for more information and then suddenly the
  // embedder claims it doesn't exist. In these cases, we hit this DCHECK.
  //
  // If you are running into this problem, check your embedder implementation
  // and verify that the data from both sides matches. If there is a mismatch,
  // V8 will crash.

#if DEBUG
  base::ScopedVector<char> property_message(128);
  base::ScopedVector<char> name_buffer(100);
  raw_name()->NameShortPrint(name_buffer);
  v8::base::SNPrintF(property_message, "Invalid result for property \"%s\"\n",
                     name_buffer.begin());
  DCHECK_WITH_MSG(result.FromJust() != ABSENT, property_message.begin());
#endif
  return Just(static_cast<v8::PropertyAttribute>(result.FromJust()));
}

v8::Maybe<v8::debug::PropertyDescriptor> DebugPropertyIterator::descriptor() {
  DirectHandle<JSReceiver> receiver =
      PrototypeIterator::GetCurrent<JSReceiver>(prototype_iterator_);

  PropertyDescriptor descriptor;
  Maybe<bool> did_get_descriptor = JSReceiver::GetOwnPropertyDescriptor(
      isolate_, receiver, raw_name(), &descriptor);
  if (did_get_descriptor.IsNothing()) {
    return Nothing<v8::debug::PropertyDescriptor>();
  }
  if (!did_get_descriptor.FromJust()) {
    return Just(v8::debug::PropertyDescriptor{
        false, false,           /* enumerable */
        false, false,           /* configurable */
        false, false,           /* writable */
        v8::Local<v8::Value>(), /* value */
        v8::Local<v8::Value>(), /* get */
        v8::Local<v8::Value>(), /* set */
    });
  }
  DCHECK(did_get_descriptor.FromJust());
  return Just(v8::debug::PropertyDescriptor{
      descriptor.enumerable(), descriptor.has_enumerable(),
      descriptor.configurable(), descriptor.has_configurable(),
      descriptor.writable(), descriptor.has_writable(),
      descriptor.has_value() ? Utils::ToLocal(descriptor.value())
                             : v8::Local<v8::Value>(),
      descriptor.has_get() ? Utils::ToLocal(descriptor.get())
                           : v8::Local<v8::Value>(),
      descriptor.has_set() ? Utils::ToLocal(descriptor.set())
                           : v8::Local<v8::Value>(),
  });
}

bool DebugPropertyIterator::is_own() { return is_own_; }

bool DebugPropertyIterator::is_array_index() {
  if (stage_ == kExoticIndices) return true;
  PropertyKey key(isolate_, raw_name());
  return key.is_element();
}

bool DebugPropertyIterator::FillKeysForCurrentPrototypeAndStage() {
  current_key_index_ = 0;
  current_keys_ = isolate_->factory()->empty_fixed_array();
  current_keys_length_ = 0;
  if (is_done_) return true;
  DirectHandle<JSReceiver> receiver =
      PrototypeIterator::GetCurrent<JSReceiver>(prototype_iterator_);
  if (stage_ == kExoticIndices) {
    if (skip_indices_ || !IsJSTypedArray(*receiver)) return true;
    auto typed_array = Cast<JSTypedArray>(receiver);
    current_keys_length_ =
        typed_array->WasDetached() ? 0 : typed_array->GetLength();
    return true;
  }
  PropertyFilter filter =
      stage_ == kEnumerableStrings ? ENUMERABLE_STRINGS : ALL_PROPERTIES;
  if (KeyAccumulator::GetKeys(isolate_, receiver, KeyCollectionMode::kOwnOnly,
                              filter, GetKeysConversion::kConvertToString,
                              false, skip_indices_ || IsJSTypedArray(*receiver))
          .ToHandle(&current_keys_)) {
    current_keys_length_ = current_keys_->length();
    return true;
  }
  return false;
}

bool DebugPropertyIterator::should_move_to_next_stage() const {
  return !is_done_ && current_key_index_ >= current_keys_length_;
}

namespace {
base::Flags<debug::NativeAccessorType, int> GetNativeAccessorDescriptorInternal(
    DirectHandle<JSReceiver> object, DirectHandle<Name> name) {
  Isolate* isolate = object->GetIsolate();
  PropertyKey key(isolate, name);
  if (key.is_element()) return debug::NativeAccessorType::None;
  LookupIterator it(isolate, object, key, LookupIterator::OWN);
  if (!it.IsFound()) return debug::NativeAccessorType::None;
  if (it.state() != LookupIterator::ACCESSOR) {
    return debug::NativeAccessorType::None;
  }
  DirectHandle<Object> structure = it.GetAccessors();
  if (!IsAccessorInfo(*structure)) return debug::NativeAccessorType::None;
  base::Flags<debug::NativeAccessorType, int> result;
  if (*structure == *isolate->factory()->value_unavailable_accessor()) {
    return debug::NativeAccessorType::IsValueUnavailable;
  }
#define IS_BUILTIN_ACCESSOR(_, name, ...)                   \
  if (*structure == *isolate->factory()->name##_accessor()) \
    return debug::NativeAccessorType::None;
  ACCESSOR_INFO_LIST_GENERATOR(IS_BUILTIN_ACCESSOR, /* not used */)
#undef IS_BUILTIN_ACCESSOR
  auto accessor_info = Cast<AccessorInfo>(structure);
  if (accessor_info->has_getter(isolate)) {
    result |= debug::NativeAccessorType::HasGetter;
  }
  if (accessor_info->has_setter(isolate)) {
    result |= debug::NativeAccessorType::HasSetter;
  }
  return result;
}
}  // anonymous namespace

void DebugPropertyIterator::CalculateNativeAccessorFlags() {
  if (calculated_native_accessor_flags_) return;
  if (stage_ == kExoticIndices) {
    native_accessor_flags_ = 0;
  } else {
    DirectHandle<JSReceiver> receiver =
        PrototypeIterator::GetCurrent<JSReceiver>(prototype_iterator_);
    native_accessor_flags_ =
        GetNativeAccessorDescriptorInternal(receiver, raw_name());
  }
  calculated_native_accessor_flags_ = true;
}
}  // namespace internal
}  // namespace v8
