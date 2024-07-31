#ifndef SRC_COMPILE_CACHE_H_
#define SRC_COMPILE_CACHE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <cinttypes>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include "v8.h"

namespace node {
class Environment;

// TODO(joyeecheung): move it into a CacheHandler class.
enum class CachedCodeType : uint8_t {
  kCommonJS = 0,
  kESM,
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
  // Copy the cache into a new store for V8 to consume. Caller takes
  // ownership.
  v8::ScriptCompiler::CachedData* CopyCache() const;
};

class CompileCacheHandler {
 public:
  explicit CompileCacheHandler(Environment* env);
  bool InitializeDirectory(Environment* env, const std::string& dir);

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

 private:
  void ReadCacheFile(CompileCacheEntry* entry);

  template <typename T>
  void MaybeSaveImpl(CompileCacheEntry* entry,
                     v8::Local<T> func_or_mod,
                     bool rejected);

  template <typename... Args>
  inline void Debug(const char* format, Args&&... args) const;

  static constexpr size_t kCodeSizeOffset = 0;
  static constexpr size_t kCacheSizeOffset = 1;
  static constexpr size_t kCodeHashOffset = 2;
  static constexpr size_t kCacheHashOffset = 3;
  static constexpr size_t kHeaderCount = 4;

  v8::Isolate* isolate_ = nullptr;
  bool is_debug_ = false;

  std::filesystem::path compile_cache_dir_;
  // The compile cache is stored in a directory whose name is the hex string of
  // compiler_cache_key_.
  uint32_t compiler_cache_key_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<CompileCacheEntry>>
      compiler_cache_store_;
};
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_COMPILE_CACHE_H_
