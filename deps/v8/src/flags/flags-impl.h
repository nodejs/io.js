// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FLAGS_FLAGS_IMPL_H_
#define V8_FLAGS_FLAGS_IMPL_H_

#include <optional>
#include <unordered_set>

#include "src/base/macros.h"
#include "src/base/vector.h"
#include "src/flags/flags.h"

namespace v8::internal {

class V8_EXPORT_PRIVATE FlagHelpers {
 public:
  static char NormalizeChar(char ch);

  static int FlagNamesCmp(const char* a, const char* b);

  static bool EqualNames(const char* a, const char* b);
  static bool EqualNameWithSuffix(const char* a, const char* b);
};

struct Flag;
Flag* FindFlagByPointer(const void* ptr);
V8_EXPORT_PRIVATE Flag* FindFlagByName(const char* name);
V8_EXPORT_PRIVATE Flag* FindImplicationFlagByName(const char* name);

V8_EXPORT_PRIVATE base::Vector<Flag> Flags();

// Helper struct for printing normalized flag names.
struct FlagName {
  const char* name;
  bool negated;

  constexpr FlagName(const char* name, bool negated)
      : name(name), negated(negated) {
    DCHECK_NE('\0', name[0]);
    DCHECK_NE('!', name[0]);
  }

  constexpr explicit FlagName(const char* name)
      : FlagName(name[0] == '!' ? name + 1 : name, name[0] == '!') {}
};

std::ostream& operator<<(std::ostream& os, FlagName flag_name);

// This structure represents a single entry in the flag system, with a pointer
// to the actual flag, default value, comment, etc.  This is designed to be POD
// initialized as to avoid requiring static constructors.
struct Flag {
  enum FlagType {
    TYPE_BOOL,
    TYPE_MAYBE_BOOL,
    TYPE_INT,
    TYPE_UINT,
    TYPE_UINT64,
    TYPE_FLOAT,
    TYPE_SIZE_T,
    TYPE_STRING,
  };

  enum class SetBy { kDefault, kWeakImplication, kImplication, kCommandLine };

  constexpr bool IsAnyImplication(Flag::SetBy set_by) {
    return set_by == SetBy::kWeakImplication || set_by == SetBy::kImplication;
  }

  FlagType type_;       // What type of flag, bool, int, or string.
  const char* name_;    // Name of the flag, ex "my_flag".
  void* valptr_;        // Pointer to the global flag variable.
  const void* defptr_;  // Pointer to the default value.
  const char* cmt_;     // A comment about the flags purpose.
  bool owns_ptr_;       // Does the flag own its string value?
  SetBy set_by_ = SetBy::kDefault;
  // Name of the flag implying this flag, if any.
  const char* implied_by_ = nullptr;
#ifdef DEBUG
  // Pointer to the flag implying this flag, if any.
  const Flag* implied_by_ptr_ = nullptr;
#endif

  FlagType type() const { return type_; }

  const char* name() const { return name_; }

  const char* comment() const { return cmt_; }

  bool PointsTo(const void* ptr) const { return valptr_ == ptr; }

#ifdef DEBUG
  bool ImpliedBy(const void* ptr) const {
    const Flag* current = this->implied_by_ptr_;
    std::unordered_set<const Flag*> visited_flags;
    while (current != nullptr) {
      visited_flags.insert(current);
      if (current->PointsTo(ptr)) return true;
      current = current->implied_by_ptr_;
      if (visited_flags.contains(current)) break;
    }
    return false;
  }
#endif

  bool bool_variable() const { return GetValue<TYPE_BOOL, bool>(); }

  void set_bool_variable(bool value, SetBy set_by) {
    SetValue<TYPE_BOOL, bool>(value, set_by);
  }

  std::optional<bool> maybe_bool_variable() const {
    return GetValue<TYPE_MAYBE_BOOL, std::optional<bool>>();
  }

  void set_maybe_bool_variable(std::optional<bool> value, SetBy set_by) {
    SetValue<TYPE_MAYBE_BOOL, std::optional<bool>>(value, set_by);
  }

  int int_variable() const { return GetValue<TYPE_INT, int>(); }

  void set_int_variable(int value, SetBy set_by) {
    SetValue<TYPE_INT, int>(value, set_by);
  }

  unsigned int uint_variable() const {
    return GetValue<TYPE_UINT, unsigned int>();
  }

  void set_uint_variable(unsigned int value, SetBy set_by) {
    SetValue<TYPE_UINT, unsigned int>(value, set_by);
  }

  uint64_t uint64_variable() const { return GetValue<TYPE_UINT64, uint64_t>(); }

  void set_uint64_variable(uint64_t value, SetBy set_by) {
    SetValue<TYPE_UINT64, uint64_t>(value, set_by);
  }

  double float_variable() const { return GetValue<TYPE_FLOAT, double>(); }

  void set_float_variable(double value, SetBy set_by) {
    SetValue<TYPE_FLOAT, double>(value, set_by);
  }

  size_t size_t_variable() const { return GetValue<TYPE_SIZE_T, size_t>(); }

  void set_size_t_variable(size_t value, SetBy set_by) {
    SetValue<TYPE_SIZE_T, size_t>(value, set_by);
  }

  const char* string_value() const {
    return GetValue<TYPE_STRING, const char*>();
  }

  void set_string_value(const char* new_value, bool owns_new_value,
                        SetBy set_by);

  template <typename T>
  T GetDefaultValue() const {
    return *reinterpret_cast<const T*>(defptr_);
  }

  bool bool_default() const {
    DCHECK_EQ(TYPE_BOOL, type_);
    return GetDefaultValue<bool>();
  }

  int int_default() const {
    DCHECK_EQ(TYPE_INT, type_);
    return GetDefaultValue<int>();
  }

  unsigned int uint_default() const {
    DCHECK_EQ(TYPE_UINT, type_);
    return GetDefaultValue<unsigned int>();
  }

  uint64_t uint64_default() const {
    DCHECK_EQ(TYPE_UINT64, type_);
    return GetDefaultValue<uint64_t>();
  }

  double float_default() const {
    DCHECK_EQ(TYPE_FLOAT, type_);
    return GetDefaultValue<double>();
  }

  size_t size_t_default() const {
    DCHECK_EQ(TYPE_SIZE_T, type_);
    return GetDefaultValue<size_t>();
  }

  const char* string_default() const {
    DCHECK_EQ(TYPE_STRING, type_);
    return GetDefaultValue<const char*>();
  }

  static bool ShouldCheckFlagContradictions();

  // {change_flag} indicates if we're going to change the flag value.
  // Returns an updated value for {change_flag}, which is changed to false if a
  // weak implication is being ignored beause a flag is already set by a normal
  // implication or from the command-line.
  bool CheckFlagChange(SetBy new_set_by, bool change_flag,
                       const char* implied_by = nullptr);

  bool IsReadOnly() const {
    // See the FLAG_READONLY definition for FLAG_MODE_META.
    return valptr_ == nullptr;
  }

  template <FlagType flag_type, typename T>
  T GetValue() const {
    DCHECK_EQ(flag_type, type_);
    if (IsReadOnly()) return GetDefaultValue<T>();
    return *reinterpret_cast<const FlagValue<T>*>(valptr_);
  }

  template <FlagType flag_type, typename T>
  void SetValue(T new_value, SetBy set_by) {
    DCHECK_EQ(flag_type, type_);
    bool change_flag = GetValue<flag_type, T>() != new_value;
    change_flag = CheckFlagChange(set_by, change_flag);
    if (change_flag) {
      DCHECK(!IsReadOnly());
      *reinterpret_cast<FlagValue<T>*>(valptr_) = new_value;
    }
  }

  // Compare this flag's current value against the default.
  bool IsDefault() const;

  void ReleaseDynamicAllocations();

  // Set a flag back to its default value.
  V8_EXPORT_PRIVATE void Reset();

  void AllowOverwriting() { set_by_ = SetBy::kDefault; }
};

}  // namespace v8::internal

#endif  // V8_FLAGS_FLAGS_IMPL_H_
