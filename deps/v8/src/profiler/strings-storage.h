// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_STRINGS_STORAGE_H_
#define V8_PROFILER_STRINGS_STORAGE_H_

#include <stdarg.h>

#include "src/base/compiler-specific.h"
#include "src/base/hashmap.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class Name;
class Symbol;

// Provides a storage of strings allocated in C++ heap, to hold them
// forever, even if they disappear from JS heap or external storage.
class V8_EXPORT_PRIVATE StringsStorage {
 public:
  StringsStorage();
  ~StringsStorage();
  StringsStorage(const StringsStorage&) = delete;
  StringsStorage& operator=(const StringsStorage&) = delete;

  // Copies the given c-string and stores it, returning the stored copy, or just
  // returns the existing string in storage if it already exists.
  const char* GetCopy(const char* src);
  // Returns a formatted string, de-duplicated via the storage.
  PRINTF_FORMAT(2, 3) const char* GetFormatted(const char* format, ...);
  // Returns a stored string resulting from name, or "<symbol>" for a symbol.
  const char* GetName(Tagged<Name> name);
  // Returns the string representation of the int from the store.
  const char* GetName(int index);
  // Appends string resulting from name to prefix, then returns the stored
  // result.
  const char* GetConsName(const char* prefix, Tagged<Name> name);
  // Reduces the refcount of the given string, freeing it if no other
  // references are made to it. Returns true if the string was successfully
  // unref'd, or false if the string was not present in the table.
  bool Release(const char* str);

  // Returns the number of strings in the store.
  size_t GetStringCountForTesting() const;

  // Returns the size of strings in the store
  size_t GetStringSize();

  // Returns true if the strings table is empty.
  bool empty() const { return names_.occupancy() == 0; }

 private:
  static bool StringsMatch(void* key1, void* key2);
  // Adds the string to storage and returns it, or if a matching string exists
  // in the storage, deletes str and returns the matching string instead.
  const char* AddOrDisposeString(char* str, size_t len);
  base::CustomMatcherHashMap::Entry* GetEntry(const char* str, size_t len);
  PRINTF_FORMAT(2, 0)
  const char* GetVFormatted(const char* format, va_list args);
  const char* GetSymbol(Tagged<Symbol> sym);

  base::CustomMatcherHashMap names_;
  base::SpinningMutex mutex_;
  size_t string_size_ = 0;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_STRINGS_STORAGE_H_
