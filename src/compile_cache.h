#ifndef SRC_COMPILE_CACHE_H_
#define SRC_COMPILE_CACHE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cinttypes>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include "v8.h"

namespace node {
class Environment;

#define CACHED_CODE_TYPES(V)                                                   \
  V(kCommonJS, 0)                                                              \
  V(kESM, 1)                                                                   \
  V(kStrippedTypeScript, 2)                                                    \
  V(kTransformedTypeScript, 3)                                                 \
  V(kTransformedTypeScriptWithSourceMaps, 4)

enum class CachedCodeType : uint8_t {
#define V(type, value) type = value,
  CACHED_CODE_TYPES(V)
#undef V
};

struct CompileCacheEntry {
  std::unique_ptr<v8::ScriptCompiler::CachedData> cache{nullptr};
  uint32_t cache_key;
  uint32_t code_hash;
  uint32_t code_size;

  std::string cache_filename;
  std::string source_filename;
  CachedCodeType type;
  bool refreshed = false;
  bool persisted = false;

  // Copy the cache into a new store for V8 to consume. Caller takes
  // ownership.
  v8::ScriptCompiler::CachedData* CopyCache() const;
  const char* type_name() const;
};

#define COMPILE_CACHE_STATUS(V)                                                \
  V(FAILED)          /* Failed to enable the cache */                          \
  V(ENABLED)         /* Was not enabled before, and now enabled. */            \
  V(ALREADY_ENABLED) /* Was already enabled. */                                \
  V(DISABLED)        /* Has been disabled by NODE_DISABLE_COMPILE_CACHE. */

enum class CompileCacheEnableStatus : uint8_t {
#define V(status) status,
  COMPILE_CACHE_STATUS(V)
#undef V
};

struct CompileCacheEnableResult {
  CompileCacheEnableStatus status;
  std::string cache_directory;
  std::string message;  // Set in case of failure.
};

class CompileCacheHandler {
 public:
  explicit CompileCacheHandler(Environment* env);
  CompileCacheEnableResult Enable(Environment* env, const std::string& dir);

  void Persist();

  CompileCacheEntry* GetOrInsert(v8::Local<v8::String> code,
                                 v8::Local<v8::String> filename,
                                 CachedCodeType type);
  void MaybeSave(CompileCacheEntry* entry,
                 v8::Local<v8::Function> func,
                 bool rejected);
  void MaybeSave(CompileCacheEntry* entry,
                 v8::Local<v8::Module> mod,
                 bool rejected);
  void MaybeSave(CompileCacheEntry* entry, std::string_view transpiled);
  std::string_view cache_dir() { return compile_cache_dir_; }

 private:
  void ReadCacheFile(CompileCacheEntry* entry);

  template <typename T>
  void MaybeSaveImpl(CompileCacheEntry* entry,
                     v8::Local<T> func_or_mod,
                     bool rejected);

  template <typename... Args>
  inline void Debug(const char* format, Args&&... args) const;

  static constexpr size_t kMagicNumberOffset = 0;
  static constexpr size_t kCodeSizeOffset = 1;
  static constexpr size_t kCacheSizeOffset = 2;
  static constexpr size_t kCodeHashOffset = 3;
  static constexpr size_t kCacheHashOffset = 4;
  static constexpr size_t kHeaderCount = 5;

  v8::Isolate* isolate_ = nullptr;
  bool is_debug_ = false;

  std::string compile_cache_dir_;
  std::unordered_map<uint32_t, std::unique_ptr<CompileCacheEntry>>
      compiler_cache_store_;
};
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_COMPILE_CACHE_H_
