// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WELL_KNOWN_IMPORTS_H_
#define V8_WASM_WELL_KNOWN_IMPORTS_H_

#include <memory>

#include "src/base/atomicops.h"
#include "src/base/platform/mutex.h"
#include "src/base/vector.h"
#include "src/common/globals.h"

namespace v8::internal::wasm {

enum class WellKnownImport : uint8_t {
  // Generic:
  kUninstantiated,
  kGeneric,
  kLinkError,

  ////////////////////////////////////////////////////////
  // Compile-time "builtin" imports:
  ////////////////////////////////////////////////////////
  kFirstCompileTimeImport,

  // JS String Builtins
  // https://github.com/WebAssembly/js-string-builtins
  // TODO(14179): Rename some of these to reflect the new import names.
  kStringCast = kFirstCompileTimeImport,
  kStringCharCodeAt,
  kStringCodePointAt,
  kStringCompare,
  kStringConcat,
  kStringEquals,
  kStringFromCharCode,
  kStringFromCodePoint,
  kStringFromUtf8Array,
  kStringFromWtf16Array,
  kStringIntoUtf8Array,
  kStringLength,
  kStringMeasureUtf8,
  kStringSubstring,
  kStringTest,
  kStringToUtf8Array,
  kStringToWtf16Array,

  kLastCompileTimeImport = kStringToWtf16Array,
  ////////////////////////////////////////////////////////
  // End of compile-time "builtin" imports.
  ////////////////////////////////////////////////////////

  // DataView methods:
  kDataViewGetBigInt64,
  kDataViewGetBigUint64,
  kDataViewGetFloat32,
  kDataViewGetFloat64,
  kDataViewGetInt8,
  kDataViewGetInt16,
  kDataViewGetInt32,
  kDataViewGetUint8,
  kDataViewGetUint16,
  kDataViewGetUint32,
  kDataViewSetBigInt64,
  kDataViewSetBigUint64,
  kDataViewSetFloat32,
  kDataViewSetFloat64,
  kDataViewSetInt8,
  kDataViewSetInt16,
  kDataViewSetInt32,
  kDataViewSetUint8,
  kDataViewSetUint16,
  kDataViewSetUint32,
  kDataViewByteLength,

  // String-related functions:
  kDoubleToString,
  kIntToString,
  kParseFloat,

  kStringIndexOf,
  kStringIndexOfImported,
  kStringToLocaleLowerCaseStringref,
  kStringToLowerCaseStringref,
  kStringToLowerCaseImported,
  // Fast API calls:
  kFastAPICall,
};

class NativeModule;

// For debugging/tracing.
const char* WellKnownImportName(WellKnownImport wki);

inline bool IsCompileTimeImport(WellKnownImport wki) {
  using T = std::underlying_type_t<WellKnownImport>;
  T num = static_cast<T>(wki);
  constexpr T kFirst = static_cast<T>(WellKnownImport::kFirstCompileTimeImport);
  constexpr T kLast = static_cast<T>(WellKnownImport::kLastCompileTimeImport);
  return kFirst <= num && num <= kLast;
}

class WellKnownImportsList {
 public:
  enum class UpdateResult : bool { kFoundIncompatibility, kOK };

  WellKnownImportsList() = default;

  // Regular initialization. Allocates size-dependent internal data.
  void Initialize(int size) {
#if DEBUG
    DCHECK_EQ(-1, size_);
    size_ = size;
#endif
    static_assert(static_cast<int>(WellKnownImport::kUninstantiated) == 0);
    statuses_ = std::make_unique<std::atomic<WellKnownImport>[]>(size);
#if !defined(__cpp_lib_atomic_value_initialization) || \
    __cpp_lib_atomic_value_initialization < 201911L
    for (int i = 0; i < size; i++) {
      std::atomic_init(&statuses_.get()[i], WellKnownImport::kUninstantiated);
    }
#endif
  }

  // Intended for deserialization. Does not check consistency with code.
  void Initialize(base::Vector<const WellKnownImport> entries);

  WellKnownImport get(int index) const {
    DCHECK_LT(index, size_);
    return statuses_[index].load(std::memory_order_relaxed);
  }

  V8_WARN_UNUSED_RESULT UpdateResult
  Update(base::Vector<WellKnownImport> entries);

  // If you need this mutex and the NativeModule's allocation_mutex_, always
  // get the latter first.
  base::Mutex* mutex() { return &mutex_; }

 private:
  // This mutex guards {statuses_}, for operations that need to ensure that
  // they see a consistent view of {statuses_} for some period of time.
  base::Mutex mutex_;
  std::unique_ptr<std::atomic<WellKnownImport>[]> statuses_;

#if DEBUG
  int size_{-1};
#endif
};

}  // namespace v8::internal::wasm

#endif  // V8_WASM_WELL_KNOWN_IMPORTS_H_
