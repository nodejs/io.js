// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_API_API_NATIVES_H_
#define V8_API_API_NATIVES_H_

#include "include/v8-template.h"
#include "src/handles/handles.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/objects.h"
#include "src/objects/property-details.h"

namespace v8 {
namespace internal {

// Forward declarations.
enum InstanceType : uint16_t;
class ObjectTemplateInfo;
class TemplateInfo;

class ApiNatives {
 public:
  static const int kInitialFunctionCacheSize = 256;

  // A convenient internal wrapper around FunctionTemplate::New() for creating
  // getter/setter callback function templates.
  static DirectHandle<FunctionTemplateInfo> CreateAccessorFunctionTemplateInfo(
      Isolate* isolate, FunctionCallback callback, int length,
      v8::SideEffectType side_effect_type);

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction> InstantiateFunction(
      Isolate* isolate, DirectHandle<NativeContext> native_context,
      DirectHandle<FunctionTemplateInfo> data,
      MaybeDirectHandle<Name> maybe_name = {});

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction> InstantiateFunction(
      Isolate* isolate, DirectHandle<FunctionTemplateInfo> data,
      MaybeDirectHandle<Name> maybe_name = {});

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSObject> InstantiateObject(
      Isolate* isolate, DirectHandle<ObjectTemplateInfo> data,
      DirectHandle<JSReceiver> new_target = {});

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSObject> InstantiateRemoteObject(
      DirectHandle<ObjectTemplateInfo> data);

  static Handle<JSFunction> CreateApiFunction(
      Isolate* isolate, DirectHandle<NativeContext> native_context,
      DirectHandle<FunctionTemplateInfo> obj, DirectHandle<Object> prototype,
      InstanceType type, MaybeDirectHandle<Name> name = {});

  static void AddDataProperty(Isolate* isolate, DirectHandle<TemplateInfo> info,
                              DirectHandle<Name> name,
                              DirectHandle<Object> value,
                              PropertyAttributes attributes);

  static void AddDataProperty(Isolate* isolate, DirectHandle<TemplateInfo> info,
                              DirectHandle<Name> name, v8::Intrinsic intrinsic,
                              PropertyAttributes attributes);

  static void AddAccessorProperty(Isolate* isolate,
                                  DirectHandle<TemplateInfo> info,
                                  DirectHandle<Name> name,
                                  DirectHandle<FunctionTemplateInfo> getter,
                                  DirectHandle<FunctionTemplateInfo> setter,
                                  PropertyAttributes attributes);

  static void AddNativeDataProperty(Isolate* isolate,
                                    DirectHandle<TemplateInfo> info,
                                    DirectHandle<AccessorInfo> property);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_NATIVES_H_
