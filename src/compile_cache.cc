#include "compile_cache.h"
#include "debug_utils-inl.h"
#include "env-inl.h"
#include "node_file.h"
#include "node_internals.h"
#include "node_version.h"
#include "path.h"
#include "zlib.h"

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

uint32_t GetCacheVersionTag() {
  std::string_view node_version(NODE_VERSION);
  uint32_t v8_tag = v8::ScriptCompiler::CachedDataVersionTag();
  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, reinterpret_cast<const Bytef*>(&v8_tag), sizeof(uint32_t));
  crc = crc32(crc,
              reinterpret_cast<const Bytef*>(node_version.data()),
              node_version.size());
  return crc;
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
  if (UNLIKELY(is_debug_)) {
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

  Debug("[%d %d %d %d]...",
        headers[kCodeSizeOffset],
        headers[kCacheSizeOffset],
        headers[kCodeHashOffset],
        headers[kCacheHashOffset]);

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

  auto emplaced =
      compiler_cache_store_.emplace(key, std::make_unique<CompileCacheEntry>());
  auto* result = emplaced.first->second.get();

  result->code_hash = code_hash;
  result->code_size = code_utf8.length();
  result->cache_key = key;
  result->cache_filename =
      (compile_cache_dir_ / Uint32ToHex(result->cache_key)).string();
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

// Layout of a cache file:
// [uint32_t] code size
// [uint32_t] code hash
// [uint32_t] cache size
// [uint32_t] cache hash
// .... compile cache content ....
void CompileCacheHandler::Persist() {
  DCHECK(!compile_cache_dir_.empty());

  // NOTE(joyeecheung): in most circumstances the code caching reading
  // writing logic is lenient enough that it's fine even if someone
  // overwrites the cache (that leads to either size or hash mismatch
  // in subsequent loads and the overwritten cache will be ignored).
  // Also in most use cases users should not change the files on disk
  // too rapidly. Therefore locking is not currently implemented to
  // avoid the cost.
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

    DCHECK_EQ(entry->cache->buffer_policy,
              v8::ScriptCompiler::CachedData::BufferOwned);
    char* cache_ptr =
        reinterpret_cast<char*>(const_cast<uint8_t*>(entry->cache->data));
    uint32_t cache_size = static_cast<uint32_t>(entry->cache->length);
    uint32_t cache_hash = GetHash(cache_ptr, cache_size);

    // Generating headers.
    std::vector<uint32_t> headers(kHeaderCount);
    headers[kCodeSizeOffset] = entry->code_size;
    headers[kCacheSizeOffset] = cache_size;
    headers[kCodeHashOffset] = entry->code_hash;
    headers[kCacheHashOffset] = cache_hash;

    Debug("[compile cache] writing cache for %s in %s [%d %d %d %d]...",
          entry->source_filename,
          entry->cache_filename,
          headers[kCodeSizeOffset],
          headers[kCacheSizeOffset],
          headers[kCodeHashOffset],
          headers[kCacheHashOffset]);

    uv_buf_t headers_buf = uv_buf_init(reinterpret_cast<char*>(headers.data()),
                                       headers.size() * sizeof(uint32_t));
    uv_buf_t data_buf = uv_buf_init(cache_ptr, entry->cache->length);
    uv_buf_t bufs[] = {headers_buf, data_buf};

    int err = WriteFileSync(entry->cache_filename.c_str(), bufs, 2);
    if (err < 0) {
      Debug("failed: %s\n", uv_strerror(err));
    } else {
      Debug("success\n");
    }
  }
}

CompileCacheHandler::CompileCacheHandler(Environment* env)
    : isolate_(env->isolate()),
      is_debug_(
          env->enabled_debug_list()->enabled(DebugCategory::COMPILE_CACHE)) {}

// Directory structure:
// - Compile cache directory (from NODE_COMPILE_CACHE)
//   - <cache_version_tag_1>: hash of CachedDataVersionTag + NODE_VERESION
//   - <cache_version_tag_2>
//   - <cache_version_tag_3>
//     - <cache_file_1>: a hash of filename + module type
//     - <cache_file_2>
//     - <cache_file_3>
bool CompileCacheHandler::InitializeDirectory(Environment* env,
                                              const std::string& dir) {
  compiler_cache_key_ = GetCacheVersionTag();
  std::string compiler_cache_key_string = Uint32ToHex(compiler_cache_key_);
  std::vector<std::string_view> paths = {dir, compiler_cache_key_string};
  std::string cache_dir = PathResolve(env, paths);

  Debug("[compile cache] resolved path %s + %s -> %s\n",
        dir,
        compiler_cache_key_string,
        cache_dir);

  if (UNLIKELY(!env->permission()->is_granted(
          env, permission::PermissionScope::kFileSystemWrite, cache_dir))) {
    Debug("[compile cache] skipping cache because write permission for %s "
          "is not granted\n",
          cache_dir);
    return false;
  }

  if (UNLIKELY(!env->permission()->is_granted(
          env, permission::PermissionScope::kFileSystemRead, cache_dir))) {
    Debug("[compile cache] skipping cache because read permission for %s "
          "is not granted\n",
          cache_dir);
    return false;
  }

  fs::FSReqWrapSync req_wrap;
  int err = fs::MKDirpSync(nullptr, &(req_wrap.req), cache_dir, 0777, nullptr);
  if (is_debug_) {
    Debug("[compile cache] creating cache directory %s...%s\n",
          cache_dir,
          err < 0 ? uv_strerror(err) : "success");
  }
  if (err != 0 && err != UV_EEXIST) {
    return false;
  }

  compile_cache_dir_ = std::filesystem::path(cache_dir);
  return true;
}

}  // namespace node
