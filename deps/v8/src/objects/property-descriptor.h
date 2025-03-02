// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROPERTY_DESCRIPTOR_H_
#define V8_OBJECTS_PROPERTY_DESCRIPTOR_H_

#include "src/handles/handles.h"
#include "src/objects/property-details.h"

namespace v8 {
namespace internal {

class Isolate;
class Object;
class PropertyDescriptorObject;

class PropertyDescriptor {
 public:
  PropertyDescriptor()
      : enumerable_(false),
        has_enumerable_(false),
        configurable_(false),
        has_configurable_(false),
        writable_(false),
        has_writable_(false) {}

  // ES6 6.2.4.1
  static bool IsAccessorDescriptor(PropertyDescriptor* desc) {
    return desc->has_get() || desc->has_set();
  }

  // ES6 6.2.4.2
  static bool IsDataDescriptor(PropertyDescriptor* desc) {
    return desc->has_value() || desc->has_writable();
  }

  // ES6 6.2.4.3
  static bool IsGenericDescriptor(PropertyDescriptor* desc) {
    return !IsAccessorDescriptor(desc) && !IsDataDescriptor(desc);
  }

  // ES6 6.2.4.4
  DirectHandle<JSObject> ToObject(Isolate* isolate);

  DirectHandle<PropertyDescriptorObject> ToPropertyDescriptorObject(
      Isolate* isolate);

  // ES6 6.2.4.5
  static bool ToPropertyDescriptor(Isolate* isolate, Handle<JSAny> obj,
                                   PropertyDescriptor* desc);

  // ES6 6.2.4.6
  static void CompletePropertyDescriptor(Isolate* isolate,
                                         PropertyDescriptor* desc);

  bool is_empty() const {
    return !has_enumerable() && !has_configurable() && !has_writable() &&
           !has_value() && !has_get() && !has_set();
  }

  bool IsRegularAccessorProperty() const {
    return has_configurable() && has_enumerable() && !has_value() &&
           !has_writable() && has_get() && has_set();
  }

  bool IsRegularDataProperty() const {
    return has_configurable() && has_enumerable() && has_value() &&
           has_writable() && !has_get() && !has_set();
  }

  bool enumerable() const { return enumerable_; }
  void set_enumerable(bool enumerable) {
    enumerable_ = enumerable;
    has_enumerable_ = true;
  }
  bool has_enumerable() const { return has_enumerable_; }

  bool configurable() const { return configurable_; }
  void set_configurable(bool configurable) {
    configurable_ = configurable;
    has_configurable_ = true;
  }
  bool has_configurable() const { return has_configurable_; }

  Handle<JSAny> value() const { return value_; }
  void set_value(DirectHandle<JSAny> value) { value_ = indirect_handle(value); }
  bool has_value() const { return !value_.is_null(); }

  bool writable() const { return writable_; }
  void set_writable(bool writable) {
    writable_ = writable;
    has_writable_ = true;
  }
  bool has_writable() const { return has_writable_; }

  Handle<UnionOf<JSAny, FunctionTemplateInfo>> get() const { return get_; }
  void set_get(DirectHandle<UnionOf<JSAny, FunctionTemplateInfo>> get) {
    get_ = indirect_handle(get);
  }
  bool has_get() const { return !get_.is_null(); }

  Handle<UnionOf<JSAny, FunctionTemplateInfo>> set() const { return set_; }
  void set_set(DirectHandle<UnionOf<JSAny, FunctionTemplateInfo>> set) {
    set_ = indirect_handle(set);
  }
  bool has_set() const { return !set_.is_null(); }

  DirectHandle<JSAny> name() const { return name_; }
  void set_name(DirectHandle<JSAny> name) { name_ = indirect_handle(name); }

  PropertyAttributes ToAttributes() {
    return static_cast<PropertyAttributes>(
        (has_enumerable() && !enumerable() ? DONT_ENUM : NONE) |
        (has_configurable() && !configurable() ? DONT_DELETE : NONE) |
        (has_writable() && !writable() ? READ_ONLY : NONE));
  }

 private:
  bool enumerable_ : 1;
  bool has_enumerable_ : 1;
  bool configurable_ : 1;
  bool has_configurable_ : 1;
  bool writable_ : 1;
  bool has_writable_ : 1;
  // TODO(42203211): When this class is only stack-allocated, the following
  // fields can change to DirectHandle. So far, there is one vector of property
  // descriptors used in JSReceiver::DefineProperties.
  IndirectHandle<JSAny> value_;
  IndirectHandle<UnionOf<JSAny, FunctionTemplateInfo>> get_;
  IndirectHandle<UnionOf<JSAny, FunctionTemplateInfo>> set_;
  IndirectHandle<JSAny> name_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_PROPERTY_DESCRIPTOR_H_
