// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FUNCTION_KIND_H_
#define V8_OBJECTS_FUNCTION_KIND_H_

#include "src/base/bounds.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

enum class FunctionKind : uint8_t {
  // BEGIN constructable functions
  kNormalFunction,
  kModule,
  kModuleWithTopLevelAwait,
  // BEGIN class constructors
  // BEGIN base constructors
  kBaseConstructor,
  // BEGIN default constructors
  kDefaultBaseConstructor,
  // END base constructors
  // BEGIN derived constructors
  kDefaultDerivedConstructor,
  // END default constructors
  kDerivedConstructor,
  // END derived constructors
  // END class constructors
  // END constructable functions.
  // BEGIN accessors
  kGetterFunction,
  kStaticGetterFunction,
  kSetterFunction,
  kStaticSetterFunction,
  // END accessors
  // BEGIN arrow functions
  kArrowFunction,
  // BEGIN async functions
  kAsyncArrowFunction,
  // END arrow functions
  kAsyncFunction,
  // BEGIN concise methods 1
  kAsyncConciseMethod,
  kStaticAsyncConciseMethod,
  // BEGIN generators
  kAsyncConciseGeneratorMethod,
  kStaticAsyncConciseGeneratorMethod,
  // END concise methods 1
  kAsyncGeneratorFunction,
  // END async functions
  kGeneratorFunction,
  // BEGIN concise methods 2
  kConciseGeneratorMethod,
  kStaticConciseGeneratorMethod,
  // END generators
  kConciseMethod,
  kStaticConciseMethod,
  kClassMembersInitializerFunction,
  kClassStaticInitializerFunction,
  // END concise methods 2
  kInvalid,

  kLastFunctionKind = kClassStaticInitializerFunction,
};

constexpr int kFunctionKindBitSize = 5;
static_assert(static_cast<int>(FunctionKind::kLastFunctionKind) <
              (1 << kFunctionKindBitSize));

inline bool IsArrowFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kArrowFunction,
                         FunctionKind::kAsyncArrowFunction);
}

inline bool IsModule(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kModule,
                         FunctionKind::kModuleWithTopLevelAwait);
}

inline bool IsModuleWithTopLevelAwait(FunctionKind kind) {
  return kind == FunctionKind::kModuleWithTopLevelAwait;
}

inline bool IsAsyncGeneratorFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kAsyncConciseGeneratorMethod,
                         FunctionKind::kAsyncGeneratorFunction);
}

inline bool IsGeneratorFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kAsyncConciseGeneratorMethod,
                         FunctionKind::kStaticConciseGeneratorMethod);
}

inline bool IsAsyncFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kAsyncArrowFunction,
                         FunctionKind::kAsyncGeneratorFunction);
}

inline bool IsResumableFunction(FunctionKind kind) {
  return IsGeneratorFunction(kind) || IsAsyncFunction(kind) || IsModule(kind);
}

inline bool IsConciseMethod(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kAsyncConciseMethod,
                         FunctionKind::kStaticAsyncConciseGeneratorMethod) ||
         base::IsInRange(kind, FunctionKind::kConciseGeneratorMethod,
                         FunctionKind::kClassStaticInitializerFunction);
}

inline bool IsStrictFunctionWithoutPrototype(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kGetterFunction,
                         FunctionKind::kAsyncArrowFunction) ||
         base::IsInRange(kind, FunctionKind::kAsyncConciseMethod,
                         FunctionKind::kStaticAsyncConciseGeneratorMethod) ||
         base::IsInRange(kind, FunctionKind::kConciseGeneratorMethod,
                         FunctionKind::kClassStaticInitializerFunction);
}

inline bool IsGetterFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kGetterFunction,
                         FunctionKind::kStaticGetterFunction);
}

inline bool IsSetterFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kSetterFunction,
                         FunctionKind::kStaticSetterFunction);
}

inline bool IsAccessorFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kGetterFunction,
                         FunctionKind::kStaticSetterFunction);
}

inline bool IsDefaultConstructor(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kDefaultBaseConstructor,
                         FunctionKind::kDefaultDerivedConstructor);
}

inline bool IsBaseConstructor(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kBaseConstructor,
                         FunctionKind::kDefaultBaseConstructor);
}

inline bool IsDerivedConstructor(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kDefaultDerivedConstructor,
                         FunctionKind::kDerivedConstructor);
}

inline bool IsClassConstructor(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kBaseConstructor,
                         FunctionKind::kDerivedConstructor);
}

inline bool IsClassMembersInitializerFunction(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kClassMembersInitializerFunction,
                         FunctionKind::kClassStaticInitializerFunction);
}

inline bool IsConstructable(FunctionKind kind) {
  return base::IsInRange(kind, FunctionKind::kNormalFunction,
                         FunctionKind::kDerivedConstructor);
}

inline bool IsStatic(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kStaticGetterFunction:
    case FunctionKind::kStaticSetterFunction:
    case FunctionKind::kStaticConciseMethod:
    case FunctionKind::kStaticConciseGeneratorMethod:
    case FunctionKind::kStaticAsyncConciseMethod:
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
    case FunctionKind::kClassStaticInitializerFunction:
      return true;
    default:
      return false;
  }
}

inline bool BindsSuper(FunctionKind kind) {
  return IsConciseMethod(kind) || IsAccessorFunction(kind) ||
         IsClassConstructor(kind);
}

inline const char* FunctionKind2String(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kNormalFunction:
      return "NormalFunction";
    case FunctionKind::kArrowFunction:
      return "ArrowFunction";
    case FunctionKind::kGeneratorFunction:
      return "GeneratorFunction";
    case FunctionKind::kConciseMethod:
      return "ConciseMethod";
    case FunctionKind::kStaticConciseMethod:
      return "StaticConciseMethod";
    case FunctionKind::kDerivedConstructor:
      return "DerivedConstructor";
    case FunctionKind::kBaseConstructor:
      return "BaseConstructor";
    case FunctionKind::kGetterFunction:
      return "GetterFunction";
    case FunctionKind::kStaticGetterFunction:
      return "StaticGetterFunction";
    case FunctionKind::kSetterFunction:
      return "SetterFunction";
    case FunctionKind::kStaticSetterFunction:
      return "StaticSetterFunction";
    case FunctionKind::kAsyncFunction:
      return "AsyncFunction";
    case FunctionKind::kModule:
      return "Module";
    case FunctionKind::kModuleWithTopLevelAwait:
      return "AsyncModule";
    case FunctionKind::kClassMembersInitializerFunction:
      return "ClassMembersInitializerFunction";
    case FunctionKind::kClassStaticInitializerFunction:
      return "ClassStaticInitializerFunction";
    case FunctionKind::kDefaultBaseConstructor:
      return "DefaultBaseConstructor";
    case FunctionKind::kDefaultDerivedConstructor:
      return "DefaultDerivedConstructor";
    case FunctionKind::kAsyncArrowFunction:
      return "AsyncArrowFunction";
    case FunctionKind::kAsyncConciseMethod:
      return "AsyncConciseMethod";
    case FunctionKind::kStaticAsyncConciseMethod:
      return "StaticAsyncConciseMethod";
    case FunctionKind::kConciseGeneratorMethod:
      return "ConciseGeneratorMethod";
    case FunctionKind::kStaticConciseGeneratorMethod:
      return "StaticConciseGeneratorMethod";
    case FunctionKind::kAsyncConciseGeneratorMethod:
      return "AsyncConciseGeneratorMethod";
    case FunctionKind::kStaticAsyncConciseGeneratorMethod:
      return "StaticAsyncConciseGeneratorMethod";
    case FunctionKind::kAsyncGeneratorFunction:
      return "AsyncGeneratorFunction";
    case FunctionKind::kInvalid:
      return "Invalid";
  }
  UNREACHABLE();
}

inline std::ostream& operator<<(std::ostream& os, FunctionKind kind) {
  return os << FunctionKind2String(kind);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_FUNCTION_KIND_H_
