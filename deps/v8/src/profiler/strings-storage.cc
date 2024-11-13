// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/strings-storage.h"

#include <memory>

#include "src/base/bits.h"
#include "src/base/strings.h"
#include "src/objects/objects-inl.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

bool StringsStorage::StringsMatch(void* key1, void* key2) {
  return strcmp(reinterpret_cast<char*>(key1), reinterpret_cast<char*>(key2)) ==
         0;
}

StringsStorage::StringsStorage() : names_(StringsMatch) {}

StringsStorage::~StringsStorage() {
  for (base::HashMap::Entry* p = names_.Start(); p != nullptr;
       p = names_.Next(p)) {
    DeleteArray(reinterpret_cast<const char*>(p->key));
  }
}

const char* StringsStorage::GetCopy(const char* src) {
  base::MutexGuard guard(&mutex_);
  int len = static_cast<int>(strlen(src));
  base::HashMap::Entry* entry = GetEntry(src, len);
  if (entry->value == nullptr) {
    base::Vector<char> dst = base::Vector<char>::New(len + 1);
    base::StrNCpy(dst, src, len);
    dst[len] = '\0';
    entry->key = dst.begin();
    string_size_ += len;
  }
  entry->value =
      reinterpret_cast<void*>(reinterpret_cast<size_t>(entry->value) + 1);
  return reinterpret_cast<const char*>(entry->key);
}

const char* StringsStorage::GetFormatted(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const char* result = GetVFormatted(format, args);
  va_end(args);
  return result;
}

const char* StringsStorage::AddOrDisposeString(char* str, int len) {
  base::MutexGuard guard(&mutex_);
  base::HashMap::Entry* entry = GetEntry(str, len);
  if (entry->value == nullptr) {
    // New entry added.
    entry->key = str;
    string_size_ += len;
  } else {
    DeleteArray(str);
  }
  entry->value =
      reinterpret_cast<void*>(reinterpret_cast<size_t>(entry->value) + 1);
  return reinterpret_cast<const char*>(entry->key);
}

const char* StringsStorage::GetVFormatted(const char* format, va_list args) {
  base::Vector<char> str = base::Vector<char>::New(1024);
  int len = base::VSNPrintF(str, format, args);
  if (len == -1) {
    DeleteArray(str.begin());
    return GetCopy(format);
  }
  return AddOrDisposeString(str.begin(), len);
}

const char* StringsStorage::GetSymbol(Tagged<Symbol> sym) {
  if (!IsString(sym->description())) {
    return "<symbol>";
  }
  Tagged<String> description = Cast<String>(sym->description());
  int length = std::min(v8_flags.heap_snapshot_string_limit.value(),
                        description->length());
  auto data = description->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL, 0,
                                     length, &length);
  if (sym->is_private_name()) {
    return AddOrDisposeString(data.release(), length);
  }
  auto str_length = 8 + length + 1 + 1;
  auto str_result = NewArray<char>(str_length);
  snprintf(str_result, str_length, "<symbol %s>", data.get());
  return AddOrDisposeString(str_result, str_length - 1);
}

const char* StringsStorage::GetName(Tagged<Name> name) {
  if (IsString(name)) {
    Tagged<String> str = Cast<String>(name);
    int length =
        std::min(v8_flags.heap_snapshot_string_limit.value(), str->length());
    int actual_length = 0;
    std::unique_ptr<char[]> data = str->ToCString(
        DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL, 0, length, &actual_length);
    return AddOrDisposeString(data.release(), actual_length);
  } else if (IsSymbol(name)) {
    return GetSymbol(Cast<Symbol>(name));
  }
  return "";
}

const char* StringsStorage::GetName(int index) {
  return GetFormatted("%d", index);
}

const char* StringsStorage::GetConsName(const char* prefix, Tagged<Name> name) {
  if (IsString(name)) {
    Tagged<String> str = Cast<String>(name);
    int length =
        std::min(v8_flags.heap_snapshot_string_limit.value(), str->length());
    int actual_length = 0;
    std::unique_ptr<char[]> data = str->ToCString(
        DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL, 0, length, &actual_length);

    int cons_length = actual_length + static_cast<int>(strlen(prefix)) + 1;
    char* cons_result = NewArray<char>(cons_length);
    snprintf(cons_result, cons_length, "%s%s", prefix, data.get());

    return AddOrDisposeString(cons_result, cons_length - 1);
  } else if (IsSymbol(name)) {
    return GetSymbol(Cast<Symbol>(name));
  }
  return "";
}

namespace {

inline uint32_t ComputeStringHash(const char* str, int len) {
  uint32_t raw_hash_field = base::bits::RotateLeft32(
      StringHasher::HashSequentialString(str, len, kZeroHashSeed), 2);
  return Name::HashBits::decode(raw_hash_field);
}

}  // namespace

bool StringsStorage::Release(const char* str) {
  base::MutexGuard guard(&mutex_);
  int len = static_cast<int>(strlen(str));
  uint32_t hash = ComputeStringHash(str, len);
  base::HashMap::Entry* entry = names_.Lookup(const_cast<char*>(str), hash);

  // If an entry wasn't found or the address of the found entry doesn't match
  // the one passed in, this string wasn't managed by this StringsStorage
  // instance (i.e. a constant). Ignore this.
  if (!entry || entry->key != str) {
    return false;
  }

  DCHECK(entry->value);
  entry->value =
      reinterpret_cast<void*>(reinterpret_cast<size_t>(entry->value) - 1);

  if (entry->value == 0) {
    string_size_ -= len;
    names_.Remove(const_cast<char*>(str), hash);
    DeleteArray(str);
  }
  return true;
}

size_t StringsStorage::GetStringCountForTesting() const {
  return names_.occupancy();
}

size_t StringsStorage::GetStringSize() {
  base::MutexGuard guard(&mutex_);
  return string_size_;
}

base::HashMap::Entry* StringsStorage::GetEntry(const char* str, int len) {
  uint32_t hash = ComputeStringHash(str, len);
  return names_.LookupOrInsert(const_cast<char*>(str), hash);
}

}  // namespace internal
}  // namespace v8
