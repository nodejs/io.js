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
  Handle<JSObject> ToObject(Isolate* isolate);

  Handle<PropertyDescriptorObject> ToPropertyDescriptorObject(Isolate* isolate);

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
  void set_value(Handle<JSAny> value) { value_ = value; }
  bool has_value() const { return !value_.is_null(); }

  bool writable() const { return writable_; }
  void set_writable(bool writable) {
    writable_ = writable;
    has_writable_ = true;
  }
  bool has_writable() const { return has_writable_; }

  Handle<UnionOf<JSAny, FunctionTemplateInfo>> get() const { return get_; }
  void set_get(Handle<UnionOf<JSAny, FunctionTemplateInfo>> get) { get_ = get; }
  bool has_get() const { return !get_.is_null(); }

  Handle<UnionOf<JSAny, FunctionTemplateInfo>> set() const { return set_; }
  void set_set(Handle<UnionOf<JSAny, FunctionTemplateInfo>> set) { set_ = set; }
  bool has_set() const { return !set_.is_null(); }

  Handle<JSAny> name() const { return name_; }
  void set_name(Handle<JSAny> name) { name_ = name; }

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
  Handle<JSAny> value_;
  Handle<UnionOf<JSAny, FunctionTemplateInfo>> get_;
  Handle<UnionOf<JSAny, FunctionTemplateInfo>> set_;
  Handle<JSAny> name_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_PROPERTY_DESCRIPTOR_H_
