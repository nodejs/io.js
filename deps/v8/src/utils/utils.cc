// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/utils/utils.h"

#include <stdarg.h>
#include <sys/stat.h>

#include <cstring>
#include <vector>

#include "src/base/functional.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/wrappers.h"
#include "src/utils/allocation.h"

#ifdef V8_CC_MSVC
#include <intrin.h>  // _AddressOfReturnAddress()
#endif

namespace v8 {
namespace internal {

std::ostream& operator<<(std::ostream& os, FeedbackSlot slot) {
  return os << "#" << slot.id_;
}

size_t hash_value(BytecodeOffset id) {
  base::hash<int> h;
  return h(id.id_);
}

std::ostream& operator<<(std::ostream& os, BytecodeOffset id) {
  return os << id.id_;
}

void PrintF(const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  base::OS::VPrint(format, arguments);
  va_end(arguments);
}

void PrintF(FILE* out, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  base::OS::VFPrint(out, format, arguments);
  va_end(arguments);
}

void PrintPID(const char* format, ...) {
  base::OS::Print("[%d] ", base::OS::GetCurrentProcessId());
  va_list arguments;
  va_start(arguments, format);
  base::OS::VPrint(format, arguments);
  va_end(arguments);
}

void PrintIsolate(void* isolate, const char* format, ...) {
  base::OS::Print("[%d:%p] ", base::OS::GetCurrentProcessId(), isolate);
  va_list arguments;
  va_start(arguments, format);
  base::OS::VPrint(format, arguments);
  va_end(arguments);
}

char* ReadLine(const char* prompt) {
  char* result = nullptr;
  char line_buf[256];
  size_t offset = 0;
  bool keep_going = true;
  fprintf(stdout, "%s", prompt);
  fflush(stdout);
  while (keep_going) {
    if (fgets(line_buf, sizeof(line_buf), stdin) == nullptr) {
      // fgets got an error. Just give up.
      if (result != nullptr) {
        DeleteArray(result);
      }
      return nullptr;
    }
    size_t len = strlen(line_buf);
    if (len > 1 && line_buf[len - 2] == '\\' && line_buf[len - 1] == '\n') {
      // When we read a line that ends with a "\" we remove the escape and
      // append the remainder.
      line_buf[len - 2] = '\n';
      line_buf[len - 1] = 0;
      len -= 1;
    } else if ((len > 0) && (line_buf[len - 1] == '\n')) {
      // Since we read a new line we are done reading the line. This
      // will exit the loop after copying this buffer into the result.
      keep_going = false;
    }
    if (result == nullptr) {
      // Allocate the initial result and make room for the terminating '\0'
      result = NewArray<char>(len + 1);
    } else {
      // Allocate a new result with enough room for the new addition.
      size_t new_len = offset + len + 1;
      char* new_result = NewArray<char>(new_len);
      // Copy the existing input into the new array and set the new
      // array as the result.
      std::memcpy(new_result, result, offset * kCharSize);
      DeleteArray(result);
      result = new_result;
    }
    // Copy the newly read line into the result.
    std::memcpy(result + offset, line_buf, len * kCharSize);
    offset += len;
  }
  DCHECK_NOT_NULL(result);
  result[offset] = '\0';
  return result;
}

namespace {

std::vector<char> ReadCharsFromFile(FILE* file, bool* exists, bool verbose,
                                    const char* filename) {
  if (file == nullptr || fseek(file, 0, SEEK_END) != 0) {
    if (verbose) {
      base::OS::PrintError("Cannot read from file %s.\n", filename);
    }
    *exists = false;
    return std::vector<char>();
  }

  // Get the size of the file and rewind it.
  ptrdiff_t size = ftell(file);
  rewind(file);

  std::vector<char> result(size);
  for (ptrdiff_t i = 0; i < size && feof(file) == 0;) {
    ptrdiff_t read = fread(result.data() + i, 1, size - i, file);
    if (read != (size - i) && ferror(file) != 0) {
      base::Fclose(file);
      *exists = false;
      return std::vector<char>();
    }
    i += read;
  }
  *exists = true;
  return result;
}

std::vector<char> ReadCharsFromFile(const char* filename, bool* exists,
                                    bool verbose) {
  FILE* file = base::OS::FOpen(filename, "rb");
  std::vector<char> result = ReadCharsFromFile(file, exists, verbose, filename);
  if (file != nullptr) base::Fclose(file);
  return result;
}

std::string VectorToString(const std::vector<char>& chars) {
  if (chars.empty()) {
    return std::string();
  }
  return std::string(chars.begin(), chars.end());
}

int WriteCharsToFile(const char* str, int size, FILE* f) {
  int total = 0;
  while (total < size) {
    int write = static_cast<int>(fwrite(str, 1, size - total, f));
    if (write == 0) {
      return total;
    }
    total += write;
    str += write;
  }
  return total;
}

}  // namespace

std::string ReadFile(const char* filename, bool* exists, bool verbose) {
  std::vector<char> result = ReadCharsFromFile(filename, exists, verbose);
  return VectorToString(result);
}

std::string ReadFile(FILE* file, bool* exists, bool verbose) {
  std::vector<char> result = ReadCharsFromFile(file, exists, verbose, "");
  return VectorToString(result);
}

int WriteChars(const char* filename, const char* str, int size, bool verbose) {
  FILE* f = base::OS::FOpen(filename, "wb");
  if (f == nullptr) {
    if (verbose) {
      base::OS::PrintError("Cannot open file %s for writing.\n", filename);
    }
    return 0;
  }
  int written = WriteCharsToFile(str, size, f);
  base::Fclose(f);
  return written;
}

int WriteBytes(const char* filename, const uint8_t* bytes, int size,
               bool verbose) {
  const char* str = reinterpret_cast<const char*>(bytes);
  return WriteChars(filename, str, size, verbose);
}

// Returns false iff d is NaN, +0, or -0.
bool DoubleToBoolean(double d) {
  IeeeDoubleArchType u;
  u.d = d;
  if (u.bits.exp == 2047) {
    // Detect NaN for IEEE double precision floating point.
    if ((u.bits.man_low | u.bits.man_high) != 0) return false;
  }
  if (u.bits.exp == 0) {
    // Detect +0, and -0 for IEEE double precision floating point.
    if ((u.bits.man_low | u.bits.man_high) == 0) return false;
  }
  return true;
}

uintptr_t GetCurrentStackPosition() {
#if V8_CC_MSVC
  return reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
#else
  return reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
#endif
}

// The filter is a pattern that matches function names in this way:
//   "*"      all; the default
//   "-"      all but the top-level function
//   "-name"  all but the function "name"
//   ""       only the top-level function
//   "name"   only the function "name"
//   "name*"  only functions starting with "name"
//   "~"      none; the tilde is not an identifier
bool PassesFilter(base::Vector<const char> name,
                  base::Vector<const char> filter) {
  if (filter.empty()) return name.empty();
  auto filter_it = filter.begin();
  bool positive_filter = true;
  if (*filter_it == '-') {
    ++filter_it;
    positive_filter = false;
  }
  if (filter_it == filter.end()) return !name.empty();
  if (*filter_it == '*') return positive_filter;
  if (*filter_it == '~') return !positive_filter;

  bool prefix_match = filter[filter.size() - 1] == '*';
  size_t min_match_length = filter.size();
  if (!positive_filter) min_match_length--;  // Subtract 1 for leading '-'.
  if (prefix_match) min_match_length--;      // Subtract 1 for trailing '*'.

  if (name.size() < min_match_length) return !positive_filter;

  // TODO(sigurds): Use the new version of std::mismatch here, once we
  // can assume C++14.
  auto res = std::mismatch(filter_it, filter.end(), name.begin());
  if (res.first == filter.end()) {
    if (res.second == name.end()) {
      // The strings match, so {name} passes if we have a {positive_filter}.
      return positive_filter;
    }
    // {name} is longer than the filter, so {name} passes if we don't have a
    // {positive_filter}.
    return !positive_filter;
  }
  if (*res.first == '*') {
    // We matched up to the wildcard, so {name} passes if we have a
    // {positive_filter}.
    return positive_filter;
  }
  // We don't match, so {name} passes if we don't have a {positive_filter}.
  return !positive_filter;
}

}  // namespace internal
}  // namespace v8
