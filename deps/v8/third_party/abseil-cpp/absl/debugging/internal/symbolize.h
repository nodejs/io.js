// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file contains internal parts of the Abseil symbolizer.
// Do not depend on the anything in this file, it may change at anytime.

#ifndef ABSL_DEBUGGING_INTERNAL_SYMBOLIZE_H_
#define ABSL_DEBUGGING_INTERNAL_SYMBOLIZE_H_

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>

#include "absl/base/config.h"
#include "absl/strings/string_view.h"

#ifdef ABSL_INTERNAL_HAVE_ELF_SYMBOLIZE
#error ABSL_INTERNAL_HAVE_ELF_SYMBOLIZE cannot be directly set
#elif defined(__ELF__) && defined(__GLIBC__) && !defined(__native_client__) \
      && !defined(__asmjs__) && !defined(__wasm__)
#define ABSL_INTERNAL_HAVE_ELF_SYMBOLIZE 1

#include <elf.h>
#include <link.h>  // For ElfW() macro.
#include <functional>
#include <string>

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

// Iterates over all sections, invoking callback on each with the section name
// and the section header.
//
// Returns true on success; otherwise returns false in case of errors.
//
// This is not async-signal-safe.
bool ForEachSection(int fd,
                    const std::function<bool(absl::string_view name,
                                             const ElfW(Shdr) &)>& callback);

// Gets the section header for the given name, if it exists. Returns true on
// success. Otherwise, returns false.
bool GetSectionHeaderByName(int fd, const char *name, size_t name_len,
                            ElfW(Shdr) *out);

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_INTERNAL_HAVE_ELF_SYMBOLIZE

#ifdef ABSL_INTERNAL_HAVE_DARWIN_SYMBOLIZE
#error ABSL_INTERNAL_HAVE_DARWIN_SYMBOLIZE cannot be directly set
#elif defined(__APPLE__)
#define ABSL_INTERNAL_HAVE_DARWIN_SYMBOLIZE 1
#endif

#ifdef ABSL_INTERNAL_HAVE_EMSCRIPTEN_SYMBOLIZE
#error ABSL_INTERNAL_HAVE_EMSCRIPTEN_SYMBOLIZE cannot be directly set
#elif defined(__EMSCRIPTEN__)
#define ABSL_INTERNAL_HAVE_EMSCRIPTEN_SYMBOLIZE 1
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {

struct SymbolDecoratorArgs {
  // The program counter we are getting symbolic name for.
  const void *pc;
  // 0 for main executable, load address for shared libraries.
  ptrdiff_t relocation;
  // Read-only file descriptor for ELF image covering "pc",
  // or -1 if no such ELF image exists in /proc/self/maps.
  int fd;
  // Output buffer, size.
  // Note: the buffer may not be empty -- default symbolizer may have already
  // produced some output, and earlier decorators may have adorned it in
  // some way. You are free to replace or augment the contents (within the
  // symbol_buf_size limit).
  char *const symbol_buf;
  size_t symbol_buf_size;
  // Temporary scratch space, size.
  // Use that space in preference to allocating your own stack buffer to
  // conserve stack.
  char *const tmp_buf;
  size_t tmp_buf_size;
  // User-provided argument
  void* arg;
};
using SymbolDecorator = void (*)(const SymbolDecoratorArgs *);

// Installs a function-pointer as a decorator. Returns a value less than zero
// if the system cannot install the decorator. Otherwise, returns a unique
// identifier corresponding to the decorator. This identifier can be used to
// uninstall the decorator - See RemoveSymbolDecorator() below.
int InstallSymbolDecorator(SymbolDecorator decorator, void* arg);

// Removes a previously installed function-pointer decorator. Parameter "ticket"
// is the return-value from calling InstallSymbolDecorator().
bool RemoveSymbolDecorator(int ticket);

// Remove all installed decorators.  Returns true if successful, false if
// symbolization is currently in progress.
bool RemoveAllSymbolDecorators();

// Registers an address range to a file mapping.
//
// Preconditions:
//   start <= end
//   filename != nullptr
//
// Returns true if the file was successfully registered.
bool RegisterFileMappingHint(const void* start, const void* end,
                             uint64_t offset, const char* filename);

// Looks up the file mapping registered by RegisterFileMappingHint for an
// address range. If there is one, the file name is stored in *filename and
// *start and *end are modified to reflect the registered mapping. Returns
// whether any hint was found.
bool GetFileMappingHint(const void** start, const void** end, uint64_t* offset,
                        const char** filename);

}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // __cplusplus

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
#endif  // __cplusplus

    bool
    AbslInternalGetFileMappingHint(const void** start, const void** end,
                                   uint64_t* offset, const char** filename);

#endif  // ABSL_DEBUGGING_INTERNAL_SYMBOLIZE_H_
