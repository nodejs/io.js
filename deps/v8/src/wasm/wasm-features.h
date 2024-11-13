// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_FEATURES_H_
#define V8_WASM_WASM_FEATURES_H_

#include <iosfwd>
#include <string>

#include "src/base/small-vector.h"
#include "src/common/globals.h"
// The feature flags are declared in their own header.
#include "src/wasm/wasm-feature-flags.h"

// Features that are always enabled and do not have a flag.
#define FOREACH_WASM_NON_FLAG_FEATURE(V) \
  V(reftypes)                            \
  V(simd)                                \
  V(threads)                             \
  V(return_call)                         \
  V(extended_const)                      \
  V(relaxed_simd)                        \
  V(gc)                                  \
  V(typed_funcref)                       \
  V(js_inlining)                         \
  V(multi_memory)

// All features, including features that do not have flags.
#define FOREACH_WASM_FEATURE(V) \
  FOREACH_WASM_FEATURE_FLAG(V)  \
  FOREACH_WASM_NON_FLAG_FEATURE(V)

namespace v8::internal::wasm {

enum class WasmEnabledFeature {
#define DECL_FEATURE_ENUM(feat, ...) feat,
  FOREACH_WASM_FEATURE_FLAG(DECL_FEATURE_ENUM)
#undef DECL_FEATURE_ENUM
};

enum class WasmDetectedFeature {
#define DECL_FEATURE_ENUM(feat, ...) feat,
  FOREACH_WASM_FEATURE(DECL_FEATURE_ENUM)
#undef DECL_FEATURE_ENUM
};

// Set of enabled features. This only includes features that have a flag.
class WasmEnabledFeatures : public base::EnumSet<WasmEnabledFeature> {
 public:
  constexpr WasmEnabledFeatures() = default;
  explicit constexpr WasmEnabledFeatures(
      std::initializer_list<WasmEnabledFeature> features)
      : EnumSet(features) {}

  // Simplified getters. Use {has_foo()} instead of
  // {contains(WasmEnabledFeature::foo)}.
#define DECL_FEATURE_GETTER(feat, ...)         \
  constexpr bool has_##feat() const {          \
    return contains(WasmEnabledFeature::feat); \
  }
  FOREACH_WASM_FEATURE_FLAG(DECL_FEATURE_GETTER)
#undef DECL_FEATURE_GETTER

  static inline constexpr WasmEnabledFeatures All() {
#define LIST_FEATURE(feat, ...) WasmEnabledFeature::feat,
    return WasmEnabledFeatures({FOREACH_WASM_FEATURE_FLAG(LIST_FEATURE)});
#undef LIST_FEATURE
  }
  static inline constexpr WasmEnabledFeatures None() { return {}; }
  static inline constexpr WasmEnabledFeatures ForAsmjs() { return {}; }
  // Retuns optional features that are enabled by flags, plus features that are
  // not enabled by a flag and are always on.
  static WasmEnabledFeatures FromFlags();
  static V8_EXPORT_PRIVATE WasmEnabledFeatures FromIsolate(Isolate*);
  static V8_EXPORT_PRIVATE WasmEnabledFeatures
  FromContext(Isolate*, Handle<NativeContext>);
};

// Set of detected features. This includes features that have a flag plus
// features in FOREACH_WASM_NON_FLAG_FEATURE.
class WasmDetectedFeatures : public base::EnumSet<WasmDetectedFeature> {
 public:
  constexpr WasmDetectedFeatures() = default;

  // Simplified getters and setters. Use {add_foo()} and {has_foo()} instead of
  // {Add(WasmDetectedFeature::foo)} or {contains(WasmDetectedFeature::foo)}.
#define DECL_FEATURE_GETTER(feat, ...)                            \
  constexpr void add_##feat() { Add(WasmDetectedFeature::feat); } \
  constexpr bool has_##feat() const {                             \
    return contains(WasmDetectedFeature::feat);                   \
  }
  FOREACH_WASM_FEATURE(DECL_FEATURE_GETTER)
#undef DECL_FEATURE_GETTER
};

inline constexpr const char* name(WasmEnabledFeature feature) {
  switch (feature) {
#define NAME(feat, ...)          \
  case WasmEnabledFeature::feat: \
    return #feat;
    FOREACH_WASM_FEATURE_FLAG(NAME)
  }
#undef NAME
}

inline std::ostream& operator<<(std::ostream& os, WasmEnabledFeature feature) {
  return os << name(feature);
}

inline constexpr const char* name(WasmDetectedFeature feature) {
  switch (feature) {
#define NAME(feat, ...)           \
  case WasmDetectedFeature::feat: \
    return #feat;
    FOREACH_WASM_FEATURE(NAME)
  }
#undef NAME
}

inline std::ostream& operator<<(std::ostream& os, WasmDetectedFeature feature) {
  return os << name(feature);
}

enum class CompileTimeImport {
  kJsString,
  kStringConstants,
  kTextEncoder,
  kTextDecoder,
};

inline std::ostream& operator<<(std::ostream& os, CompileTimeImport imp) {
  return os << static_cast<int>(imp);
}

using CompileTimeImportFlags = base::EnumSet<CompileTimeImport, int>;

class CompileTimeImports {
 public:
  CompileTimeImports() = default;

  CompileTimeImports(const CompileTimeImports& other) V8_NOEXCEPT = default;
  CompileTimeImports& operator=(const CompileTimeImports& other)
      V8_NOEXCEPT = default;
  CompileTimeImports(CompileTimeImports&& other) V8_NOEXCEPT {
    *this = std::move(other);
  }
  CompileTimeImports& operator=(CompileTimeImports&& other) V8_NOEXCEPT {
    bits_ = other.bits_;
    constants_module_ = std::move(other.constants_module_);
    return *this;
  }
  static CompileTimeImports FromSerialized(
      CompileTimeImportFlags::StorageType flags,
      base::Vector<const char> constants_module) {
    CompileTimeImports result;
    result.bits_ = CompileTimeImportFlags::FromIntegral(flags);
    result.constants_module_.assign(constants_module.begin(),
                                    constants_module.end());
    return result;
  }

  bool empty() const { return bits_.empty(); }
  bool has_string_constants(base::Vector<const uint8_t> name) const {
    return bits_.contains(CompileTimeImport::kStringConstants) &&
           constants_module_.size() == name.size() &&
           std::equal(name.begin(), name.end(), constants_module_.begin());
  }
  bool contains(CompileTimeImport imp) const { return bits_.contains(imp); }

  int compare(const CompileTimeImports& other) const {
    if (bits_.ToIntegral() < other.bits_.ToIntegral()) return -1;
    if (bits_.ToIntegral() > other.bits_.ToIntegral()) return 1;
    return constants_module_.compare(other.constants_module_);
  }

  void Add(CompileTimeImport imp) { bits_.Add(imp); }

  std::string& constants_module() { return constants_module_; }
  const std::string& constants_module() const { return constants_module_; }

  CompileTimeImportFlags flags() const { return bits_; }

 private:
  CompileTimeImportFlags bits_;
  std::string constants_module_;
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WASM_FEATURES_H_
