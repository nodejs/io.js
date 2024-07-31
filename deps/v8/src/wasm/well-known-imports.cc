// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/well-known-imports.h"

namespace v8::internal::wasm {

const char* WellKnownImportName(WellKnownImport wki) {
  switch (wki) {
    // Generic:
    case WellKnownImport::kUninstantiated:
      return "uninstantiated";
    case WellKnownImport::kGeneric:
      return "generic";
    case WellKnownImport::kLinkError:
      return "LinkError";

    // DataView methods:
    case WellKnownImport::kDataViewGetBigInt64:
      return "DataView.getBigInt64";
    case WellKnownImport::kDataViewGetBigUint64:
      return "DataView.getBigUint64";
    case WellKnownImport::kDataViewGetFloat32:
      return "DataView.getFloat32";
    case WellKnownImport::kDataViewGetFloat64:
      return "DataView.getFloat64";
    case WellKnownImport::kDataViewGetInt8:
      return "DataView.getInt8";
    case WellKnownImport::kDataViewGetInt16:
      return "DataView.getInt16";
    case WellKnownImport::kDataViewGetInt32:
      return "DataView.getInt32";
    case WellKnownImport::kDataViewGetUint8:
      return "DataView.getUint8";
    case WellKnownImport::kDataViewGetUint16:
      return "DataView.getUint16";
    case WellKnownImport::kDataViewGetUint32:
      return "DataView.getUint32";
    case WellKnownImport::kDataViewSetBigInt64:
      return "DataView.setBigInt64";
    case WellKnownImport::kDataViewSetBigUint64:
      return "DataView.setBigUint64";
    case WellKnownImport::kDataViewSetFloat32:
      return "DataView.setFloat32";
    case WellKnownImport::kDataViewSetFloat64:
      return "DataView.setFloat64";
    case WellKnownImport::kDataViewSetInt8:
      return "DataView.setInt8";
    case WellKnownImport::kDataViewSetInt16:
      return "DataView.setInt16";
    case WellKnownImport::kDataViewSetInt32:
      return "DataView.setInt32";
    case WellKnownImport::kDataViewSetUint8:
      return "DataView.setUint8";
    case WellKnownImport::kDataViewSetUint16:
      return "DataView.setUint16";
    case WellKnownImport::kDataViewSetUint32:
      return "DataView.setUint32";
    case WellKnownImport::kDataViewByteLength:
      return "DataView.byteLength";

      // String-related functions:
    case WellKnownImport::kDoubleToString:
      return "DoubleToString";
    case WellKnownImport::kIntToString:
      return "IntToString";
    case WellKnownImport::kParseFloat:
      return "ParseFloat";
    case WellKnownImport::kStringIndexOf:
    case WellKnownImport::kStringIndexOfImported:
      return "String.indexOf";
    case WellKnownImport::kStringToLocaleLowerCaseStringref:
      return "String.toLocaleLowerCase";
    case WellKnownImport::kStringToLowerCaseStringref:
    case WellKnownImport::kStringToLowerCaseImported:
      return "String.toLowerCase";

      // JS String Builtins:
    case WellKnownImport::kStringCast:
      return "js-string:cast";
    case WellKnownImport::kStringCharCodeAt:
      return "js-string:charCodeAt";
    case WellKnownImport::kStringCodePointAt:
      return "js-string:codePointAt";
    case WellKnownImport::kStringCompare:
      return "js-string:compare";
    case WellKnownImport::kStringConcat:
      return "js-string:concat";
    case WellKnownImport::kStringEquals:
      return "js-string:equals";
    case WellKnownImport::kStringFromCharCode:
      return "js-string:fromCharCode";
    case WellKnownImport::kStringFromCodePoint:
      return "js-string:fromCodePoint";
    case WellKnownImport::kStringFromUtf8Array:
      return "text-decoder:decodeStringFromUTF8Array";
    case WellKnownImport::kStringFromWtf16Array:
      return "js-string:fromCharCodeArray";
    case WellKnownImport::kStringIntoUtf8Array:
      return "text-encoder:encodeStringIntoUTF8Array";
    case WellKnownImport::kStringToUtf8Array:
      return "text-encoder:encodeStringToUTF8Array";
    case WellKnownImport::kStringLength:
      return "js-string:length";
    case WellKnownImport::kStringMeasureUtf8:
      return "text-encoder:measureStringAsUTF8";
    case WellKnownImport::kStringSubstring:
      return "js-string:substring";
    case WellKnownImport::kStringTest:
      return "js-string:test";
    case WellKnownImport::kStringToWtf16Array:
      return "js-string:intoCharCodeArray";

      // Fast API Call:
    case WellKnownImport::kFastAPICall:
      return "fast API call";
  }
}

WellKnownImportsList::UpdateResult WellKnownImportsList::Update(
    base::Vector<WellKnownImport> entries) {
  DCHECK_EQ(entries.size(), static_cast<size_t>(size_));
  {
    base::MutexGuard lock(&mutex_);
    for (size_t i = 0; i < entries.size(); i++) {
      WellKnownImport entry = entries[i];
      DCHECK(entry != WellKnownImport::kUninstantiated);
      WellKnownImport old = statuses_[i].load(std::memory_order_relaxed);
      if (old == WellKnownImport::kGeneric) continue;
      if (old == entry) continue;
      if (old == WellKnownImport::kUninstantiated) {
        statuses_[i].store(entry, std::memory_order_relaxed);
      } else {
        // To avoid having to clear Turbofan code multiple times, we give up
        // entirely once the first problem occurs.
        // This is a heuristic; we could also choose to make finer-grained
        // decisions and only set {statuses_[i] = kGeneric}. We expect that
        // this case won't ever happen for production modules, so guarding
        // against pathological cases seems more important than being lenient
        // towards almost-well-behaved modules.
        for (size_t j = 0; j < entries.size(); j++) {
          statuses_[j].store(WellKnownImport::kGeneric,
                             std::memory_order_relaxed);
        }
        return UpdateResult::kFoundIncompatibility;
      }
    }
  }
  return UpdateResult::kOK;
}

void WellKnownImportsList::Initialize(
    base::Vector<const WellKnownImport> entries) {
  DCHECK_EQ(entries.size(), static_cast<size_t>(size_));
  for (size_t i = 0; i < entries.size(); i++) {
    DCHECK_EQ(WellKnownImport::kUninstantiated,
              statuses_[i].load(std::memory_order_relaxed));
    statuses_[i].store(entries[i], std::memory_order_relaxed);
  }
}

}  // namespace v8::internal::wasm
