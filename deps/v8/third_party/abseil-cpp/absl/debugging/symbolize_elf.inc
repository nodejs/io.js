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

// This library provides Symbolize() function that symbolizes program
// counters to their corresponding symbol names on linux platforms.
// This library has a minimal implementation of an ELF symbol table
// reader (i.e. it doesn't depend on libelf, etc.).
//
// The algorithm used in Symbolize() is as follows.
//
//   1. Go through a list of maps in /proc/self/maps and find the map
//   containing the program counter.
//
//   2. Open the mapped file and find a regular symbol table inside.
//   Iterate over symbols in the symbol table and look for the symbol
//   containing the program counter.  If such a symbol is found,
//   obtain the symbol name, and demangle the symbol if possible.
//   If the symbol isn't found in the regular symbol table (binary is
//   stripped), try the same thing with a dynamic symbol table.
//
// Note that Symbolize() is originally implemented to be used in
// signal handlers, hence it doesn't use malloc() and other unsafe
// operations.  It should be both thread-safe and async-signal-safe.
//
// Implementation note:
//
// We don't use heaps but only use stacks.  We want to reduce the
// stack consumption so that the symbolizer can run on small stacks.
//
// Here are some numbers collected with GCC 4.1.0 on x86:
// - sizeof(Elf32_Sym)  = 16
// - sizeof(Elf32_Shdr) = 40
// - sizeof(Elf64_Sym)  = 24
// - sizeof(Elf64_Shdr) = 64
//
// This implementation is intended to be async-signal-safe but uses some
// functions which are not guaranteed to be so, such as memchr() and
// memmove().  We assume they are async-signal-safe.

#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <link.h>  // For ElfW() macro.
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "absl/base/casts.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/internal/low_level_alloc.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/port.h"
#include "absl/debugging/internal/demangle.h"
#include "absl/debugging/internal/vdso_support.h"
#include "absl/strings/string_view.h"

#if defined(__FreeBSD__) && !defined(ElfW)
#define ElfW(x) __ElfN(x)
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN

// Value of argv[0]. Used by MaybeInitializeObjFile().
static char *argv0_value = nullptr;

void InitializeSymbolizer(const char *argv0) {
#ifdef ABSL_HAVE_VDSO_SUPPORT
  // We need to make sure VDSOSupport::Init() is called before any setuid or
  // chroot calls, so InitializeSymbolizer() should be called very early in the
  // life of a program.
  absl::debugging_internal::VDSOSupport::Init();
#endif
  if (argv0_value != nullptr) {
    free(argv0_value);
    argv0_value = nullptr;
  }
  if (argv0 != nullptr && argv0[0] != '\0') {
    argv0_value = strdup(argv0);
  }
}

namespace debugging_internal {
namespace {

// Re-runs fn until it doesn't cause EINTR.
#define NO_INTR(fn) \
  do {              \
  } while ((fn) < 0 && errno == EINTR)

// On Linux, ELF_ST_* are defined in <linux/elf.h>.  To make this portable
// we define our own ELF_ST_BIND and ELF_ST_TYPE if not available.
#ifndef ELF_ST_BIND
#define ELF_ST_BIND(info) (((unsigned char)(info)) >> 4)
#endif

#ifndef ELF_ST_TYPE
#define ELF_ST_TYPE(info) (((unsigned char)(info)) & 0xF)
#endif

// Some platforms use a special .opd section to store function pointers.
const char kOpdSectionName[] = ".opd";

#if (defined(__powerpc__) && !(_CALL_ELF > 1)) || defined(__ia64)
// Use opd section for function descriptors on these platforms, the function
// address is the first word of the descriptor.
enum { kPlatformUsesOPDSections = 1 };
#else  // not PPC or IA64
enum { kPlatformUsesOPDSections = 0 };
#endif

// This works for PowerPC & IA64 only.  A function descriptor consist of two
// pointers and the first one is the function's entry.
const size_t kFunctionDescriptorSize = sizeof(void *) * 2;

const int kMaxDecorators = 10;  // Seems like a reasonable upper limit.

struct InstalledSymbolDecorator {
  SymbolDecorator fn;
  void *arg;
  int ticket;
};

int g_num_decorators;
InstalledSymbolDecorator g_decorators[kMaxDecorators];

struct FileMappingHint {
  const void *start;
  const void *end;
  uint64_t offset;
  const char *filename;
};

// Protects g_decorators.
// We are using SpinLock and not a Mutex here, because we may be called
// from inside Mutex::Lock itself, and it prohibits recursive calls.
// This happens in e.g. base/stacktrace_syscall_unittest.
// Moreover, we are using only TryLock(), if the decorator list
// is being modified (is busy), we skip all decorators, and possibly
// loose some info. Sorry, that's the best we could do.
ABSL_CONST_INIT absl::base_internal::SpinLock g_decorators_mu(
    absl::kConstInit, absl::base_internal::SCHEDULE_KERNEL_ONLY);

const int kMaxFileMappingHints = 8;
int g_num_file_mapping_hints;
FileMappingHint g_file_mapping_hints[kMaxFileMappingHints];
// Protects g_file_mapping_hints.
ABSL_CONST_INIT absl::base_internal::SpinLock g_file_mapping_mu(
    absl::kConstInit, absl::base_internal::SCHEDULE_KERNEL_ONLY);

// Async-signal-safe function to zero a buffer.
// memset() is not guaranteed to be async-signal-safe.
static void SafeMemZero(void* p, size_t size) {
  unsigned char *c = static_cast<unsigned char *>(p);
  while (size--) {
    *c++ = 0;
  }
}

struct ObjFile {
  ObjFile()
      : filename(nullptr),
        start_addr(nullptr),
        end_addr(nullptr),
        offset(0),
        fd(-1),
        elf_type(-1) {
    SafeMemZero(&elf_header, sizeof(elf_header));
    SafeMemZero(&phdr[0], sizeof(phdr));
  }

  char *filename;
  const void *start_addr;
  const void *end_addr;
  uint64_t offset;

  // The following fields are initialized on the first access to the
  // object file.
  int fd;
  int elf_type;
  ElfW(Ehdr) elf_header;

  // PT_LOAD program header describing executable code.
  // Normally we expect just one, but SWIFT binaries have two.
  // CUDA binaries have 3 (see cr/473913254 description).
  std::array<ElfW(Phdr), 4> phdr;
};

// Build 4-way associative cache for symbols. Within each cache line, symbols
// are replaced in LRU order.
enum {
  ASSOCIATIVITY = 4,
};
struct SymbolCacheLine {
  const void *pc[ASSOCIATIVITY];
  char *name[ASSOCIATIVITY];

  // age[i] is incremented when a line is accessed. it's reset to zero if the
  // i'th entry is read.
  uint32_t age[ASSOCIATIVITY];
};

// ---------------------------------------------------------------
// An async-signal-safe arena for LowLevelAlloc
static std::atomic<base_internal::LowLevelAlloc::Arena *> g_sig_safe_arena;

static base_internal::LowLevelAlloc::Arena *SigSafeArena() {
  return g_sig_safe_arena.load(std::memory_order_acquire);
}

static void InitSigSafeArena() {
  if (SigSafeArena() == nullptr) {
    base_internal::LowLevelAlloc::Arena *new_arena =
        base_internal::LowLevelAlloc::NewArena(
            base_internal::LowLevelAlloc::kAsyncSignalSafe);
    base_internal::LowLevelAlloc::Arena *old_value = nullptr;
    if (!g_sig_safe_arena.compare_exchange_strong(old_value, new_arena,
                                                  std::memory_order_release,
                                                  std::memory_order_relaxed)) {
      // We lost a race to allocate an arena; deallocate.
      base_internal::LowLevelAlloc::DeleteArena(new_arena);
    }
  }
}

// ---------------------------------------------------------------
// An AddrMap is a vector of ObjFile, using SigSafeArena() for allocation.

class AddrMap {
 public:
  AddrMap() : size_(0), allocated_(0), obj_(nullptr) {}
  ~AddrMap() { base_internal::LowLevelAlloc::Free(obj_); }
  size_t Size() const { return size_; }
  ObjFile *At(size_t i) { return &obj_[i]; }
  ObjFile *Add();
  void Clear();

 private:
  size_t size_;       // count of valid elements (<= allocated_)
  size_t allocated_;  // count of allocated elements
  ObjFile *obj_;      // array of allocated_ elements
  AddrMap(const AddrMap &) = delete;
  AddrMap &operator=(const AddrMap &) = delete;
};

void AddrMap::Clear() {
  for (size_t i = 0; i != size_; i++) {
    At(i)->~ObjFile();
  }
  size_ = 0;
}

ObjFile *AddrMap::Add() {
  if (size_ == allocated_) {
    size_t new_allocated = allocated_ * 2 + 50;
    ObjFile *new_obj_ =
        static_cast<ObjFile *>(base_internal::LowLevelAlloc::AllocWithArena(
            new_allocated * sizeof(*new_obj_), SigSafeArena()));
    if (obj_) {
      memcpy(new_obj_, obj_, allocated_ * sizeof(*new_obj_));
      base_internal::LowLevelAlloc::Free(obj_);
    }
    obj_ = new_obj_;
    allocated_ = new_allocated;
  }
  return new (&obj_[size_++]) ObjFile;
}

class CachingFile {
 public:
  // Setup reader for fd that uses buf[0, buf_size-1] as a cache.
  CachingFile(int fd, char *buf, size_t buf_size)
      : fd_(fd),
        cache_(buf),
        cache_size_(buf_size),
        cache_start_(0),
        cache_limit_(0) {}

  int fd() const { return fd_; }
  ssize_t ReadFromOffset(void *buf, size_t count, off_t offset);
  bool ReadFromOffsetExact(void *buf, size_t count, off_t offset);

 private:
  // Bytes [cache_start_, cache_limit_-1] from fd_ are stored in
  // a prefix of cache_[0, cache_size_-1].
  int fd_;
  char *cache_;
  size_t cache_size_;
  off_t cache_start_;
  off_t cache_limit_;
};

// ---------------------------------------------------------------

enum FindSymbolResult { SYMBOL_NOT_FOUND = 1, SYMBOL_TRUNCATED, SYMBOL_FOUND };

class Symbolizer {
 public:
  Symbolizer();
  ~Symbolizer();
  const char *GetSymbol(const void *const pc);

 private:
  char *CopyString(const char *s) {
    size_t len = strlen(s);
    char *dst = static_cast<char *>(
        base_internal::LowLevelAlloc::AllocWithArena(len + 1, SigSafeArena()));
    ABSL_RAW_CHECK(dst != nullptr, "out of memory");
    memcpy(dst, s, len + 1);
    return dst;
  }
  ObjFile *FindObjFile(const void *const start,
                       size_t size) ABSL_ATTRIBUTE_NOINLINE;
  static bool RegisterObjFile(const char *filename,
                              const void *const start_addr,
                              const void *const end_addr, uint64_t offset,
                              void *arg);
  SymbolCacheLine *GetCacheLine(const void *const pc);
  const char *FindSymbolInCache(const void *const pc);
  const char *InsertSymbolInCache(const void *const pc, const char *name);
  void AgeSymbols(SymbolCacheLine *line);
  void ClearAddrMap();
  FindSymbolResult GetSymbolFromObjectFile(const ObjFile &obj,
                                           const void *const pc,
                                           const ptrdiff_t relocation,
                                           char *out, size_t out_size,
                                           char *tmp_buf, size_t tmp_buf_size);
  const char *GetUncachedSymbol(const void *pc);

  enum {
    SYMBOL_BUF_SIZE = 3072,
    TMP_BUF_SIZE = 1024,
    SYMBOL_CACHE_LINES = 128,
    FILE_CACHE_SIZE = 8192,
  };

  AddrMap addr_map_;

  bool ok_;
  bool addr_map_read_;

  char symbol_buf_[SYMBOL_BUF_SIZE];
  char file_cache_[FILE_CACHE_SIZE];

  // tmp_buf_ will be used to store arrays of ElfW(Shdr) and ElfW(Sym)
  // so we ensure that tmp_buf_ is properly aligned to store either.
  alignas(16) char tmp_buf_[TMP_BUF_SIZE];
  static_assert(alignof(ElfW(Shdr)) <= 16,
                "alignment of tmp buf too small for Shdr");
  static_assert(alignof(ElfW(Sym)) <= 16,
                "alignment of tmp buf too small for Sym");

  SymbolCacheLine symbol_cache_[SYMBOL_CACHE_LINES];
};

static std::atomic<Symbolizer *> g_cached_symbolizer;

}  // namespace

static size_t SymbolizerSize() {
#if defined(__wasm__) || defined(__asmjs__)
  auto pagesize = static_cast<size_t>(getpagesize());
#else
  auto pagesize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
  return ((sizeof(Symbolizer) - 1) / pagesize + 1) * pagesize;
}

// Return (and set null) g_cached_symbolized_state if it is not null.
// Otherwise return a new symbolizer.
static Symbolizer *AllocateSymbolizer() {
  InitSigSafeArena();
  Symbolizer *symbolizer =
      g_cached_symbolizer.exchange(nullptr, std::memory_order_acquire);
  if (symbolizer != nullptr) {
    return symbolizer;
  }
  return new (base_internal::LowLevelAlloc::AllocWithArena(
      SymbolizerSize(), SigSafeArena())) Symbolizer();
}

// Set g_cached_symbolize_state to s if it is null, otherwise
// delete s.
static void FreeSymbolizer(Symbolizer *s) {
  Symbolizer *old_cached_symbolizer = nullptr;
  if (!g_cached_symbolizer.compare_exchange_strong(old_cached_symbolizer, s,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {
    s->~Symbolizer();
    base_internal::LowLevelAlloc::Free(s);
  }
}

Symbolizer::Symbolizer() : ok_(true), addr_map_read_(false) {
  for (SymbolCacheLine &symbol_cache_line : symbol_cache_) {
    for (size_t j = 0; j < ABSL_ARRAYSIZE(symbol_cache_line.name); ++j) {
      symbol_cache_line.pc[j] = nullptr;
      symbol_cache_line.name[j] = nullptr;
      symbol_cache_line.age[j] = 0;
    }
  }
}

Symbolizer::~Symbolizer() {
  for (SymbolCacheLine &symbol_cache_line : symbol_cache_) {
    for (char *s : symbol_cache_line.name) {
      base_internal::LowLevelAlloc::Free(s);
    }
  }
  ClearAddrMap();
}

// We don't use assert() since it's not guaranteed to be
// async-signal-safe.  Instead we define a minimal assertion
// macro. So far, we don't need pretty printing for __FILE__, etc.
#define SAFE_ASSERT(expr) ((expr) ? static_cast<void>(0) : abort())

// Read up to "count" bytes from file descriptor "fd" into the buffer
// starting at "buf" while handling short reads and EINTR.  On
// success, return the number of bytes read.  Otherwise, return -1.
static ssize_t ReadPersistent(int fd, void *buf, size_t count) {
  SAFE_ASSERT(fd >= 0);
  SAFE_ASSERT(count <= SSIZE_MAX);
  char *buf0 = reinterpret_cast<char *>(buf);
  size_t num_bytes = 0;
  while (num_bytes < count) {
    ssize_t len;
    NO_INTR(len = read(fd, buf0 + num_bytes, count - num_bytes));
    if (len < 0) {  // There was an error other than EINTR.
      ABSL_RAW_LOG(WARNING, "read failed: errno=%d", errno);
      return -1;
    }
    if (len == 0) {  // Reached EOF.
      break;
    }
    num_bytes += static_cast<size_t>(len);
  }
  SAFE_ASSERT(num_bytes <= count);
  return static_cast<ssize_t>(num_bytes);
}

// Read up to "count" bytes from "offset" into the buffer starting at "buf",
// while handling short reads and EINTR.  On success, return the number of bytes
// read.  Otherwise, return -1.
ssize_t CachingFile::ReadFromOffset(void *buf, size_t count, off_t offset) {
  char *dst = static_cast<char *>(buf);
  size_t read = 0;
  while (read < count) {
    // Look in cache first.
    if (offset >= cache_start_ && offset < cache_limit_) {
      const char *hit_start = &cache_[offset - cache_start_];
      const size_t n =
          std::min(count - read, static_cast<size_t>(cache_limit_ - offset));
      memcpy(dst, hit_start, n);
      dst += n;
      read += static_cast<size_t>(n);
      offset += static_cast<off_t>(n);
      continue;
    }

    cache_start_ = 0;
    cache_limit_ = 0;
    ssize_t n = pread(fd_, cache_, cache_size_, offset);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ABSL_RAW_LOG(WARNING, "read failed: errno=%d", errno);
      return -1;
    }
    if (n == 0) {  // Reached EOF.
      break;
    }

    cache_start_ = offset;
    cache_limit_ = offset + static_cast<off_t>(n);
    // Next iteration will copy from cache into dst.
  }
  return static_cast<ssize_t>(read);
}

// Try reading exactly "count" bytes from "offset" bytes into the buffer
// starting at "buf" while handling short reads and EINTR.  On success, return
// true. Otherwise, return false.
bool CachingFile::ReadFromOffsetExact(void *buf, size_t count, off_t offset) {
  ssize_t len = ReadFromOffset(buf, count, offset);
  return len >= 0 && static_cast<size_t>(len) == count;
}

// Returns elf_header.e_type if the file pointed by fd is an ELF binary.
static int FileGetElfType(CachingFile *file) {
  ElfW(Ehdr) elf_header;
  if (!file->ReadFromOffsetExact(&elf_header, sizeof(elf_header), 0)) {
    return -1;
  }
  if (memcmp(elf_header.e_ident, ELFMAG, SELFMAG) != 0) {
    return -1;
  }
  return elf_header.e_type;
}

// Read the section headers in the given ELF binary, and if a section
// of the specified type is found, set the output to this section header
// and return true.  Otherwise, return false.
// To keep stack consumption low, we would like this function to not get
// inlined.
static ABSL_ATTRIBUTE_NOINLINE bool GetSectionHeaderByType(
    CachingFile *file, ElfW(Half) sh_num, const off_t sh_offset,
    ElfW(Word) type, ElfW(Shdr) * out, char *tmp_buf, size_t tmp_buf_size) {
  ElfW(Shdr) *buf = reinterpret_cast<ElfW(Shdr) *>(tmp_buf);
  const size_t buf_entries = tmp_buf_size / sizeof(buf[0]);
  const size_t buf_bytes = buf_entries * sizeof(buf[0]);

  for (size_t i = 0; static_cast<int>(i) < sh_num;) {
    const size_t num_bytes_left =
        (static_cast<size_t>(sh_num) - i) * sizeof(buf[0]);
    const size_t num_bytes_to_read =
        (buf_bytes > num_bytes_left) ? num_bytes_left : buf_bytes;
    const off_t offset = sh_offset + static_cast<off_t>(i * sizeof(buf[0]));
    const ssize_t len = file->ReadFromOffset(buf, num_bytes_to_read, offset);
    if (len < 0) {
      ABSL_RAW_LOG(
          WARNING,
          "Reading %zu bytes from offset %ju returned %zd which is negative.",
          num_bytes_to_read, static_cast<intmax_t>(offset), len);
      return false;
    }
    if (static_cast<size_t>(len) % sizeof(buf[0]) != 0) {
      ABSL_RAW_LOG(
          WARNING,
          "Reading %zu bytes from offset %jd returned %zd which is not a "
          "multiple of %zu.",
          num_bytes_to_read, static_cast<intmax_t>(offset), len,
          sizeof(buf[0]));
      return false;
    }
    const size_t num_headers_in_buf = static_cast<size_t>(len) / sizeof(buf[0]);
    SAFE_ASSERT(num_headers_in_buf <= buf_entries);
    for (size_t j = 0; j < num_headers_in_buf; ++j) {
      if (buf[j].sh_type == type) {
        *out = buf[j];
        return true;
      }
    }
    i += num_headers_in_buf;
  }
  return false;
}

// There is no particular reason to limit section name to 63 characters,
// but there has (as yet) been no need for anything longer either.
const int kMaxSectionNameLen = 64;

// Small cache to use for miscellaneous file reads.
const int kSmallFileCacheSize = 100;

bool ForEachSection(int fd,
                    const std::function<bool(absl::string_view name,
                                             const ElfW(Shdr) &)> &callback) {
  char buf[kSmallFileCacheSize];
  CachingFile file(fd, buf, sizeof(buf));

  ElfW(Ehdr) elf_header;
  if (!file.ReadFromOffsetExact(&elf_header, sizeof(elf_header), 0)) {
    return false;
  }

  // Technically it can be larger, but in practice this never happens.
  if (elf_header.e_shentsize != sizeof(ElfW(Shdr))) {
    return false;
  }

  ElfW(Shdr) shstrtab;
  off_t shstrtab_offset = static_cast<off_t>(elf_header.e_shoff) +
                          elf_header.e_shentsize * elf_header.e_shstrndx;
  if (!file.ReadFromOffsetExact(&shstrtab, sizeof(shstrtab), shstrtab_offset)) {
    return false;
  }

  for (int i = 0; i < elf_header.e_shnum; ++i) {
    ElfW(Shdr) out;
    off_t section_header_offset =
        static_cast<off_t>(elf_header.e_shoff) + elf_header.e_shentsize * i;
    if (!file.ReadFromOffsetExact(&out, sizeof(out), section_header_offset)) {
      return false;
    }
    off_t name_offset = static_cast<off_t>(shstrtab.sh_offset) + out.sh_name;
    char header_name[kMaxSectionNameLen];
    ssize_t n_read =
        file.ReadFromOffset(&header_name, kMaxSectionNameLen, name_offset);
    if (n_read < 0) {
      return false;
    } else if (n_read > kMaxSectionNameLen) {
      // Long read?
      return false;
    }

    absl::string_view name(header_name,
                           strnlen(header_name, static_cast<size_t>(n_read)));
    if (!callback(name, out)) {
      break;
    }
  }
  return true;
}

// name_len should include terminating '\0'.
bool GetSectionHeaderByName(int fd, const char *name, size_t name_len,
                            ElfW(Shdr) * out) {
  char header_name[kMaxSectionNameLen];
  if (sizeof(header_name) < name_len) {
    ABSL_RAW_LOG(WARNING,
                 "Section name '%s' is too long (%zu); "
                 "section will not be found (even if present).",
                 name, name_len);
    // No point in even trying.
    return false;
  }

  char buf[kSmallFileCacheSize];
  CachingFile file(fd, buf, sizeof(buf));
  ElfW(Ehdr) elf_header;
  if (!file.ReadFromOffsetExact(&elf_header, sizeof(elf_header), 0)) {
    return false;
  }

  // Technically it can be larger, but in practice this never happens.
  if (elf_header.e_shentsize != sizeof(ElfW(Shdr))) {
    return false;
  }

  ElfW(Shdr) shstrtab;
  off_t shstrtab_offset = static_cast<off_t>(elf_header.e_shoff) +
                          elf_header.e_shentsize * elf_header.e_shstrndx;
  if (!file.ReadFromOffsetExact(&shstrtab, sizeof(shstrtab), shstrtab_offset)) {
    return false;
  }

  for (int i = 0; i < elf_header.e_shnum; ++i) {
    off_t section_header_offset =
        static_cast<off_t>(elf_header.e_shoff) + elf_header.e_shentsize * i;
    if (!file.ReadFromOffsetExact(out, sizeof(*out), section_header_offset)) {
      return false;
    }
    off_t name_offset = static_cast<off_t>(shstrtab.sh_offset) + out->sh_name;
    ssize_t n_read = file.ReadFromOffset(&header_name, name_len, name_offset);
    if (n_read < 0) {
      return false;
    } else if (static_cast<size_t>(n_read) != name_len) {
      // Short read -- name could be at end of file.
      continue;
    }
    if (memcmp(header_name, name, name_len) == 0) {
      return true;
    }
  }
  return false;
}

// Compare symbols at in the same address.
// Return true if we should pick symbol1.
static bool ShouldPickFirstSymbol(const ElfW(Sym) & symbol1,
                                  const ElfW(Sym) & symbol2) {
  // If one of the symbols is weak and the other is not, pick the one
  // this is not a weak symbol.
  char bind1 = ELF_ST_BIND(symbol1.st_info);
  char bind2 = ELF_ST_BIND(symbol1.st_info);
  if (bind1 == STB_WEAK && bind2 != STB_WEAK) return false;
  if (bind2 == STB_WEAK && bind1 != STB_WEAK) return true;

  // If one of the symbols has zero size and the other is not, pick the
  // one that has non-zero size.
  if (symbol1.st_size != 0 && symbol2.st_size == 0) {
    return true;
  }
  if (symbol1.st_size == 0 && symbol2.st_size != 0) {
    return false;
  }

  // If one of the symbols has no type and the other is not, pick the
  // one that has a type.
  char type1 = ELF_ST_TYPE(symbol1.st_info);
  char type2 = ELF_ST_TYPE(symbol1.st_info);
  if (type1 != STT_NOTYPE && type2 == STT_NOTYPE) {
    return true;
  }
  if (type1 == STT_NOTYPE && type2 != STT_NOTYPE) {
    return false;
  }

  // Pick the first one, if we still cannot decide.
  return true;
}

// Return true if an address is inside a section.
static bool InSection(const void *address, ptrdiff_t relocation,
                      const ElfW(Shdr) * section) {
  const char *start = reinterpret_cast<const char *>(
      section->sh_addr + static_cast<ElfW(Addr)>(relocation));
  size_t size = static_cast<size_t>(section->sh_size);
  return start <= address && address < (start + size);
}

static const char *ComputeOffset(const char *base, ptrdiff_t offset) {
  // Note: cast to intptr_t to avoid undefined behavior when base evaluates to
  // zero and offset is non-zero.
  return reinterpret_cast<const char *>(reinterpret_cast<intptr_t>(base) +
                                        offset);
}

// Read a symbol table and look for the symbol containing the
// pc. Iterate over symbols in a symbol table and look for the symbol
// containing "pc".  If the symbol is found, and its name fits in
// out_size, the name is written into out and SYMBOL_FOUND is returned.
// If the name does not fit, truncated name is written into out,
// and SYMBOL_TRUNCATED is returned. Out is NUL-terminated.
// If the symbol is not found, SYMBOL_NOT_FOUND is returned;
// To keep stack consumption low, we would like this function to not get
// inlined.
static ABSL_ATTRIBUTE_NOINLINE FindSymbolResult FindSymbol(
    const void *const pc, CachingFile *file, char *out, size_t out_size,
    ptrdiff_t relocation, const ElfW(Shdr) * strtab, const ElfW(Shdr) * symtab,
    const ElfW(Shdr) * opd, char *tmp_buf, size_t tmp_buf_size) {
  if (symtab == nullptr) {
    return SYMBOL_NOT_FOUND;
  }

  // Read multiple symbols at once to save read() calls.
  ElfW(Sym) *buf = reinterpret_cast<ElfW(Sym) *>(tmp_buf);
  const size_t buf_entries = tmp_buf_size / sizeof(buf[0]);

  const size_t num_symbols = symtab->sh_size / symtab->sh_entsize;

  // On platforms using an .opd section (PowerPC & IA64), a function symbol
  // has the address of a function descriptor, which contains the real
  // starting address.  However, we do not always want to use the real
  // starting address because we sometimes want to symbolize a function
  // pointer into the .opd section, e.g. FindSymbol(&foo,...).
  const bool pc_in_opd = kPlatformUsesOPDSections && opd != nullptr &&
                         InSection(pc, relocation, opd);
  const bool deref_function_descriptor_pointer =
      kPlatformUsesOPDSections && opd != nullptr && !pc_in_opd;

  ElfW(Sym) best_match;
  SafeMemZero(&best_match, sizeof(best_match));
  bool found_match = false;
  for (size_t i = 0; i < num_symbols;) {
    off_t offset =
        static_cast<off_t>(symtab->sh_offset + i * symtab->sh_entsize);
    const size_t num_remaining_symbols = num_symbols - i;
    const size_t entries_in_chunk =
        std::min(num_remaining_symbols, buf_entries);
    const size_t bytes_in_chunk = entries_in_chunk * sizeof(buf[0]);
    const ssize_t len = file->ReadFromOffset(buf, bytes_in_chunk, offset);
    SAFE_ASSERT(len >= 0);
    SAFE_ASSERT(static_cast<size_t>(len) % sizeof(buf[0]) == 0);
    const size_t num_symbols_in_buf = static_cast<size_t>(len) / sizeof(buf[0]);
    SAFE_ASSERT(num_symbols_in_buf <= entries_in_chunk);
    for (size_t j = 0; j < num_symbols_in_buf; ++j) {
      const ElfW(Sym) &symbol = buf[j];

      // For a DSO, a symbol address is relocated by the loading address.
      // We keep the original address for opd redirection below.
      const char *const original_start_address =
          reinterpret_cast<const char *>(symbol.st_value);
      const char *start_address =
          ComputeOffset(original_start_address, relocation);

#ifdef __arm__
      // ARM functions are always aligned to multiples of two bytes; the
      // lowest-order bit in start_address is ignored by the CPU and indicates
      // whether the function contains ARM (0) or Thumb (1) code. We don't care
      // about what encoding is being used; we just want the real start address
      // of the function.
      start_address = reinterpret_cast<const char *>(
          reinterpret_cast<uintptr_t>(start_address) & ~1u);
#endif

      if (deref_function_descriptor_pointer &&
          InSection(original_start_address, /*relocation=*/0, opd)) {
        // The opd section is mapped into memory.  Just dereference
        // start_address to get the first double word, which points to the
        // function entry.
        start_address = *reinterpret_cast<const char *const *>(start_address);
      }

      // If pc is inside the .opd section, it points to a function descriptor.
      const size_t size = pc_in_opd ? kFunctionDescriptorSize : symbol.st_size;
      const void *const end_address =
          ComputeOffset(start_address, static_cast<ptrdiff_t>(size));
      if (symbol.st_value != 0 &&  // Skip null value symbols.
          symbol.st_shndx != 0 &&  // Skip undefined symbols.
#ifdef STT_TLS
          ELF_ST_TYPE(symbol.st_info) != STT_TLS &&  // Skip thread-local data.
#endif                                               // STT_TLS
          ((start_address <= pc && pc < end_address) ||
           (start_address == pc && pc == end_address))) {
        if (!found_match || ShouldPickFirstSymbol(symbol, best_match)) {
          found_match = true;
          best_match = symbol;
        }
      }
    }
    i += num_symbols_in_buf;
  }

  if (found_match) {
    const off_t off =
        static_cast<off_t>(strtab->sh_offset) + best_match.st_name;
    const ssize_t n_read = file->ReadFromOffset(out, out_size, off);
    if (n_read <= 0) {
      // This should never happen.
      ABSL_RAW_LOG(WARNING,
                   "Unable to read from fd %d at offset %lld: n_read = %zd",
                   file->fd(), static_cast<long long>(off), n_read);
      return SYMBOL_NOT_FOUND;
    }
    ABSL_RAW_CHECK(static_cast<size_t>(n_read) <= out_size,
                   "ReadFromOffset read too much data.");

    // strtab->sh_offset points into .strtab-like section that contains
    // NUL-terminated strings: '\0foo\0barbaz\0...".
    //
    // sh_offset+st_name points to the start of symbol name, but we don't know
    // how long the symbol is, so we try to read as much as we have space for,
    // and usually over-read (i.e. there is a NUL somewhere before n_read).
    if (memchr(out, '\0', static_cast<size_t>(n_read)) == nullptr) {
      // Either out_size was too small (n_read == out_size and no NUL), or
      // we tried to read past the EOF (n_read < out_size) and .strtab is
      // corrupt (missing terminating NUL; should never happen for valid ELF).
      out[n_read - 1] = '\0';
      return SYMBOL_TRUNCATED;
    }
    return SYMBOL_FOUND;
  }

  return SYMBOL_NOT_FOUND;
}

// Get the symbol name of "pc" from the file pointed by "fd".  Process
// both regular and dynamic symbol tables if necessary.
// See FindSymbol() comment for description of return value.
FindSymbolResult Symbolizer::GetSymbolFromObjectFile(
    const ObjFile &obj, const void *const pc, const ptrdiff_t relocation,
    char *out, size_t out_size, char *tmp_buf, size_t tmp_buf_size) {
  ElfW(Shdr) symtab;
  ElfW(Shdr) strtab;
  ElfW(Shdr) opd;
  ElfW(Shdr) *opd_ptr = nullptr;

  // On platforms using an .opd sections for function descriptor, read
  // the section header.  The .opd section is in data segment and should be
  // loaded but we check that it is mapped just to be extra careful.
  if (kPlatformUsesOPDSections) {
    if (GetSectionHeaderByName(obj.fd, kOpdSectionName,
                               sizeof(kOpdSectionName) - 1, &opd) &&
        FindObjFile(reinterpret_cast<const char *>(opd.sh_addr) + relocation,
                    opd.sh_size) != nullptr) {
      opd_ptr = &opd;
    } else {
      return SYMBOL_NOT_FOUND;
    }
  }

  CachingFile file(obj.fd, file_cache_, sizeof(file_cache_));

  // Consult a regular symbol table, then fall back to the dynamic symbol table.
  for (const auto symbol_table_type : {SHT_SYMTAB, SHT_DYNSYM}) {
    if (!GetSectionHeaderByType(&file, obj.elf_header.e_shnum,
                                static_cast<off_t>(obj.elf_header.e_shoff),
                                static_cast<ElfW(Word)>(symbol_table_type),
                                &symtab, tmp_buf, tmp_buf_size)) {
      continue;
    }
    if (!file.ReadFromOffsetExact(
            &strtab, sizeof(strtab),
            static_cast<off_t>(obj.elf_header.e_shoff +
                               symtab.sh_link * sizeof(symtab)))) {
      continue;
    }
    const FindSymbolResult rc =
        FindSymbol(pc, &file, out, out_size, relocation, &strtab, &symtab,
                   opd_ptr, tmp_buf, tmp_buf_size);
    if (rc != SYMBOL_NOT_FOUND) {
      return rc;
    }
  }

  return SYMBOL_NOT_FOUND;
}

namespace {
// Thin wrapper around a file descriptor so that the file descriptor
// gets closed for sure.
class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) : fd_(fd) {}
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;

  ~FileDescriptor() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  int get() const { return fd_; }

 private:
  const int fd_;
};

// Helper class for reading lines from file.
//
// Note: we don't use ProcMapsIterator since the object is big (it has
// a 5k array member) and uses async-unsafe functions such as sscanf()
// and snprintf().
class LineReader {
 public:
  explicit LineReader(int fd, char *buf, size_t buf_len)
      : fd_(fd),
        buf_len_(buf_len),
        buf_(buf),
        bol_(buf),
        eol_(buf),
        eod_(buf) {}

  LineReader(const LineReader &) = delete;
  LineReader &operator=(const LineReader &) = delete;

  // Read '\n'-terminated line from file.  On success, modify "bol"
  // and "eol", then return true.  Otherwise, return false.
  //
  // Note: if the last line doesn't end with '\n', the line will be
  // dropped.  It's an intentional behavior to make the code simple.
  bool ReadLine(const char **bol, const char **eol) {
    if (BufferIsEmpty()) {  // First time.
      const ssize_t num_bytes = ReadPersistent(fd_, buf_, buf_len_);
      if (num_bytes <= 0) {  // EOF or error.
        return false;
      }
      eod_ = buf_ + num_bytes;
      bol_ = buf_;
    } else {
      bol_ = eol_ + 1;            // Advance to the next line in the buffer.
      SAFE_ASSERT(bol_ <= eod_);  // "bol_" can point to "eod_".
      if (!HasCompleteLine()) {
        const auto incomplete_line_length = static_cast<size_t>(eod_ - bol_);
        // Move the trailing incomplete line to the beginning.
        memmove(buf_, bol_, incomplete_line_length);
        // Read text from file and append it.
        char *const append_pos = buf_ + incomplete_line_length;
        const size_t capacity_left = buf_len_ - incomplete_line_length;
        const ssize_t num_bytes =
            ReadPersistent(fd_, append_pos, capacity_left);
        if (num_bytes <= 0) {  // EOF or error.
          return false;
        }
        eod_ = append_pos + num_bytes;
        bol_ = buf_;
      }
    }
    eol_ = FindLineFeed();
    if (eol_ == nullptr) {  // '\n' not found.  Malformed line.
      return false;
    }
    *eol_ = '\0';  // Replace '\n' with '\0'.

    *bol = bol_;
    *eol = eol_;
    return true;
  }

 private:
  char *FindLineFeed() const {
    return reinterpret_cast<char *>(
        memchr(bol_, '\n', static_cast<size_t>(eod_ - bol_)));
  }

  bool BufferIsEmpty() const { return buf_ == eod_; }

  bool HasCompleteLine() const {
    return !BufferIsEmpty() && FindLineFeed() != nullptr;
  }

  const int fd_;
  const size_t buf_len_;
  char *const buf_;
  char *bol_;
  char *eol_;
  const char *eod_;  // End of data in "buf_".
};
}  // namespace

// Place the hex number read from "start" into "*hex".  The pointer to
// the first non-hex character or "end" is returned.
static const char *GetHex(const char *start, const char *end,
                          uint64_t *const value) {
  uint64_t hex = 0;
  const char *p;
  for (p = start; p < end; ++p) {
    int ch = *p;
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
        (ch >= 'a' && ch <= 'f')) {
      hex = (hex << 4) |
            static_cast<uint64_t>(ch < 'A' ? ch - '0' : (ch & 0xF) + 9);
    } else {  // Encountered the first non-hex character.
      break;
    }
  }
  SAFE_ASSERT(p <= end);
  *value = hex;
  return p;
}

static const char *GetHex(const char *start, const char *end,
                          const void **const addr) {
  uint64_t hex = 0;
  const char *p = GetHex(start, end, &hex);
  *addr = reinterpret_cast<void *>(hex);
  return p;
}

// Normally we are only interested in "r?x" maps.
// On the PowerPC, function pointers point to descriptors in the .opd
// section.  The descriptors themselves are not executable code, so
// we need to relax the check below to "r??".
static bool ShouldUseMapping(const char *const flags) {
  return flags[0] == 'r' && (kPlatformUsesOPDSections || flags[2] == 'x');
}

// Read /proc/self/maps and run "callback" for each mmapped file found.  If
// "callback" returns false, stop scanning and return true. Else continue
// scanning /proc/self/maps. Return true if no parse error is found.
static ABSL_ATTRIBUTE_NOINLINE bool ReadAddrMap(
    bool (*callback)(const char *filename, const void *const start_addr,
                     const void *const end_addr, uint64_t offset, void *arg),
    void *arg, void *tmp_buf, size_t tmp_buf_size) {
  // Use /proc/self/task/<pid>/maps instead of /proc/self/maps. The latter
  // requires kernel to stop all threads, and is significantly slower when there
  // are 1000s of threads.
  char maps_path[80];
  snprintf(maps_path, sizeof(maps_path), "/proc/self/task/%d/maps", getpid());

  int maps_fd;
  NO_INTR(maps_fd = open(maps_path, O_RDONLY));
  FileDescriptor wrapped_maps_fd(maps_fd);
  if (wrapped_maps_fd.get() < 0) {
    ABSL_RAW_LOG(WARNING, "%s: errno=%d", maps_path, errno);
    return false;
  }

  // Iterate over maps and look for the map containing the pc.  Then
  // look into the symbol tables inside.
  LineReader reader(wrapped_maps_fd.get(), static_cast<char *>(tmp_buf),
                    tmp_buf_size);
  while (true) {
    const char *cursor;
    const char *eol;
    if (!reader.ReadLine(&cursor, &eol)) {  // EOF or malformed line.
      break;
    }

    const char *line = cursor;
    const void *start_address;
    // Start parsing line in /proc/self/maps.  Here is an example:
    //
    // 08048000-0804c000 r-xp 00000000 08:01 2142121    /bin/cat
    //
    // We want start address (08048000), end address (0804c000), flags
    // (r-xp) and file name (/bin/cat).

    // Read start address.
    cursor = GetHex(cursor, eol, &start_address);
    if (cursor == eol || *cursor != '-') {
      ABSL_RAW_LOG(WARNING, "Corrupt /proc/self/maps line: %s", line);
      return false;
    }
    ++cursor;  // Skip '-'.

    // Read end address.
    const void *end_address;
    cursor = GetHex(cursor, eol, &end_address);
    if (cursor == eol || *cursor != ' ') {
      ABSL_RAW_LOG(WARNING, "Corrupt /proc/self/maps line: %s", line);
      return false;
    }
    ++cursor;  // Skip ' '.

    // Read flags.  Skip flags until we encounter a space or eol.
    const char *const flags_start = cursor;
    while (cursor < eol && *cursor != ' ') {
      ++cursor;
    }
    // We expect at least four letters for flags (ex. "r-xp").
    if (cursor == eol || cursor < flags_start + 4) {
      ABSL_RAW_LOG(WARNING, "Corrupt /proc/self/maps: %s", line);
      return false;
    }

    // Check flags.
    if (!ShouldUseMapping(flags_start)) {
      continue;  // We skip this map.
    }
    ++cursor;  // Skip ' '.

    // Read file offset.
    uint64_t offset;
    cursor = GetHex(cursor, eol, &offset);
    ++cursor;  // Skip ' '.

    // Skip to file name.  "cursor" now points to dev.  We need to skip at least
    // two spaces for dev and inode.
    int num_spaces = 0;
    while (cursor < eol) {
      if (*cursor == ' ') {
        ++num_spaces;
      } else if (num_spaces >= 2) {
        // The first non-space character after  skipping two spaces
        // is the beginning of the file name.
        break;
      }
      ++cursor;
    }

    // Check whether this entry corresponds to our hint table for the true
    // filename.
    bool hinted =
        GetFileMappingHint(&start_address, &end_address, &offset, &cursor);
    if (!hinted && (cursor == eol || cursor[0] == '[')) {
      // not an object file, typically [vdso] or [vsyscall]
      continue;
    }
    if (!callback(cursor, start_address, end_address, offset, arg)) break;
  }
  return true;
}

// Find the objfile mapped in address region containing [addr, addr + len).
ObjFile *Symbolizer::FindObjFile(const void *const addr, size_t len) {
  for (int i = 0; i < 2; ++i) {
    if (!ok_) return nullptr;

    // Read /proc/self/maps if necessary
    if (!addr_map_read_) {
      addr_map_read_ = true;
      if (!ReadAddrMap(RegisterObjFile, this, tmp_buf_, TMP_BUF_SIZE)) {
        ok_ = false;
        return nullptr;
      }
    }

    size_t lo = 0;
    size_t hi = addr_map_.Size();
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      if (addr < addr_map_.At(mid)->end_addr) {
        hi = mid;
      } else {
        lo = mid + 1;
      }
    }
    if (lo != addr_map_.Size()) {
      ObjFile *obj = addr_map_.At(lo);
      SAFE_ASSERT(obj->end_addr > addr);
      if (addr >= obj->start_addr &&
          reinterpret_cast<const char *>(addr) + len <= obj->end_addr)
        return obj;
    }

    // The address mapping may have changed since it was last read.  Retry.
    ClearAddrMap();
  }
  return nullptr;
}

void Symbolizer::ClearAddrMap() {
  for (size_t i = 0; i != addr_map_.Size(); i++) {
    ObjFile *o = addr_map_.At(i);
    base_internal::LowLevelAlloc::Free(o->filename);
    if (o->fd >= 0) {
      close(o->fd);
    }
  }
  addr_map_.Clear();
  addr_map_read_ = false;
}

// Callback for ReadAddrMap to register objfiles in an in-memory table.
bool Symbolizer::RegisterObjFile(const char *filename,
                                 const void *const start_addr,
                                 const void *const end_addr, uint64_t offset,
                                 void *arg) {
  Symbolizer *impl = static_cast<Symbolizer *>(arg);

  // Files are supposed to be added in the increasing address order.  Make
  // sure that's the case.
  size_t addr_map_size = impl->addr_map_.Size();
  if (addr_map_size != 0) {
    ObjFile *old = impl->addr_map_.At(addr_map_size - 1);
    if (old->end_addr > end_addr) {
      ABSL_RAW_LOG(ERROR,
                   "Unsorted addr map entry: 0x%" PRIxPTR ": %s <-> 0x%" PRIxPTR
                   ": %s",
                   reinterpret_cast<uintptr_t>(end_addr), filename,
                   reinterpret_cast<uintptr_t>(old->end_addr), old->filename);
      return true;
    } else if (old->end_addr == end_addr) {
      // The same entry appears twice. This sometimes happens for [vdso].
      if (old->start_addr != start_addr ||
          strcmp(old->filename, filename) != 0) {
        ABSL_RAW_LOG(ERROR,
                     "Duplicate addr 0x%" PRIxPTR ": %s <-> 0x%" PRIxPTR ": %s",
                     reinterpret_cast<uintptr_t>(end_addr), filename,
                     reinterpret_cast<uintptr_t>(old->end_addr), old->filename);
      }
      return true;
    } else if (old->end_addr == start_addr &&
               reinterpret_cast<uintptr_t>(old->start_addr) - old->offset ==
                   reinterpret_cast<uintptr_t>(start_addr) - offset &&
               strcmp(old->filename, filename) == 0) {
      // Two contiguous map entries that span a contiguous region of the file,
      // perhaps because some part of the file was mlock()ed. Combine them.
      old->end_addr = end_addr;
      return true;
    }
  }
  ObjFile *obj = impl->addr_map_.Add();
  obj->filename = impl->CopyString(filename);
  obj->start_addr = start_addr;
  obj->end_addr = end_addr;
  obj->offset = offset;
  obj->elf_type = -1;  // filled on demand
  obj->fd = -1;        // opened on demand
  return true;
}

// This function wraps the Demangle function to provide an interface
// where the input symbol is demangled in-place.
// To keep stack consumption low, we would like this function to not
// get inlined.
static ABSL_ATTRIBUTE_NOINLINE void DemangleInplace(char *out, size_t out_size,
                                                    char *tmp_buf,
                                                    size_t tmp_buf_size) {
  if (Demangle(out, tmp_buf, tmp_buf_size)) {
    // Demangling succeeded. Copy to out if the space allows.
    size_t len = strlen(tmp_buf);
    if (len + 1 <= out_size) {  // +1 for '\0'.
      SAFE_ASSERT(len < tmp_buf_size);
      memmove(out, tmp_buf, len + 1);
    }
  }
}

SymbolCacheLine *Symbolizer::GetCacheLine(const void *const pc) {
  uintptr_t pc0 = reinterpret_cast<uintptr_t>(pc);
  pc0 >>= 3;  // drop the low 3 bits

  // Shuffle bits.
  pc0 ^= (pc0 >> 6) ^ (pc0 >> 12) ^ (pc0 >> 18);
  return &symbol_cache_[pc0 % SYMBOL_CACHE_LINES];
}

void Symbolizer::AgeSymbols(SymbolCacheLine *line) {
  for (uint32_t &age : line->age) {
    ++age;
  }
}

const char *Symbolizer::FindSymbolInCache(const void *const pc) {
  if (pc == nullptr) return nullptr;

  SymbolCacheLine *line = GetCacheLine(pc);
  for (size_t i = 0; i < ABSL_ARRAYSIZE(line->pc); ++i) {
    if (line->pc[i] == pc) {
      AgeSymbols(line);
      line->age[i] = 0;
      return line->name[i];
    }
  }
  return nullptr;
}

const char *Symbolizer::InsertSymbolInCache(const void *const pc,
                                            const char *name) {
  SAFE_ASSERT(pc != nullptr);

  SymbolCacheLine *line = GetCacheLine(pc);
  uint32_t max_age = 0;
  size_t oldest_index = 0;
  bool found_oldest_index = false;
  for (size_t i = 0; i < ABSL_ARRAYSIZE(line->pc); ++i) {
    if (line->pc[i] == nullptr) {
      AgeSymbols(line);
      line->pc[i] = pc;
      line->name[i] = CopyString(name);
      line->age[i] = 0;
      return line->name[i];
    }
    if (line->age[i] >= max_age) {
      max_age = line->age[i];
      oldest_index = i;
      found_oldest_index = true;
    }
  }

  AgeSymbols(line);
  ABSL_RAW_CHECK(found_oldest_index, "Corrupt cache");
  base_internal::LowLevelAlloc::Free(line->name[oldest_index]);
  line->pc[oldest_index] = pc;
  line->name[oldest_index] = CopyString(name);
  line->age[oldest_index] = 0;
  return line->name[oldest_index];
}

static void MaybeOpenFdFromSelfExe(ObjFile *obj) {
  if (memcmp(obj->start_addr, ELFMAG, SELFMAG) != 0) {
    return;
  }
  int fd = open("/proc/self/exe", O_RDONLY);
  if (fd == -1) {
    return;
  }
  // Verify that contents of /proc/self/exe matches in-memory image of
  // the binary. This can fail if the "deleted" binary is in fact not
  // the main executable, or for binaries that have the first PT_LOAD
  // segment smaller than 4K. We do it in four steps so that the
  // buffer is smaller and we don't consume too much stack space.
  const char *mem = reinterpret_cast<const char *>(obj->start_addr);
  for (int i = 0; i < 4; ++i) {
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n != sizeof(buf) || memcmp(buf, mem, sizeof(buf)) != 0) {
      close(fd);
      return;
    }
    mem += sizeof(buf);
  }
  obj->fd = fd;
}

static bool MaybeInitializeObjFile(ObjFile *obj) {
  if (obj->fd < 0) {
    obj->fd = open(obj->filename, O_RDONLY);

    if (obj->fd < 0) {
      // Getting /proc/self/exe here means that we were hinted.
      if (strcmp(obj->filename, "/proc/self/exe") == 0) {
        // /proc/self/exe may be inaccessible (due to setuid, etc.), so try
        // accessing the binary via argv0.
        if (argv0_value != nullptr) {
          obj->fd = open(argv0_value, O_RDONLY);
        }
      } else {
        MaybeOpenFdFromSelfExe(obj);
      }
    }

    if (obj->fd < 0) {
      ABSL_RAW_LOG(WARNING, "%s: open failed: errno=%d", obj->filename, errno);
      return false;
    }

    char buf[kSmallFileCacheSize];
    CachingFile file(obj->fd, buf, sizeof(buf));

    obj->elf_type = FileGetElfType(&file);
    if (obj->elf_type < 0) {
      ABSL_RAW_LOG(WARNING, "%s: wrong elf type: %d", obj->filename,
                   obj->elf_type);
      return false;
    }

    if (!file.ReadFromOffsetExact(&obj->elf_header, sizeof(obj->elf_header),
                                  0)) {
      ABSL_RAW_LOG(WARNING, "%s: failed to read elf header", obj->filename);
      return false;
    }
    const int phnum = obj->elf_header.e_phnum;
    const int phentsize = obj->elf_header.e_phentsize;
    auto phoff = static_cast<off_t>(obj->elf_header.e_phoff);
    size_t num_interesting_load_segments = 0;
    for (int j = 0; j < phnum; j++) {
      ElfW(Phdr) phdr;
      if (!file.ReadFromOffsetExact(&phdr, sizeof(phdr), phoff)) {
        ABSL_RAW_LOG(WARNING, "%s: failed to read program header %d",
                     obj->filename, j);
        return false;
      }
      phoff += phentsize;

#if defined(__powerpc__) && !(_CALL_ELF > 1)
      // On the PowerPC ELF v1 ABI, function pointers actually point to function
      // descriptors. These descriptors are stored in an .opd section, which is
      // mapped read-only. We thus need to look at all readable segments, not
      // just the executable ones.
      constexpr int interesting = PF_R;
#else
      constexpr int interesting = PF_X | PF_R;
#endif

      if (phdr.p_type != PT_LOAD
          || (phdr.p_flags & interesting) != interesting) {
        // Not a LOAD segment, not executable code, and not a function
        // descriptor.
        continue;
      }
      if (num_interesting_load_segments < obj->phdr.size()) {
        memcpy(&obj->phdr[num_interesting_load_segments++], &phdr, sizeof(phdr));
      } else {
        ABSL_RAW_LOG(
            WARNING, "%s: too many interesting LOAD segments: %zu >= %zu",
            obj->filename, num_interesting_load_segments, obj->phdr.size());
        break;
      }
    }
    if (num_interesting_load_segments == 0) {
      // This object has no interesting LOAD segments. That's unexpected.
      ABSL_RAW_LOG(WARNING, "%s: no interesting LOAD segments", obj->filename);
      return false;
    }
  }
  return true;
}

// The implementation of our symbolization routine.  If it
// successfully finds the symbol containing "pc" and obtains the
// symbol name, returns pointer to that symbol. Otherwise, returns nullptr.
// If any symbol decorators have been installed via InstallSymbolDecorator(),
// they are called here as well.
// To keep stack consumption low, we would like this function to not
// get inlined.
const char *Symbolizer::GetUncachedSymbol(const void *pc) {
  ObjFile *const obj = FindObjFile(pc, 1);
  ptrdiff_t relocation = 0;
  int fd = -1;
  if (obj != nullptr) {
    if (MaybeInitializeObjFile(obj)) {
      const size_t start_addr = reinterpret_cast<size_t>(obj->start_addr);
      if (obj->elf_type == ET_DYN && start_addr >= obj->offset) {
        // This object was relocated.
        //
        // For obj->offset > 0, adjust the relocation since a mapping at offset
        // X in the file will have a start address of [true relocation]+X.
        relocation = static_cast<ptrdiff_t>(start_addr - obj->offset);

        // Note: some binaries have multiple LOAD segments that can contain
        // function pointers. We must find the right one.
        ElfW(Phdr) *phdr = nullptr;
        for (size_t j = 0; j < obj->phdr.size(); j++) {
          ElfW(Phdr) &p = obj->phdr[j];
          if (p.p_type != PT_LOAD) {
            // We only expect PT_LOADs. This must be PT_NULL that we didn't
            // write over (i.e. we exhausted all interesting PT_LOADs).
            ABSL_RAW_CHECK(p.p_type == PT_NULL, "unexpected p_type");
            break;
          }
          if (pc < reinterpret_cast<void *>(start_addr + p.p_vaddr + p.p_memsz)) {
            phdr = &p;
            break;
          }
        }
        if (phdr == nullptr) {
          // That's unexpected. Hope for the best.
          ABSL_RAW_LOG(
              WARNING,
              "%s: unable to find LOAD segment for pc: %p, start_addr: %zx",
              obj->filename, pc, start_addr);
        } else {
          // Adjust relocation in case phdr.p_vaddr != 0.
          // This happens for binaries linked with `lld --rosegment`, and for
          // binaries linked with BFD `ld -z separate-code`.
          relocation -= phdr->p_vaddr - phdr->p_offset;
        }
      }

      fd = obj->fd;
      if (GetSymbolFromObjectFile(*obj, pc, relocation, symbol_buf_,
                                  sizeof(symbol_buf_), tmp_buf_,
                                  sizeof(tmp_buf_)) == SYMBOL_FOUND) {
        // Only try to demangle the symbol name if it fit into symbol_buf_.
        DemangleInplace(symbol_buf_, sizeof(symbol_buf_), tmp_buf_,
                        sizeof(tmp_buf_));
      }
    }
  } else {
#if ABSL_HAVE_VDSO_SUPPORT
    VDSOSupport vdso;
    if (vdso.IsPresent()) {
      VDSOSupport::SymbolInfo symbol_info;
      if (vdso.LookupSymbolByAddress(pc, &symbol_info)) {
        // All VDSO symbols are known to be short.
        size_t len = strlen(symbol_info.name);
        ABSL_RAW_CHECK(len + 1 < sizeof(symbol_buf_),
                       "VDSO symbol unexpectedly long");
        memcpy(symbol_buf_, symbol_info.name, len + 1);
      }
    }
#endif
  }

  if (g_decorators_mu.TryLock()) {
    if (g_num_decorators > 0) {
      SymbolDecoratorArgs decorator_args = {
          pc,       relocation,       fd,     symbol_buf_, sizeof(symbol_buf_),
          tmp_buf_, sizeof(tmp_buf_), nullptr};
      for (int i = 0; i < g_num_decorators; ++i) {
        decorator_args.arg = g_decorators[i].arg;
        g_decorators[i].fn(&decorator_args);
      }
    }
    g_decorators_mu.Unlock();
  }
  if (symbol_buf_[0] == '\0') {
    return nullptr;
  }
  symbol_buf_[sizeof(symbol_buf_) - 1] = '\0';  // Paranoia.
  return InsertSymbolInCache(pc, symbol_buf_);
}

const char *Symbolizer::GetSymbol(const void *pc) {
  const char *entry = FindSymbolInCache(pc);
  if (entry != nullptr) {
    return entry;
  }
  symbol_buf_[0] = '\0';

#ifdef __hppa__
  {
    // In some contexts (e.g., return addresses), PA-RISC uses the lowest two
    // bits of the address to indicate the privilege level. Clear those bits
    // before trying to symbolize.
    const auto pc_bits = reinterpret_cast<uintptr_t>(pc);
    const auto address = pc_bits & ~0x3;
    entry = GetUncachedSymbol(reinterpret_cast<const void *>(address));
    if (entry != nullptr) {
      return entry;
    }

    // In some contexts, PA-RISC also uses bit 1 of the address to indicate that
    // this is a cross-DSO function pointer. Such function pointers actually
    // point to a procedure label, a struct whose first 32-bit (pointer) element
    // actually points to the function text. With no symbol found for this
    // address so far, try interpreting it as a cross-DSO function pointer and
    // see how that goes.
    if (pc_bits & 0x2) {
      return GetUncachedSymbol(*reinterpret_cast<const void *const *>(address));
    }

    return nullptr;
  }
#else
  return GetUncachedSymbol(pc);
#endif
}

bool RemoveAllSymbolDecorators(void) {
  if (!g_decorators_mu.TryLock()) {
    // Someone else is using decorators. Get out.
    return false;
  }
  g_num_decorators = 0;
  g_decorators_mu.Unlock();
  return true;
}

bool RemoveSymbolDecorator(int ticket) {
  if (!g_decorators_mu.TryLock()) {
    // Someone else is using decorators. Get out.
    return false;
  }
  for (int i = 0; i < g_num_decorators; ++i) {
    if (g_decorators[i].ticket == ticket) {
      while (i < g_num_decorators - 1) {
        g_decorators[i] = g_decorators[i + 1];
        ++i;
      }
      g_num_decorators = i;
      break;
    }
  }
  g_decorators_mu.Unlock();
  return true;  // Decorator is known to be removed.
}

int InstallSymbolDecorator(SymbolDecorator decorator, void *arg) {
  static int ticket = 0;

  if (!g_decorators_mu.TryLock()) {
    // Someone else is using decorators. Get out.
    return -2;
  }
  int ret = ticket;
  if (g_num_decorators >= kMaxDecorators) {
    ret = -1;
  } else {
    g_decorators[g_num_decorators] = {decorator, arg, ticket++};
    ++g_num_decorators;
  }
  g_decorators_mu.Unlock();
  return ret;
}

bool RegisterFileMappingHint(const void *start, const void *end, uint64_t offset,
                             const char *filename) {
  SAFE_ASSERT(start <= end);
  SAFE_ASSERT(filename != nullptr);

  InitSigSafeArena();

  if (!g_file_mapping_mu.TryLock()) {
    return false;
  }

  bool ret = true;
  if (g_num_file_mapping_hints >= kMaxFileMappingHints) {
    ret = false;
  } else {
    // TODO(ckennelly): Move this into a string copy routine.
    size_t len = strlen(filename);
    char *dst = static_cast<char *>(
        base_internal::LowLevelAlloc::AllocWithArena(len + 1, SigSafeArena()));
    ABSL_RAW_CHECK(dst != nullptr, "out of memory");
    memcpy(dst, filename, len + 1);

    auto &hint = g_file_mapping_hints[g_num_file_mapping_hints++];
    hint.start = start;
    hint.end = end;
    hint.offset = offset;
    hint.filename = dst;
  }

  g_file_mapping_mu.Unlock();
  return ret;
}

bool GetFileMappingHint(const void **start, const void **end, uint64_t *offset,
                        const char **filename) {
  if (!g_file_mapping_mu.TryLock()) {
    return false;
  }
  bool found = false;
  for (int i = 0; i < g_num_file_mapping_hints; i++) {
    if (g_file_mapping_hints[i].start <= *start &&
        *end <= g_file_mapping_hints[i].end) {
      // We assume that the start_address for the mapping is the base
      // address of the ELF section, but when [start_address,end_address) is
      // not strictly equal to [hint.start, hint.end), that assumption is
      // invalid.
      //
      // This uses the hint's start address (even though hint.start is not
      // necessarily equal to start_address) to ensure the correct
      // relocation is computed later.
      *start = g_file_mapping_hints[i].start;
      *end = g_file_mapping_hints[i].end;
      *offset = g_file_mapping_hints[i].offset;
      *filename = g_file_mapping_hints[i].filename;
      found = true;
      break;
    }
  }
  g_file_mapping_mu.Unlock();
  return found;
}

}  // namespace debugging_internal

bool Symbolize(const void *pc, char *out, int out_size) {
  // Symbolization is very slow under tsan.
  ABSL_ANNOTATE_IGNORE_READS_AND_WRITES_BEGIN();
  SAFE_ASSERT(out_size >= 0);
  debugging_internal::Symbolizer *s = debugging_internal::AllocateSymbolizer();
  const char *name = s->GetSymbol(pc);
  bool ok = false;
  if (name != nullptr && out_size > 0) {
    strncpy(out, name, static_cast<size_t>(out_size));
    ok = true;
    if (out[static_cast<size_t>(out_size) - 1] != '\0') {
      // strncpy() does not '\0' terminate when it truncates.  Do so, with
      // trailing ellipsis.
      static constexpr char kEllipsis[] = "...";
      size_t ellipsis_size =
          std::min(strlen(kEllipsis), static_cast<size_t>(out_size) - 1);
      memcpy(out + static_cast<size_t>(out_size) - ellipsis_size - 1, kEllipsis,
             ellipsis_size);
      out[static_cast<size_t>(out_size) - 1] = '\0';
    }
  }
  debugging_internal::FreeSymbolizer(s);
  ABSL_ANNOTATE_IGNORE_READS_AND_WRITES_END();
  return ok;
}

ABSL_NAMESPACE_END
}  // namespace absl

extern "C" bool AbslInternalGetFileMappingHint(const void **start,
                                               const void **end, uint64_t *offset,
                                               const char **filename) {
  return absl::debugging_internal::GetFileMappingHint(start, end, offset,
                                                      filename);
}
