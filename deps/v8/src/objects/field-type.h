// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FIELD_TYPE_H_
#define V8_OBJECTS_FIELD_TYPE_H_

#include "src/handles/handles.h"
#include "src/objects/casting.h"
#include "src/objects/tagged.h"

namespace v8 {
namespace internal {

class FieldType;

class FieldType : public AllStatic {
 public:
  // If the GC can clear field types we must ensure that every store updates
  // field types.
  static constexpr bool kFieldTypesCanBeClearedOnGC = true;

  V8_EXPORT_PRIVATE static Tagged<FieldType> None();
  V8_EXPORT_PRIVATE static Tagged<FieldType> Any();
  V8_EXPORT_PRIVATE static Handle<FieldType> None(Isolate* isolate);
  V8_EXPORT_PRIVATE static Handle<FieldType> Any(Isolate* isolate);
  V8_EXPORT_PRIVATE static Tagged<FieldType> Class(Tagged<Map> map);
  V8_EXPORT_PRIVATE static Handle<FieldType> Class(DirectHandle<Map> map,
                                                   Isolate* isolate);

  static bool NowContains(Tagged<FieldType> type, Tagged<Object> value);

  static bool NowContains(Tagged<FieldType> type, DirectHandle<Object> value) {
    return NowContains(type, *value);
  }

  static Tagged<Map> AsClass(Tagged<FieldType> type);
  static Handle<Map> AsClass(Handle<FieldType> type);
  static bool NowStable(Tagged<FieldType> type);
  static bool NowIs(Tagged<FieldType> type, Tagged<FieldType> other);
  static bool NowIs(Tagged<FieldType> type, DirectHandle<FieldType> other);

  V8_EXPORT_PRIVATE static bool Equals(Tagged<FieldType> type,
                                       Tagged<FieldType> other);
  V8_EXPORT_PRIVATE static void PrintTo(Tagged<FieldType> type,
                                        std::ostream& os);
};

bool IsClass(Tagged<FieldType> obj);
inline bool IsNone(Tagged<FieldType> obj) { return obj == FieldType::None(); }
inline bool IsAny(Tagged<FieldType> obj) { return obj == FieldType::Any(); }

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_FIELD_TYPE_H_
