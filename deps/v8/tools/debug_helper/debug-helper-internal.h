// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines internal versions of the public API structs. These should
// all be tidy and simple classes which maintain proper ownership (unique_ptr)
// of each other. Each contains an instance of its corresponding public type,
// which can be filled out with GetPublicView.

#ifndef V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_INTERNAL_H_
#define V8_TOOLS_DEBUG_HELPER_DEBUG_HELPER_INTERNAL_H_

#include <memory>
#include <string>
#include <vector>

#include "debug-helper.h"
#include "src/common/globals.h"
#include "src/objects/instance-type.h"

namespace d = v8::debug_helper;

namespace v8 {
namespace internal {
namespace debug_helper_internal {

// A value that was read from the debuggee's memory.
template <typename TValue>
struct Value {
  d::MemoryAccessResult validity;
  TValue value;
};

// Internal version of API class v8::debug_helper::PropertyBase.
class PropertyBase {
 public:
  PropertyBase(std::string name, std::string type) : name_(name), type_(type) {}
  void SetFieldsOnPublicView(d::PropertyBase* public_view) {
    public_view->name = name_.c_str();
    public_view->type = type_.c_str();
  }

 private:
  std::string name_;
  std::string type_;
};

// Internal version of API class v8::debug_helper::StructProperty.
class StructProperty : public PropertyBase {
 public:
  StructProperty(std::string name, std::string type, size_t offset,
                 uint8_t num_bits, uint8_t shift_bits)
      : PropertyBase(std::move(name), std::move(type)),
        offset_(offset),
        num_bits_(num_bits),
        shift_bits_(shift_bits) {}

  d::StructProperty* GetPublicView() {
    PropertyBase::SetFieldsOnPublicView(&public_view_);
    public_view_.offset = offset_;
    public_view_.num_bits = num_bits_;
    public_view_.shift_bits = shift_bits_;
    return &public_view_;
  }

 private:
  size_t offset_;
  uint8_t num_bits_;
  uint8_t shift_bits_;

  d::StructProperty public_view_;
};

// Internal version of API class v8::debug_helper::ObjectProperty.
class ObjectProperty : public PropertyBase {
 public:
  ObjectProperty(std::string name, std::string type, uintptr_t address,
                 size_t num_values, size_t size,
                 std::vector<std::unique_ptr<StructProperty>> struct_fields,
                 d::PropertyKind kind)
      : PropertyBase(std::move(name), std::move(type)),
        address_(address),
        num_values_(num_values),
        size_(size),
        struct_fields_(std::move(struct_fields)),
        kind_(kind) {}

  d::ObjectProperty* GetPublicView() {
    PropertyBase::SetFieldsOnPublicView(&public_view_);
    public_view_.address = address_;
    public_view_.num_values = num_values_;
    public_view_.size = size_;
    public_view_.num_struct_fields = struct_fields_.size();
    struct_fields_raw_.clear();
    for (const auto& property : struct_fields_) {
      struct_fields_raw_.push_back(property->GetPublicView());
    }
    public_view_.struct_fields = struct_fields_raw_.data();
    public_view_.kind = kind_;
    return &public_view_;
  }

 private:
  uintptr_t address_;
  size_t num_values_;
  size_t size_;
  std::vector<std::unique_ptr<StructProperty>> struct_fields_;
  d::PropertyKind kind_;

  d::ObjectProperty public_view_;
  std::vector<d::StructProperty*> struct_fields_raw_;
};

class ObjectPropertiesResult;
struct ObjectPropertiesResultExtended : public d::ObjectPropertiesResult {
  // Back reference for cleanup.
  debug_helper_internal::ObjectPropertiesResult* base;
};

// Internal version of API class v8::debug_helper::ObjectPropertiesResult.
class ObjectPropertiesResult {
 public:
  ObjectPropertiesResult(d::TypeCheckResult type_check_result,
                         std::string brief, std::string type)
      : type_check_result_(type_check_result), brief_(brief), type_(type) {}
  ObjectPropertiesResult(
      d::TypeCheckResult type_check_result, std::string brief, std::string type,
      std::vector<std::unique_ptr<ObjectProperty>> properties,
      std::vector<std::string> guessed_types)
      : ObjectPropertiesResult(type_check_result, brief, type) {
    properties_ = std::move(properties);
    guessed_types_ = std::move(guessed_types);
  }

  void Prepend(const char* prefix) { brief_ = prefix + brief_; }

  d::ObjectPropertiesResult* GetPublicView() {
    public_view_.type_check_result = type_check_result_;
    public_view_.brief = brief_.c_str();
    public_view_.type = type_.c_str();
    public_view_.num_properties = properties_.size();
    properties_raw_.clear();
    for (const auto& property : properties_) {
      properties_raw_.push_back(property->GetPublicView());
    }
    public_view_.properties = properties_raw_.data();
    public_view_.num_guessed_types = guessed_types_.size();
    guessed_types_raw_.clear();
    for (const auto& guess : guessed_types_) {
      guessed_types_raw_.push_back(guess.c_str());
    }
    public_view_.guessed_types = guessed_types_raw_.data();
    public_view_.base = this;
    return &public_view_;
  }

 private:
  d::TypeCheckResult type_check_result_;
  std::string brief_;
  std::string type_;
  std::vector<std::unique_ptr<ObjectProperty>> properties_;
  std::vector<std::string> guessed_types_;

  ObjectPropertiesResultExtended public_view_;
  std::vector<d::ObjectProperty*> properties_raw_;
  std::vector<const char*> guessed_types_raw_;
};

class StackFrameResult;
struct StackFrameResultExtended : public d::StackFrameResult {
  // Back reference for cleanup.
  debug_helper_internal::StackFrameResult* base;
};

// Internal version of API class v8::debug_helper::StackFrameResult.
class StackFrameResult {
 public:
  StackFrameResult(std::vector<std::unique_ptr<ObjectProperty>> properties) {
    properties_ = std::move(properties);
  }

  d::StackFrameResult* GetPublicView() {
    public_view_.num_properties = properties_.size();
    properties_raw_.clear();
    for (const auto& property : properties_) {
      properties_raw_.push_back(property->GetPublicView());
    }
    public_view_.properties = properties_raw_.data();
    public_view_.base = this;
    return &public_view_;
  }

 private:
  std::vector<std::unique_ptr<ObjectProperty>> properties_;

  StackFrameResultExtended public_view_;
  std::vector<d::ObjectProperty*> properties_raw_;
};

class TqObjectVisitor;

// Base class representing a V8 object in the debuggee's address space.
// Subclasses for specific object types are generated by the Torque compiler.
class TqObject {
 public:
  TqObject(uintptr_t address) : address_(address) {}
  virtual ~TqObject() = default;
  virtual std::vector<std::unique_ptr<ObjectProperty>> GetProperties(
      d::MemoryAccessor accessor) const;
  virtual const char* GetName() const;
  virtual void Visit(TqObjectVisitor* visitor) const;
  virtual bool IsSuperclassOf(const TqObject* other) const;

 protected:
  uintptr_t address_;
};

// A helpful template so that generated code can be sure that a string type name
// actually resolves to a type, by repeating the name as the template parameter
// and the value.
template <typename T>
const char* CheckTypeName(const char* name) {
  return name;
}

// In ptr-compr builds, returns whether the address looks like a compressed
// pointer (zero-extended from 32 bits). Otherwise returns false because no
// pointers can be compressed.
bool IsPointerCompressed(uintptr_t address);

// If the given address looks like a compressed pointer, returns a decompressed
// representation of it. Otherwise returns the address unmodified.
uintptr_t EnsureDecompressed(uintptr_t address,
                             uintptr_t any_uncompressed_address);

// Converts the MemoryAccessResult from attempting to read an array's length
// into the corresponding PropertyKind for the array.
d::PropertyKind GetArrayKind(d::MemoryAccessResult mem_result);

}  // namespace debug_helper_internal
}  // namespace internal
}  // namespace v8

#endif
