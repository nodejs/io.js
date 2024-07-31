// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/extensions/externalize-string-extension.h"

#include "include/v8-template.h"
#include "src/api/api-inl.h"
#include "src/base/strings.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

template <typename Char, typename Base>
class SimpleStringResource : public Base {
 public:
  // Takes ownership of |data|.
  SimpleStringResource(Char* data, size_t length)
      : data_(data),
        length_(length) {}

  ~SimpleStringResource() override { delete[] data_; }

  const Char* data() const override { return data_; }

  size_t length() const override { return length_; }

 private:
  Char* const data_;
  const size_t length_;
};

using SimpleOneByteStringResource =
    SimpleStringResource<char, v8::String::ExternalOneByteStringResource>;
using SimpleTwoByteStringResource =
    SimpleStringResource<base::uc16, v8::String::ExternalStringResource>;

static constexpr int kMinOneByteLength =
    kExternalPointerSlotSize - kTaggedSize + 1;
static constexpr int kMinTwoByteLength =
    (kExternalPointerSlotSize - kTaggedSize) / sizeof(base::uc16) + 1;
static constexpr int kMinOneByteCachedLength =
    2 * kExternalPointerSlotSize - kTaggedSize + 1;
static constexpr int kMinTwoByteCachedLength =
    (2 * kExternalPointerSlotSize - kTaggedSize) / sizeof(base::uc16) + 1;

// static
const char* ExternalizeStringExtension::BuildSource(char* buf, size_t size) {
  base::SNPrintF(base::Vector<char>(buf, static_cast<int>(size)),
                 "native function externalizeString();"
                 "native function createExternalizableString();"
                 "native function isOneByteString();"
                 "let kExternalStringMinOneByteLength = %d;"
                 "let kExternalStringMinTwoByteLength = %d;"
                 "let kExternalStringMinOneByteCachedLength = %d;"
                 "let kExternalStringMinTwoByteCachedLength = %d;",
                 kMinOneByteLength, kMinTwoByteLength, kMinOneByteCachedLength,
                 kMinTwoByteCachedLength);
  return buf;
}
v8::Local<v8::FunctionTemplate>
ExternalizeStringExtension::GetNativeFunctionTemplate(
    v8::Isolate* isolate, v8::Local<v8::String> str) {
  if (strcmp(*v8::String::Utf8Value(isolate, str), "externalizeString") == 0) {
    return v8::FunctionTemplate::New(isolate,
                                     ExternalizeStringExtension::Externalize);
  } else if (strcmp(*v8::String::Utf8Value(isolate, str),
                    "createExternalizableString") == 0) {
    return v8::FunctionTemplate::New(
        isolate, ExternalizeStringExtension::CreateExternalizableString);
  } else {
    DCHECK_EQ(strcmp(*v8::String::Utf8Value(isolate, str), "isOneByteString"),
              0);
    return v8::FunctionTemplate::New(isolate,
                                     ExternalizeStringExtension::IsOneByte);
  }
}

namespace {

bool HasExternalForwardingIndex(Handle<String> string) {
  if (!string->IsShared()) return false;
  uint32_t raw_hash = string->raw_hash_field(kAcquireLoad);
  return Name::IsExternalForwardingIndex(raw_hash);
}

}  // namespace

void ExternalizeStringExtension::Externalize(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  if (info.Length() < 1 || !info[0]->IsString()) {
    info.GetIsolate()->ThrowError(
        "First parameter to externalizeString() must be a string.");
    return;
  }
  bool result = false;
  Handle<String> string = Utils::OpenHandle(*info[0].As<v8::String>());
  const bool externalize_as_one_byte = string->IsOneByteRepresentation();
  if (!string->SupportsExternalization(
          externalize_as_one_byte ? v8::String::Encoding::ONE_BYTE_ENCODING
                                  : v8::String::Encoding::TWO_BYTE_ENCODING)) {
    // If the string is shared, testing with the combination of
    // --shared-string-table and --isolate in d8 may result in races to
    // externalize the same string. If GC is stressed in addition, this test
    // might fail as the string was already externalized (marked for
    // externalization by another thread and externalized during GC).
    if (!StringShape(*string).IsExternal()) {
      info.GetIsolate()->ThrowError("string does not support externalization.");
    }
    return;
  }
  if (externalize_as_one_byte) {
    uint8_t* data = new uint8_t[string->length()];
    String::WriteToFlat(*string, data, 0, string->length());
    SimpleOneByteStringResource* resource = new SimpleOneByteStringResource(
        reinterpret_cast<char*>(data), string->length());
    result = Utils::ToLocal(string)->MakeExternal(resource);
    if (!result) delete resource;
  } else {
    base::uc16* data = new base::uc16[string->length()];
    String::WriteToFlat(*string, data, 0, string->length());
    SimpleTwoByteStringResource* resource = new SimpleTwoByteStringResource(
        data, string->length());
    result = Utils::ToLocal(string)->MakeExternal(resource);
    if (!result) delete resource;
  }
  // If the string is shared, testing with the combination of
  // --shared-string-table and --isolate in d8 may result in races to
  // externalize the same string. Those races manifest as externalization
  // sometimes failing if another thread won and already forwarded the string to
  // the external resource. Don't consider those races as failures.
  if (!result && !HasExternalForwardingIndex(string)) {
    info.GetIsolate()->ThrowError("externalizeString() failed.");
    return;
  }
}

namespace {

MaybeHandle<String> CopyConsStringToOld(Isolate* isolate,
                                        Handle<ConsString> string) {
  return isolate->factory()->NewConsString(handle(string->first(), isolate),
                                           handle(string->second(), isolate),
                                           AllocationType::kOld);
}

}  // namespace

void ExternalizeStringExtension::CreateExternalizableString(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  if (info.Length() < 1 || !info[0]->IsString()) {
    info.GetIsolate()->ThrowError(
        "First parameter to createExternalizableString() must be a string.");
    return;
  }
  Handle<String> string = Utils::OpenHandle(*info[0].As<v8::String>());
  Isolate* isolate = reinterpret_cast<Isolate*>(info.GetIsolate());
  v8::String::Encoding encoding = string->IsOneByteRepresentation()
                                      ? v8::String::Encoding::ONE_BYTE_ENCODING
                                      : v8::String::Encoding::TWO_BYTE_ENCODING;
  if (string->SupportsExternalization(encoding)) {
    info.GetReturnValue().Set(Utils::ToLocal(string));
    return;
  }
  // Return the string if it is already externalized.
  if (StringShape(*string).IsExternal()) {
    info.GetReturnValue().Set(Utils::ToLocal(string));
    return;
  }

  // Read-only strings are never externalizable. Don't try to copy them as
  // some parts of the code might rely on some strings being in RO space (i.e.
  // empty string).
  if (IsReadOnlyHeapObject(*string)) {
    info.GetIsolate()->ThrowError("Read-only strings cannot be externalized.");
    return;
  }
#ifdef V8_COMPRESS_POINTERS
  // Small strings may not be in-place externalizable.
  if (string->Size() < static_cast<int>(sizeof(UncachedExternalString))) {
    info.GetIsolate()->ThrowError("String is too short to be externalized.");
    return;
  }
#endif

  // Special handling for ConsStrings, as the ConsString -> ExternalString
  // migration is special for GC (Tagged pointers to Untagged pointers).
  // Skip if the ConsString is flat (second is empty), as we won't be guaranteed
  // a string in old space in that case.
  if (IsConsString(*string, isolate) && !string->IsFlat()) {
    Handle<String> result;
    if (CopyConsStringToOld(isolate, Handle<ConsString>::cast(string))
            .ToHandle(&result)) {
      DCHECK(result->SupportsExternalization(encoding));
      info.GetReturnValue().Set(Utils::ToLocal(result));
      return;
    }
  }
  // All other strings can be implicitly flattened.
  if (encoding == v8::String::ONE_BYTE_ENCODING) {
    MaybeHandle<SeqOneByteString> maybe_result =
        isolate->factory()->NewRawOneByteString(string->length(),
                                                AllocationType::kOld);
    Handle<SeqOneByteString> result;
    if (maybe_result.ToHandle(&result)) {
      DisallowGarbageCollection no_gc;
      String::WriteToFlat(*string, result->GetChars(no_gc), 0,
                          string->length());
      DCHECK(result->SupportsExternalization(encoding));
      info.GetReturnValue().Set(Utils::ToLocal(Handle<String>::cast(result)));
      return;
    }
  } else {
    MaybeHandle<SeqTwoByteString> maybe_result =
        isolate->factory()->NewRawTwoByteString(string->length(),
                                                AllocationType::kOld);
    Handle<SeqTwoByteString> result;
    if (maybe_result.ToHandle(&result)) {
      DisallowGarbageCollection no_gc;
      String::WriteToFlat(*string, result->GetChars(no_gc), 0,
                          string->length());
      DCHECK(result->SupportsExternalization(encoding));
      info.GetReturnValue().Set(Utils::ToLocal(Handle<String>::cast(result)));
      return;
    }
  }
  info.GetIsolate()->ThrowError("Unable to create string");
}

void ExternalizeStringExtension::IsOneByte(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  DCHECK(ValidateCallbackInfo(info));
  if (info.Length() != 1 || !info[0]->IsString()) {
    info.GetIsolate()->ThrowError(
        "isOneByteString() requires a single string argument.");
    return;
  }
  bool is_one_byte = Utils::OpenDirectHandle(*info[0].As<v8::String>())
                         ->IsOneByteRepresentation();
  info.GetReturnValue().Set(is_one_byte);
}

}  // namespace internal
}  // namespace v8
