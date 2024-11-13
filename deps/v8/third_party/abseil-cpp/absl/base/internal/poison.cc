// Copyright 2024 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/internal/poison.h"

#include <cstdlib>

#include "absl/base/config.h"
#include "absl/base/internal/direct_mmap.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#if defined(ABSL_HAVE_ADDRESS_SANITIZER)
#include <sanitizer/asan_interface.h>
#elif defined(ABSL_HAVE_MEMORY_SANITIZER)
#include <sanitizer/msan_interface.h>
#elif defined(ABSL_HAVE_MMAP)
#include <sys/mman.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

namespace {

size_t GetPageSize() {
#ifdef _WIN32
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  return system_info.dwPageSize;
#elif defined(__wasm__) || defined(__asmjs__) || defined(__hexagon__)
  return getpagesize();
#else
  return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}

}  // namespace

void* InitializePoisonedPointerInternal() {
  const size_t block_size = GetPageSize();
#if defined(ABSL_HAVE_ADDRESS_SANITIZER)
  void* data = malloc(block_size);
  ASAN_POISON_MEMORY_REGION(data, block_size);
#elif defined(ABSL_HAVE_MEMORY_SANITIZER)
  void* data = malloc(block_size);
  __msan_poison(data, block_size);
#elif defined(ABSL_HAVE_MMAP)
  void* data = DirectMmap(nullptr, block_size, PROT_NONE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (data == MAP_FAILED) return GetBadPointerInternal();
#elif defined(_WIN32)
  void* data = VirtualAlloc(nullptr, block_size, MEM_RESERVE | MEM_COMMIT,
                            PAGE_NOACCESS);
  if (data == nullptr) return GetBadPointerInternal();
#else
  return GetBadPointerInternal();
#endif
  // Return the middle of the block so that dereferences before and after the
  // pointer will both crash.
  return static_cast<char*>(data) + block_size / 2;
}

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
