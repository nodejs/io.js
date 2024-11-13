#include "compile_cache.h"
#include <string>
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node_file.h"
#include "node_internals.h"
#include "node_version.h"
#include "path.h"
#include "util.h"
#include "zlib.h"

#ifdef NODE_IMPLEMENTS_POSIX_CREDENTIALS
#include <unistd.h>  // getuid
#endif

namespace node {
std::string Uint32ToHex(uint32_t crc) {
  std::string str;
  str.reserve(8);

  for (int i = 28; i >= 0; i -= 4) {
    char digit = (crc >> i) & 0xF;
    digit += digit < 10 ? '0' : 'a' - 10;
    str.push_back(digit);
  }

  return str;
}

// TODO(joyeecheung): use other hashes?
uint32_t GetHash(const char* data, size_t size) {
  uLong crc = crc32(0L, Z_NULL, 0);
  return crc32(crc, reinterpret_cast<const Bytef*>(data), size);
}

std::string GetCacheVersionTag() {
  // On platforms where uids are available, use different folders for
  // different users to avoid cache miss due to permission incompatibility.
  // On platforms where uids are not available, bare with the cache miss.
  // This should be fine on Windows, as there local directories tend to be
  // user-specific.
  std::string tag = std::string(NODE_VERSION) + '-' + std::string(NODE_ARCH) +
                    '-' +
                    Uint32ToHex(v8::ScriptCompiler::CachedDataVersionTag());
#ifdef NODE_IMPLEMENTS_POSIX_CREDENTIALS
  tag += '-' + std::to_string(getuid());
#endif
  return tag;
}

uint32_t GetCacheKey(std::string_view filename, CachedCodeType type) {
  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(&type), sizeof(type));
  crc = crc32(
      crc, reinterpret_cast<const Bytef*>(filename.data()), filename.length());
  return crc;
}

template <typename... Args>
inline void CompileCacheHandler::Debug(const char* format,
                                       Args&&... args) const {
  if (is_debug_) [[unlikely]] {
    FPrintF(stderr, format, std::forward<Args>(args)...);
  }
}

v8::ScriptCompiler::CachedData* CompileCacheEntry::CopyCache() const {
  DCHECK_NOT_NULL(cache);
  int cache_size = cache->length;
  uint8_t* data = new uint8_t[cache_size];
  memcpy(data, cache->data, cache_size);
  return new v8::ScriptCompiler::CachedData(
      data, cache_size, v8::ScriptCompiler::CachedData::BufferOwned);
}

// Used for identifying and verifying a file is a compile cache file.
// See comments in CompileCacheHandler::Persist().
constexpr uint32_t kCacheMagicNumber = 0x8adfdbb2;

void CompileCacheHandler::ReadCacheFile(CompileCacheEntry* entry) {
  Debug("[compile cache] reading cache from %s for %s %s...",
        entry->cache_filename,
        entry->type == CachedCodeType::kCommonJS ? "CommonJS" : "ESM",
        entry->source_filename);

  uv_fs_t req;
  auto defer_req_cleanup = OnScopeLeave([&req]() { uv_fs_req_cleanup(&req); });
  const char* path = entry->cache_filename.c_str();
  uv_file file = uv_fs_open(nullptr, &req, path, O_RDONLY, 0, nullptr);
  if (req.result < 0) {
    // req will be cleaned up by scope leave.
    Debug(" %s\n", uv_strerror(req.result));
    return;
  }
  uv_fs_req_cleanup(&req);

  auto defer_close = OnScopeLeave([file]() {
    uv_fs_t close_req;
    CHECK_EQ(0, uv_fs_close(nullptr, &close_req, file, nullptr));
    uv_fs_req_cleanup(&close_req);
  });

  // Read the headers.
  std::vector<uint32_t> headers(kHeaderCount);
  uv_buf_t headers_buf = uv_buf_init(reinterpret_cast<char*>(headers.data()),
                                     kHeaderCount * sizeof(uint32_t));
  const int r = uv_fs_read(nullptr, &req, file, &headers_buf, 1, 0, nullptr);
  if (r != static_cast<int>(headers_buf.len)) {
    Debug("reading header failed, bytes read %d", r);
    if (req.result < 0 && is_debug_) {
      Debug(", %s", uv_strerror(req.result));
    }
    Debug("\n");
    return;
  }

  Debug("[%d %d %d %d %d]...",
        headers[kMagicNumberOffset],
        headers[kCodeSizeOffset],
        headers[kCacheSizeOffset],
        headers[kCodeHashOffset],
        headers[kCacheHashOffset]);

  if (headers[kMagicNumberOffset] != kCacheMagicNumber) {
    Debug("magic number mismatch: expected %d, actual %d\n",
          kCacheMagicNumber,
          headers[kMagicNumberOffset]);
    return;
  }

  // Check the code size and hash which are already computed.
  if (headers[kCodeSizeOffset] != entry->code_size) {
    Debug("code size mismatch: expected %d, actual %d\n",
          entry->code_size,
          headers[kCodeSizeOffset]);
    return;
  }
  if (headers[kCodeHashOffset] != entry->code_hash) {
    Debug("code hash mismatch: expected %d, actual %d\n",
          entry->code_hash,
          headers[kCodeHashOffset]);
    return;
  }

  // Read the cache, grow the buffer exponentially whenever it fills up.
  size_t offset = headers_buf.len;
  size_t capacity = 4096;  // Initial buffer capacity
  size_t total_read = 0;
  uint8_t* buffer = new uint8_t[capacity];

  while (true) {
    // If there is not enough space to read more data, do a simple
    // realloc here (we don't actually realloc because V8 requires
    // the underlying buffer to be delete[]-able).
    if (total_read == capacity) {
      size_t new_capacity = capacity * 2;
      auto* new_buffer = new uint8_t[new_capacity];
      memcpy(new_buffer, buffer, capacity);
      delete[] buffer;
      buffer = new_buffer;
      capacity = new_capacity;
    }

    uv_buf_t iov = uv_buf_init(reinterpret_cast<char*>(buffer + total_read),
                               capacity - total_read);
    int bytes_read =
        uv_fs_read(nullptr, &req, file, &iov, 1, offset + total_read, nullptr);
    if (req.result < 0) {  // Error.
      // req will be cleaned up by scope leave.
      delete[] buffer;
      Debug(" %s\n", uv_strerror(req.result));
      return;
    }
    uv_fs_req_cleanup(&req);
    if (bytes_read <= 0) {
      break;
    }
    total_read += bytes_read;
  }

  // Check the cache size and hash.
  if (headers[kCacheSizeOffset] != total_read) {
    Debug("cache size mismatch: expected %d, actual %d\n",
          headers[kCacheSizeOffset],
          total_read);
    return;
  }
  uint32_t cache_hash = GetHash(reinterpret_cast<char*>(buffer), total_read);
  if (headers[kCacheHashOffset] != cache_hash) {
    Debug("cache hash mismatch: expected %d, actual %d\n",
          headers[kCacheHashOffset],
          cache_hash);
    return;
  }

  entry->cache.reset(new v8::ScriptCompiler::CachedData(
      buffer, total_read, v8::ScriptCompiler::CachedData::BufferOwned));
  Debug(" success, size=%d\n", total_read);
}

CompileCacheEntry* CompileCacheHandler::GetOrInsert(
    v8::Local<v8::String> code,
    v8::Local<v8::String> filename,
    CachedCodeType type) {
  DCHECK(!compile_cache_dir_.empty());

  Utf8Value filename_utf8(isolate_, filename);
  uint32_t key = GetCacheKey(filename_utf8.ToStringView(), type);

  // TODO(joyeecheung): don't encode this again into UTF8. If we read the
  // UTF8 content on disk as raw buffer (from the JS layer, while watching out
  // for monkey patching), we can just hash it directly.
  Utf8Value code_utf8(isolate_, code);
  uint32_t code_hash = GetHash(code_utf8.out(), code_utf8.length());
  auto loaded = compiler_cache_store_.find(key);

  // TODO(joyeecheung): let V8's in-isolate compilation cache take precedence.
  if (loaded != compiler_cache_store_.end() &&
      loaded->second->code_hash == code_hash) {
    return loaded->second.get();
  }

  // If the code hash mismatches, the code has changed, discard the stale entry
  // and create a new one.
  auto emplaced =
      compiler_cache_store_.emplace(key, std::make_unique<CompileCacheEntry>());
  auto* result = emplaced.first->second.get();

  result->code_hash = code_hash;
  result->code_size = code_utf8.length();
  result->cache_key = key;
  result->cache_filename =
      compile_cache_dir_ + kPathSeparator + Uint32ToHex(key);
  result->source_filename = filename_utf8.ToString();
  result->cache = nullptr;
  result->type = type;

  // TODO(joyeecheung): if we fail enough times, stop trying for any future
  // files.
  ReadCacheFile(result);

  return result;
}

v8::ScriptCompiler::CachedData* SerializeCodeCache(
    v8::Local<v8::Function> func) {
  return v8::ScriptCompiler::CreateCodeCacheForFunction(func);
}

v8::ScriptCompiler::CachedData* SerializeCodeCache(v8::Local<v8::Module> mod) {
  return v8::ScriptCompiler::CreateCodeCache(mod->GetUnboundModuleScript());
}

template <typename T>
void CompileCacheHandler::MaybeSaveImpl(CompileCacheEntry* entry,
                                        v8::Local<T> func_or_mod,
                                        bool rejected) {
  DCHECK_NOT_NULL(entry);
  Debug("[compile cache] cache for %s was %s, ",
        entry->source_filename,
        rejected                    ? "rejected"
        : (entry->cache == nullptr) ? "not initialized"
                                    : "accepted");
  if (entry->cache != nullptr && !rejected) {  // accepted
    Debug("keeping the in-memory entry\n");
    return;
  }
  Debug("%s the in-memory entry\n",
        entry->cache == nullptr ? "initializing" : "refreshing");

  v8::ScriptCompiler::CachedData* data = SerializeCodeCache(func_or_mod);
  DCHECK_EQ(data->buffer_policy, v8::ScriptCompiler::CachedData::BufferOwned);
  entry->refreshed = true;
  entry->cache.reset(data);
}

void CompileCacheHandler::MaybeSave(CompileCacheEntry* entry,
                                    v8::Local<v8::Module> mod,
                                    bool rejected) {
  DCHECK(mod->IsSourceTextModule());
  MaybeSaveImpl(entry, mod, rejected);
}

void CompileCacheHandler::MaybeSave(CompileCacheEntry* entry,
                                    v8::Local<v8::Function> func,
                                    bool rejected) {
  MaybeSaveImpl(entry, func, rejected);
}

/**
 * Persist the compile cache accumulated in memory to disk.
 *
 * To avoid race conditions, the cache file includes hashes of the original
 * source code and the cache content. It's first written to a temporary file
 * before being renamed to the target name.
 *
 * Layout of a cache file:
 *   [uint32_t] magic number
 *   [uint32_t] code size
 *   [uint32_t] code hash
 *   [uint32_t] cache size
 *   [uint32_t] cache hash
 *   .... compile cache content ....
 */
void CompileCacheHandler::Persist() {
  DCHECK(!compile_cache_dir_.empty());

  // TODO(joyeecheung): do this using a separate event loop to utilize the
  // libuv thread pool and do the file system operations concurrently.
  // TODO(joyeecheung): Currently flushing is triggered by either process
  // shutdown or user requests. In the future we should simply start the
  // writes right after module loading on a separate thread, and this method
  // only blocks until all the pending writes (if any) on the other thread are
  // finished. In that case, the off-thread writes should finish long
  // before any attempt of flushing is made so the method would then only
  // incur a negligible overhead from thread synchronization.
  for (auto& pair : compiler_cache_store_) {
    auto* entry = pair.second.get();
    if (entry->cache == nullptr) {
      Debug("[compile cache] skip %s because the cache was not initialized\n",
            entry->source_filename);
      continue;
    }
    if (entry->refreshed == false) {
      Debug("[compile cache] skip %s because cache was the same\n",
            entry->source_filename);
      continue;
    }
    if (entry->persisted == true) {
      Debug("[compile cache] skip %s because cache was already persisted\n",
            entry->source_filename);
      continue;
    }

    DCHECK_EQ(entry->cache->buffer_policy,
              v8::ScriptCompiler::CachedData::BufferOwned);
    char* cache_ptr =
        reinterpret_cast<char*>(const_cast<uint8_t*>(entry->cache->data));
    uint32_t cache_size = static_cast<uint32_t>(entry->cache->length);
    uint32_t cache_hash = GetHash(cache_ptr, cache_size);

    // Generating headers.
    std::vector<uint32_t> headers(kHeaderCount);
    headers[kMagicNumberOffset] = kCacheMagicNumber;
    headers[kCodeSizeOffset] = entry->code_size;
    headers[kCacheSizeOffset] = cache_size;
    headers[kCodeHashOffset] = entry->code_hash;
    headers[kCacheHashOffset] = cache_hash;

    // Generate the temporary filename.
    // The temporary file should be placed in a location like:
    //
    // $NODE_COMPILE_CACHE_DIR/v23.0.0-pre-arm64-5fad6d45-501/e7f8ef7f.cache.tcqrsK
    //
    // 1. $NODE_COMPILE_CACHE_DIR either comes from the $NODE_COMPILE_CACHE
    // environment
    //    variable or `module.enableCompileCache()`.
    // 2. v23.0.0-pre-arm64-5fad6d45-501 is the sub cache directory and
    //    e7f8ef7f is the hash for the cache (see
    //    CompileCacheHandler::Enable()),
    // 3. tcqrsK is generated by uv_fs_mkstemp() as a temporary identifier.
    uv_fs_t mkstemp_req;
    auto cleanup_mkstemp =
        OnScopeLeave([&mkstemp_req]() { uv_fs_req_cleanup(&mkstemp_req); });
    std::string cache_filename_tmp = entry->cache_filename + ".XXXXXX";
    Debug("[compile cache] Creating temporary file for cache of %s...",
          entry->source_filename);
    int err = uv_fs_mkstemp(
        nullptr, &mkstemp_req, cache_filename_tmp.c_str(), nullptr);
    if (err < 0) {
      Debug("failed. %s\n", uv_strerror(err));
      continue;
    }
    Debug(" -> %s\n", mkstemp_req.path);
    Debug("[compile cache] writing cache for %s to temporary file %s [%d %d %d "
          "%d %d]...",
          entry->source_filename,
          mkstemp_req.path,
          headers[kMagicNumberOffset],
          headers[kCodeSizeOffset],
          headers[kCacheSizeOffset],
          headers[kCodeHashOffset],
          headers[kCacheHashOffset]);

    // Write to the temporary file.
    uv_buf_t headers_buf = uv_buf_init(reinterpret_cast<char*>(headers.data()),
                                       headers.size() * sizeof(uint32_t));
    uv_buf_t data_buf = uv_buf_init(cache_ptr, entry->cache->length);
    uv_buf_t bufs[] = {headers_buf, data_buf};

    uv_fs_t write_req;
    auto cleanup_write =
        OnScopeLeave([&write_req]() { uv_fs_req_cleanup(&write_req); });
    err = uv_fs_write(
        nullptr, &write_req, mkstemp_req.result, bufs, 2, 0, nullptr);
    if (err < 0) {
      Debug("failed: %s\n", uv_strerror(err));
      continue;
    }

    uv_fs_t close_req;
    auto cleanup_close =
        OnScopeLeave([&close_req]() { uv_fs_req_cleanup(&close_req); });
    err = uv_fs_close(nullptr, &close_req, mkstemp_req.result, nullptr);

    if (err < 0) {
      Debug("failed: %s\n", uv_strerror(err));
      continue;
    }

    Debug("success\n");

    // Rename the temporary file to the actual cache file.
    uv_fs_t rename_req;
    auto cleanup_rename =
        OnScopeLeave([&rename_req]() { uv_fs_req_cleanup(&rename_req); });
    std::string cache_filename_final = entry->cache_filename;
    Debug("[compile cache] Renaming %s to %s...",
          mkstemp_req.path,
          cache_filename_final);
    err = uv_fs_rename(nullptr,
                       &rename_req,
                       mkstemp_req.path,
                       cache_filename_final.c_str(),
                       nullptr);
    if (err < 0) {
      Debug("failed: %s\n", uv_strerror(err));
      continue;
    }
    Debug("success\n");
    entry->persisted = true;
  }

  // Clear the map at the end in one go instead of during the iteration to
  // avoid rehashing costs.
  Debug("[compile cache] Clear deserialized cache.\n");
  compiler_cache_store_.clear();
}

CompileCacheHandler::CompileCacheHandler(Environment* env)
    : isolate_(env->isolate()),
      is_debug_(
          env->enabled_debug_list()->enabled(DebugCategory::COMPILE_CACHE)) {}

// Directory structure:
// - Compile cache directory (from NODE_COMPILE_CACHE)
//   - $NODE_VERSION-$ARCH-$CACHE_DATA_VERSION_TAG-$UID
//     - $FILENAME_AND_MODULE_TYPE_HASH.cache: a hash of filename + module type
CompileCacheEnableResult CompileCacheHandler::Enable(Environment* env,
                                                     const std::string& dir) {
  std::string cache_tag = GetCacheVersionTag();
  std::string absolute_cache_dir_base = PathResolve(env, {dir});
  std::string cache_dir_with_tag =
      absolute_cache_dir_base + kPathSeparator + cache_tag;
  CompileCacheEnableResult result;
  Debug("[compile cache] resolved path %s + %s -> %s\n",
        dir,
        cache_tag,
        cache_dir_with_tag);

  if (!env->permission()->is_granted(
          env,
          permission::PermissionScope::kFileSystemWrite,
          cache_dir_with_tag)) [[unlikely]] {
    result.message = "Skipping compile cache because write permission for " +
                     cache_dir_with_tag + " is not granted";
    result.status = CompileCacheEnableStatus::FAILED;
    return result;
  }

  if (!env->permission()->is_granted(
          env,
          permission::PermissionScope::kFileSystemRead,
          cache_dir_with_tag)) [[unlikely]] {
    result.message = "Skipping compile cache because read permission for " +
                     cache_dir_with_tag + " is not granted";
    result.status = CompileCacheEnableStatus::FAILED;
    return result;
  }

  fs::FSReqWrapSync req_wrap;
  int err = fs::MKDirpSync(
      nullptr, &(req_wrap.req), cache_dir_with_tag, 0777, nullptr);
  if (is_debug_) {
    Debug("[compile cache] creating cache directory %s...%s\n",
          cache_dir_with_tag,
          err < 0 ? uv_strerror(err) : "success");
  }
  if (err != 0 && err != UV_EEXIST) {
    result.message =
        "Cannot create cache directory: " + std::string(uv_strerror(err));
    result.status = CompileCacheEnableStatus::FAILED;
    return result;
  }

  result.cache_directory = absolute_cache_dir_base;
  compile_cache_dir_ = cache_dir_with_tag;
  result.status = CompileCacheEnableStatus::ENABLED;
  return result;
}

}  // namespace node
