// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROTOTYPE_H_
#define V8_OBJECTS_PROTOTYPE_H_

#include "src/execution/isolate.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

/**
 * A class to uniformly access the prototype of any Object and walk its
 * prototype chain.
 *
 * The PrototypeIterator can either start at the prototype (default), or
 * include the receiver itself. If a PrototypeIterator is constructed for a
 * Map, it will always start at the prototype.
 *
 * The PrototypeIterator can either run to the null_value(), the first
 * non-hidden prototype, or a given object.
 */

class PrototypeIterator {
 public:
  enum WhereToEnd { END_AT_NULL, END_AT_NON_HIDDEN };

  inline PrototypeIterator(Isolate* isolate, DirectHandle<JSReceiver> receiver,
                           WhereToStart where_to_start = kStartAtPrototype,
                           WhereToEnd where_to_end = END_AT_NULL);

  inline PrototypeIterator(Isolate* isolate, Tagged<JSReceiver> receiver,
                           WhereToStart where_to_start = kStartAtPrototype,
                           WhereToEnd where_to_end = END_AT_NULL);

  inline explicit PrototypeIterator(Isolate* isolate, Tagged<Map> receiver_map,
                                    WhereToEnd where_to_end = END_AT_NULL);

  inline explicit PrototypeIterator(Isolate* isolate,
                                    DirectHandle<Map> receiver_map,
                                    WhereToEnd where_to_end = END_AT_NULL);

  ~PrototypeIterator() = default;
  PrototypeIterator(const PrototypeIterator&) = delete;
  PrototypeIterator& operator=(const PrototypeIterator&) = delete;

  inline bool HasAccess() const;

  template <typename T = JSPrototype>
  Tagged<T> GetCurrent() const {
    DCHECK(handle_.is_null());
    return Cast<T>(object_);
  }

  template <typename T = JSPrototype>
  static DirectHandle<T> GetCurrent(const PrototypeIterator& iterator) {
    DCHECK(!iterator.handle_.is_null());
    DCHECK_EQ(iterator.object_, Tagged<HeapObject>());
    return Cast<T>(iterator.handle_);
  }

  inline void Advance();

  inline void AdvanceIgnoringProxies();

  // Returns false iff a call to JSProxy::GetPrototype throws.
  V8_WARN_UNUSED_RESULT inline bool AdvanceFollowingProxies();

  V8_WARN_UNUSED_RESULT inline bool
  AdvanceFollowingProxiesIgnoringAccessChecks();

  bool IsAtEnd() const { return is_at_end_; }
  Isolate* isolate() const { return isolate_; }

 private:
  Isolate* isolate_;
  Tagged<JSPrototype> object_ = {};
  // TODO(372390038): This handle cannot be migrated to a direct one, because
  // the PrototypeIterator is used as a field in DebugPropertyIterator, which
  // can be heap allocated.
  IndirectHandle<JSPrototype> handle_;
  WhereToEnd where_to_end_;
  bool is_at_end_;
  int seen_proxies_;
};

}  // namespace internal

}  // namespace v8

#endif  // V8_OBJECTS_PROTOTYPE_H_
