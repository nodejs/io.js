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
  static Handle<FunctionTemplateInfo> CreateAccessorFunctionTemplateInfo(
      Isolate* isolate, FunctionCallback callback, int length,
      v8::SideEffectType side_effect_type);

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction> InstantiateFunction(
      Isolate* isolate, Handle<NativeContext> native_context,
      Handle<FunctionTemplateInfo> data,
      MaybeHandle<Name> maybe_name = MaybeHandle<Name>());

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSFunction> InstantiateFunction(
      Isolate* isolate, Handle<FunctionTemplateInfo> data,
      MaybeHandle<Name> maybe_name = MaybeHandle<Name>());

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSObject> InstantiateObject(
      Isolate* isolate, Handle<ObjectTemplateInfo> data,
      Handle<JSReceiver> new_target = Handle<JSReceiver>());

  V8_WARN_UNUSED_RESULT static MaybeHandle<JSObject> InstantiateRemoteObject(
      DirectHandle<ObjectTemplateInfo> data);

  static Handle<JSFunction> CreateApiFunction(
      Isolate* isolate, Handle<NativeContext> native_context,
      DirectHandle<FunctionTemplateInfo> obj, Handle<Object> prototype,
      InstanceType type, MaybeHandle<Name> name = MaybeHandle<Name>());

  static void AddDataProperty(Isolate* isolate, DirectHandle<TemplateInfo> info,
                              Handle<Name> name, Handle<Object> value,
                              PropertyAttributes attributes);

  static void AddDataProperty(Isolate* isolate, DirectHandle<TemplateInfo> info,
                              Handle<Name> name, v8::Intrinsic intrinsic,
                              PropertyAttributes attributes);

  static void AddAccessorProperty(Isolate* isolate,
                                  DirectHandle<TemplateInfo> info,
                                  Handle<Name> name,
                                  Handle<FunctionTemplateInfo> getter,
                                  Handle<FunctionTemplateInfo> setter,
                                  PropertyAttributes attributes);

  static void AddNativeDataProperty(Isolate* isolate,
                                    DirectHandle<TemplateInfo> info,
                                    DirectHandle<AccessorInfo> property);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_API_API_NATIVES_H_
