// Copyright 2017 The Abseil Authors.
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
//

#ifndef ABSL_BASE_INTERNAL_LOW_LEVEL_ALLOC_H_
#define ABSL_BASE_INTERNAL_LOW_LEVEL_ALLOC_H_

// A simple thread-safe memory allocator that does not depend on
// mutexes or thread-specific data.  It is intended to be used
// sparingly, and only when malloc() would introduce an unwanted
// dependency, such as inside the heap-checker, or the Mutex
// implementation.

// IWYU pragma: private, include "base/low_level_alloc.h"

#include <sys/types.h>

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/config.h"

// LowLevelAlloc requires that the platform support low-level
// allocation of virtual memory. Platforms lacking this cannot use
// LowLevelAlloc.
#ifdef ABSL_LOW_LEVEL_ALLOC_MISSING
#error ABSL_LOW_LEVEL_ALLOC_MISSING cannot be directly set
#elif !defined(ABSL_HAVE_MMAP) && !defined(_WIN32)
#define ABSL_LOW_LEVEL_ALLOC_MISSING 1
#endif

// Using LowLevelAlloc with kAsyncSignalSafe isn't supported on Windows or
// asm.js / WebAssembly.
// See https://kripken.github.io/emscripten-site/docs/porting/pthreads.html
// for more information.
#ifdef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
#error ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING cannot be directly set
#elif defined(_WIN32) || defined(__asmjs__) || defined(__wasm__) || \
    defined(__hexagon__)
#define ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING 1
#endif

#include <cstddef>

#include "absl/base/port.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

class LowLevelAlloc {
 public:
  struct Arena;       // an arena from which memory may be allocated

  // Returns a pointer to a block of at least "request" bytes
  // that have been newly allocated from the specific arena.
  // for Alloc() call the DefaultArena() is used.
  // Returns 0 if passed request==0.
  // Does not return 0 under other circumstances; it crashes if memory
  // is not available.
  static void *Alloc(size_t request) ABSL_ATTRIBUTE_SECTION(malloc_hook);
  static void *AllocWithArena(size_t request, Arena *arena)
      ABSL_ATTRIBUTE_SECTION(malloc_hook);

  // Deallocates a region of memory that was previously allocated with
  // Alloc().   Does nothing if passed 0.   "s" must be either 0,
  // or must have been returned from a call to Alloc() and not yet passed to
  // Free() since that call to Alloc().  The space is returned to the arena
  // from which it was allocated.
  static void Free(void *s) ABSL_ATTRIBUTE_SECTION(malloc_hook);

  // ABSL_ATTRIBUTE_SECTION(malloc_hook) for Alloc* and Free
  // are to put all callers of MallocHook::Invoke* in this module
  // into special section,
  // so that MallocHook::GetCallerStackTrace can function accurately.

  // Create a new arena.
  // The root metadata for the new arena is allocated in the
  // meta_data_arena; the DefaultArena() can be passed for meta_data_arena.
  // These values may be ored into flags:
  enum {
    // Report calls to Alloc() and Free() via the MallocHook interface.
    // Set in the DefaultArena.
    kCallMallocHook = 0x0001,

#ifndef ABSL_LOW_LEVEL_ALLOC_ASYNC_SIGNAL_SAFE_MISSING
    // Make calls to Alloc(), Free() be async-signal-safe. Not set in
    // DefaultArena(). Not supported on all platforms.
    kAsyncSignalSafe = 0x0002,
#endif
  };
  // Construct a new arena.  The allocation of the underlying metadata honors
  // the provided flags.  For example, the call NewArena(kAsyncSignalSafe)
  // is itself async-signal-safe, as well as generatating an arena that provides
  // async-signal-safe Alloc/Free.
  static Arena *NewArena(uint32_t flags);

  // Destroys an arena allocated by NewArena and returns true,
  // provided no allocated blocks remain in the arena.
  // If allocated blocks remain in the arena, does nothing and
  // returns false.
  // It is illegal to attempt to destroy the DefaultArena().
  static bool DeleteArena(Arena *arena);

  // The default arena that always exists.
  static Arena *DefaultArena();

 private:
  LowLevelAlloc();      // no instances
};

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_LOW_LEVEL_ALLOC_H_
