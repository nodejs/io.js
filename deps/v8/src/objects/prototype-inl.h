// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROTOTYPE_INL_H_
#define V8_OBJECTS_PROTOTYPE_INL_H_

#include "src/objects/prototype.h"

#include "src/handles/handles-inl.h"
#include "src/objects/js-proxy.h"
#include "src/objects/map-inl.h"

namespace v8 {
namespace internal {

PrototypeIterator::PrototypeIterator(Isolate* isolate,
                                     Handle<JSReceiver> receiver,
                                     WhereToStart where_to_start,
                                     WhereToEnd where_to_end)
    : isolate_(isolate),
      handle_(receiver),
      where_to_end_(where_to_end),
      is_at_end_(false),
      seen_proxies_(0) {
  CHECK(!handle_.is_null());
  if (where_to_start == kStartAtPrototype) Advance();
}

PrototypeIterator::PrototypeIterator(Isolate* isolate,
                                     Tagged<JSReceiver> receiver,
                                     WhereToStart where_to_start,
                                     WhereToEnd where_to_end)
    : isolate_(isolate),
      object_(receiver),
      where_to_end_(where_to_end),
      is_at_end_(false),
      seen_proxies_(0) {
  if (where_to_start == kStartAtPrototype) Advance();
}

PrototypeIterator::PrototypeIterator(Isolate* isolate, Tagged<Map> receiver_map,
                                     WhereToEnd where_to_end)
    : isolate_(isolate),
      object_(receiver_map->GetPrototypeChainRootMap(isolate_)->prototype()),
      where_to_end_(where_to_end),
      is_at_end_(IsNull(object_, isolate_)),
      seen_proxies_(0) {
  if (!is_at_end_ && where_to_end_ == END_AT_NON_HIDDEN) {
    DCHECK(IsJSReceiver(object_));
    Tagged<Map> map = Cast<JSReceiver>(object_)->map();
    is_at_end_ = !IsJSGlobalProxyMap(map);
  }
}

PrototypeIterator::PrototypeIterator(Isolate* isolate,
                                     DirectHandle<Map> receiver_map,
                                     WhereToEnd where_to_end)
    : isolate_(isolate),
      handle_(receiver_map->GetPrototypeChainRootMap(isolate_)->prototype(),
              isolate_),
      where_to_end_(where_to_end),
      is_at_end_(IsNull(*handle_, isolate_)),
      seen_proxies_(0) {
  if (!is_at_end_ && where_to_end_ == END_AT_NON_HIDDEN) {
    DCHECK(IsJSReceiver(*handle_));
    Tagged<Map> map = Cast<JSReceiver>(*handle_)->map();
    is_at_end_ = !IsJSGlobalProxyMap(map);
  }
}

bool PrototypeIterator::HasAccess() const {
  // We can only perform access check in the handlified version of the
  // PrototypeIterator.
  DCHECK(!handle_.is_null());
  if (IsAccessCheckNeeded(*handle_)) {
    return isolate_->MayAccess(isolate_->native_context(),
                               Cast<JSObject>(handle_));
  }
  return true;
}

void PrototypeIterator::Advance() {
  if (handle_.is_null() && IsJSProxy(object_)) {
    is_at_end_ = true;
    object_ = ReadOnlyRoots(isolate_).null_value();
    return;
  } else if (!handle_.is_null() && IsJSProxy(*handle_)) {
    is_at_end_ = true;
    handle_ = isolate_->factory()->null_value();
    return;
  }
  AdvanceIgnoringProxies();
}

void PrototypeIterator::AdvanceIgnoringProxies() {
  Tagged<Object> object = handle_.is_null() ? object_ : *handle_;
  Tagged<Map> map = Cast<HeapObject>(object)->map();

  Tagged<HeapObject> prototype = map->prototype();
  is_at_end_ = IsNull(prototype, isolate_) ||
               (where_to_end_ == END_AT_NON_HIDDEN && !IsJSGlobalProxyMap(map));

  if (handle_.is_null()) {
    object_ = prototype;
  } else {
    handle_ = handle(prototype, isolate_);
  }
}

V8_WARN_UNUSED_RESULT bool PrototypeIterator::AdvanceFollowingProxies() {
  DCHECK(!(handle_.is_null() && IsJSProxy(object_)));
  if (!HasAccess()) {
    // Abort the lookup if we do not have access to the current object.
    handle_ = isolate_->factory()->null_value();
    is_at_end_ = true;
    return true;
  }
  return AdvanceFollowingProxiesIgnoringAccessChecks();
}

V8_WARN_UNUSED_RESULT bool
PrototypeIterator::AdvanceFollowingProxiesIgnoringAccessChecks() {
  if (handle_.is_null() || !IsJSProxy(*handle_)) {
    AdvanceIgnoringProxies();
    return true;
  }

  // Due to possible __proto__ recursion limit the number of Proxies
  // we visit to an arbitrarily chosen large number.
  seen_proxies_++;
  if (seen_proxies_ > JSProxy::kMaxIterationLimit) {
    isolate_->StackOverflow();
    return false;
  }
  MaybeHandle<HeapObject> proto = JSProxy::GetPrototype(Cast<JSProxy>(handle_));
  if (!proto.ToHandle(&handle_)) return false;
  is_at_end_ = where_to_end_ == END_AT_NON_HIDDEN || IsNull(*handle_, isolate_);
  return true;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_PROTOTYPE_INL_H_
