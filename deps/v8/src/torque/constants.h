// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_CONSTANTS_H_
#define V8_TORQUE_CONSTANTS_H_

#include <cstring>
#include <string>

#include "src/base/flags.h"

namespace v8 {
namespace internal {
namespace torque {

static const char* const CONSTEXPR_TYPE_PREFIX = "constexpr ";
static const char* const NEVER_TYPE_STRING = "never";
static const char* const CONSTEXPR_BOOL_TYPE_STRING = "constexpr bool";
static const char* const CONSTEXPR_STRING_TYPE_STRING = "constexpr string";
static const char* const CONSTEXPR_INTPTR_TYPE_STRING = "constexpr intptr";
static const char* const CONSTEXPR_INSTANCE_TYPE_TYPE_STRING =
    "constexpr InstanceType";
static const char* const BOOL_TYPE_STRING = "bool";
static const char* const VOID_TYPE_STRING = "void";
static const char* const ARGUMENTS_TYPE_STRING = "Arguments";
static const char* const CONTEXT_TYPE_STRING = "Context";
static const char* const NO_CONTEXT_TYPE_STRING = "NoContext";
static const char* const NATIVE_CONTEXT_TYPE_STRING = "NativeContext";
static const char* const JS_FUNCTION_TYPE_STRING = "JSFunction";
static const char* const MAP_TYPE_STRING = "Map";
static const char* const OBJECT_TYPE_STRING = "Object";
static const char* const HEAP_OBJECT_TYPE_STRING = "HeapObject";
static const char* const TAGGED_ZERO_PATTERN_TYPE_STRING = "TaggedZeroPattern";
static const char* const JSANY_TYPE_STRING = "JSAny";
static const char* const JSOBJECT_TYPE_STRING = "JSObject";
static const char* const SMI_TYPE_STRING = "Smi";
static const char* const TAGGED_TYPE_STRING = "Tagged";
static const char* const STRONG_TAGGED_TYPE_STRING = "StrongTagged";
static const char* const UNINITIALIZED_TYPE_STRING = "Uninitialized";
static const char* const UNINITIALIZED_HEAP_OBJECT_TYPE_STRING =
    "UninitializedHeapObject";
static const char* const RAWPTR_TYPE_STRING = "RawPtr";
static const char* const EXTERNALPTR_TYPE_STRING = "ExternalPointer";
static const char* const CPPHEAPPTR_TYPE_STRING = "CppHeapPointer";
static const char* const TRUSTEDPTR_TYPE_STRING = "TrustedPointer";
static const char* const PROTECTEDPTR_TYPE_STRING = "ProtectedPointer";
static const char* const DISPATCH_HANDLE_TYPE_STRING = "DispatchHandle";
static const char* const CONST_STRING_TYPE_STRING = "constexpr string";
static const char* const STRING_TYPE_STRING = "String";
static const char* const NUMBER_TYPE_STRING = "Number";
static const char* const BUILTIN_POINTER_TYPE_STRING = "BuiltinPtr";
static const char* const INTPTR_TYPE_STRING = "intptr";
static const char* const UINTPTR_TYPE_STRING = "uintptr";
static const char* const INT64_TYPE_STRING = "int64";
static const char* const UINT64_TYPE_STRING = "uint64";
static const char* const INT31_TYPE_STRING = "int31";
static const char* const INT32_TYPE_STRING = "int32";
static const char* const UINT31_TYPE_STRING = "uint31";
static const char* const UINT32_TYPE_STRING = "uint32";
static const char* const INT16_TYPE_STRING = "int16";
static const char* const UINT16_TYPE_STRING = "uint16";
static const char* const INT8_TYPE_STRING = "int8";
static const char* const UINT8_TYPE_STRING = "uint8";
static const char* const BINT_TYPE_STRING = "bint";
static const char* const CHAR8_TYPE_STRING = "char8";
static const char* const CHAR16_TYPE_STRING = "char16";
static const char* const FLOAT16_RAW_BITS_TYPE_STRING = "float16_raw_bits";
static const char* const FLOAT32_TYPE_STRING = "float32";
static const char* const FLOAT64_TYPE_STRING = "float64";
static const char* const FLOAT64_OR_UNDEFINED_OR_HOLE_TYPE_STRING =
    "float64_or_undefined_or_hole";
static const char* const CONST_INT31_TYPE_STRING = "constexpr int31";
static const char* const CONST_INT32_TYPE_STRING = "constexpr int32";
static const char* const CONST_FLOAT64_TYPE_STRING = "constexpr float64";
static const char* const INTEGER_LITERAL_TYPE_STRING =
    "constexpr IntegerLiteral";
static const char* const TORQUE_INTERNAL_NAMESPACE_STRING = "torque_internal";
static const char* const MUTABLE_REFERENCE_TYPE_STRING = "MutableReference";
static const char* const CONST_REFERENCE_TYPE_STRING = "ConstReference";
static const char* const MUTABLE_SLICE_TYPE_STRING = "MutableSlice";
static const char* const CONST_SLICE_TYPE_STRING = "ConstSlice";
static const char* const WEAK_TYPE_STRING = "Weak";
static const char* const SMI_TAGGED_TYPE_STRING = "SmiTagged";
static const char* const LAZY_TYPE_STRING = "Lazy";
static const char* const UNINITIALIZED_ITERATOR_TYPE_STRING =
    "UninitializedIterator";
static const char* const GENERIC_TYPE_INSTANTIATION_NAMESPACE_STRING =
    "_generic_type_instantiation_namespace";
static const char* const FIXED_ARRAY_BASE_TYPE_STRING = "FixedArrayBase";
static const char* const WEAK_HEAP_OBJECT = "WeakHeapObject";
static const char* const STATIC_ASSERT_MACRO_STRING = "StaticAssert";

static const char* const ANNOTATION_ABSTRACT = "@abstract";
static const char* const ANNOTATION_HAS_SAME_INSTANCE_TYPE_AS_PARENT =
    "@hasSameInstanceTypeAsParent";
static const char* const ANNOTATION_DO_NOT_GENERATE_CPP_CLASS =
    "@doNotGenerateCppClass";
static const char* const ANNOTATION_CUSTOM_MAP = "@customMap";
static const char* const ANNOTATION_CUSTOM_CPP_CLASS = "@customCppClass";
static const char* const ANNOTATION_HIGHEST_INSTANCE_TYPE_WITHIN_PARENT =
    "@highestInstanceTypeWithinParentClassRange";
static const char* const ANNOTATION_LOWEST_INSTANCE_TYPE_WITHIN_PARENT =
    "@lowestInstanceTypeWithinParentClassRange";
static const char* const ANNOTATION_RESERVE_BITS_IN_INSTANCE_TYPE =
    "@reserveBitsInInstanceType";
static const char* const ANNOTATION_INSTANCE_TYPE_VALUE =
    "@apiExposedInstanceTypeValue";
static const char* const ANNOTATION_IF = "@if";
static const char* const ANNOTATION_IFNOT = "@ifnot";
static const char* const ANNOTATION_GENERATE_BODY_DESCRIPTOR =
    "@generateBodyDescriptor";
static const char* const ANNOTATION_GENERATE_UNIQUE_MAP = "@generateUniqueMap";
static const char* const ANNOTATION_GENERATE_FACTORY_FUNCTION =
    "@generateFactoryFunction";
static const char* const ANNOTATION_EXPORT = "@export";
static const char* const ANNOTATION_DO_NOT_GENERATE_CAST = "@doNotGenerateCast";
static const char* const ANNOTATION_USE_PARENT_TYPE_CHECKER =
    "@useParentTypeChecker";
static const char* const ANNOTATION_CPP_OBJECT_DEFINITION =
    "@cppObjectDefinition";
static const char* const ANNOTATION_CPP_OBJECT_LAYOUT_DEFINITION =
    "@cppObjectLayoutDefinition";
static const char* const ANNOTATION_SAME_ENUM_VALUE_AS = "@sameEnumValueAs";
// Generate C++ accessors with relaxed store semantics.
// Weak<T> and Tagged<MaybeObject> fields always use relaxed store.
static const char* const ANNOTATION_CPP_RELAXED_STORE = "@cppRelaxedStore";
// Generate C++ accessors with relaxed load semantics.
static const char* const ANNOTATION_CPP_RELAXED_LOAD = "@cppRelaxedLoad";
// Generate C++ accessors with release store semantics.
static const char* const ANNOTATION_CPP_RELEASE_STORE = "@cppReleaseStore";
// Generate C++ accessors with acquire load semantics.
static const char* const ANNOTATION_CPP_ACQUIRE_LOAD = "@cppAcquireLoad";
// Generate BodyDescriptor using IterateCustomWeakPointers.
static const char* const ANNOTATION_CUSTOM_WEAK_MARKING = "@customWeakMarking";
// Do not generate an interface descriptor for this builtin.
static const char* const ANNOTATION_CUSTOM_INTERFACE_DESCRIPTOR =
    "@customInterfaceDescriptor";
// Automatically generates a call to IncrementUseCounter at the start of a
// builtin.
static const char* const ANNOTATION_INCREMENT_USE_COUNTER =
    "@incrementUseCounter";

inline bool IsConstexprName(const std::string& name) {
  return name.substr(0, std::strlen(CONSTEXPR_TYPE_PREFIX)) ==
         CONSTEXPR_TYPE_PREFIX;
}

inline std::string GetNonConstexprName(const std::string& name) {
  if (!IsConstexprName(name)) return name;
  return name.substr(std::strlen(CONSTEXPR_TYPE_PREFIX));
}

inline std::string GetConstexprName(const std::string& name) {
  if (IsConstexprName(name)) return name;
  return CONSTEXPR_TYPE_PREFIX + name;
}

enum class AbstractTypeFlag {
  kNone = 0,
  kTransient = 1 << 0,
  kConstexpr = 1 << 1,
  kUseParentTypeChecker = 1 << 2,
};
using AbstractTypeFlags = base::Flags<AbstractTypeFlag>;

enum class ClassFlag {
  kNone = 0,
  kExtern = 1 << 0,
  kTransient = 1 << 1,
  kAbstract = 1 << 2,
  kIsShape = 1 << 3,
  kHasSameInstanceTypeAsParent = 1 << 4,
  kGenerateCppClassDefinitions = 1 << 5,
  kHighestInstanceTypeWithinParent = 1 << 6,
  kLowestInstanceTypeWithinParent = 1 << 7,
  kUndefinedLayout = 1 << 8,
  kGenerateBodyDescriptor = 1 << 9,
  kExport = 1 << 10,
  kDoNotGenerateCast = 1 << 11,
  kGenerateUniqueMap = 1 << 12,
  kGenerateFactoryFunction = 1 << 13,
  kCppObjectDefinition = 1 << 14,
  kCppObjectLayoutDefinition = 1 << 15,
};
using ClassFlags = base::Flags<ClassFlag>;

enum class StructFlag { kNone = 0, kExport = 1 << 0 };
using StructFlags = base::Flags<StructFlag>;

enum class FieldSynchronization {
  kNone,
  kRelaxed,
  kAcquireRelease,
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_CONSTANTS_H_
