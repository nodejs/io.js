// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "memory_tracker-inl.h"
#include "node.h"
#include "node_buffer.h"

#include "async_wrap-inl.h"
#include "env-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "threadpoolwork-inl.h"
#include "util-inl.h"

#include "v8.h"

#include "brotli/encode.h"
#include "brotli/decode.h"
#include "zlib.h"

#include <sys/types.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <atomic>

namespace node {

using v8::ArrayBuffer;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Uint32Array;
using v8::Value;

namespace {

// Fewer than 64 bytes per chunk is not recommended.
// Technically it could work with as few as 8, but even 64 bytes
// is low.  Usually a MB or more is best.
#define Z_MIN_CHUNK 64
#define Z_MAX_CHUNK std::numeric_limits<double>::infinity()
#define Z_DEFAULT_CHUNK (16 * 1024)
#define Z_MIN_MEMLEVEL 1
#define Z_MAX_MEMLEVEL 9
#define Z_DEFAULT_MEMLEVEL 8
#define Z_MIN_LEVEL -1
#define Z_MAX_LEVEL 9
#define Z_DEFAULT_LEVEL Z_DEFAULT_COMPRESSION
#define Z_MIN_WINDOWBITS 8
#define Z_MAX_WINDOWBITS 15
#define Z_DEFAULT_WINDOWBITS 15

#define ZLIB_ERROR_CODES(V)      \
  V(Z_OK)                        \
  V(Z_STREAM_END)                \
  V(Z_NEED_DICT)                 \
  V(Z_ERRNO)                     \
  V(Z_STREAM_ERROR)              \
  V(Z_DATA_ERROR)                \
  V(Z_MEM_ERROR)                 \
  V(Z_BUF_ERROR)                 \
  V(Z_VERSION_ERROR)             \

inline const char* ZlibStrerror(int err) {
#define V(code) if (err == code) return #code;
  ZLIB_ERROR_CODES(V)
#undef V
  return "Z_UNKNOWN_ERROR";
}

enum node_zlib_mode {
  NONE,
  DEFLATE,
  INFLATE,
  GZIP,
  GUNZIP,
  DEFLATERAW,
  INFLATERAW,
  UNZIP,
  BROTLI_DECODE,
  BROTLI_ENCODE
};

constexpr uint8_t GZIP_HEADER_ID1 = 0x1f;
constexpr uint8_t GZIP_HEADER_ID2 = 0x8b;

struct CompressionError {
  CompressionError(const char* message, const char* code, int err)
      : message(message),
        code(code),
        err(err) {
    CHECK_NOT_NULL(message);
  }

  CompressionError() = default;

  const char* message = nullptr;
  const char* code = nullptr;
  int err = 0;

  inline bool IsError() const { return code != nullptr; }
};

class ZlibContext final : public MemoryRetainer {
 public:
  ZlibContext() = default;

  // Streaming-related, should be available for all compression libraries:
  void Close();
  void DoThreadPoolWork();
  void SetBuffers(const char* in, uint32_t in_len, char* out, uint32_t out_len);
  void SetFlush(int flush);
  void GetAfterWriteOffsets(uint32_t* avail_in, uint32_t* avail_out) const;
  CompressionError GetErrorInfo() const;
  inline void SetMode(node_zlib_mode mode) { mode_ = mode; }
  CompressionError ResetStream();

  // Zlib-specific:
  void Init(int level, int window_bits, int mem_level, int strategy,
            std::vector<unsigned char>&& dictionary);
  void SetAllocationFunctions(alloc_func alloc, free_func free, void* opaque);
  CompressionError SetParams(int level, int strategy);

  SET_MEMORY_INFO_NAME(ZlibContext)
  SET_SELF_SIZE(ZlibContext)

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackField("dictionary", dictionary_);
  }

  ZlibContext(const ZlibContext&) = delete;
  ZlibContext& operator=(const ZlibContext&) = delete;

 private:
  CompressionError ErrorForMessage(const char* message) const;
  CompressionError SetDictionary();
  bool InitZlib();

  Mutex mutex_;  // Protects zlib_init_done_.
  bool zlib_init_done_ = false;
  int err_ = 0;
  int flush_ = 0;
  int level_ = 0;
  int mem_level_ = 0;
  node_zlib_mode mode_ = NONE;
  int strategy_ = 0;
  int window_bits_ = 0;
  unsigned int gzip_id_bytes_read_ = 0;
  std::vector<unsigned char> dictionary_;

  z_stream strm_;
};

// Brotli has different data types for compression and decompression streams,
// so some of the specifics are implemented in more specific subclasses
class BrotliContext : public MemoryRetainer {
 public:
  BrotliContext() = default;

  void SetBuffers(const char* in, uint32_t in_len, char* out, uint32_t out_len);
  void SetFlush(int flush);
  void GetAfterWriteOffsets(uint32_t* avail_in, uint32_t* avail_out) const;
  inline void SetMode(node_zlib_mode mode) { mode_ = mode; }

  BrotliContext(const BrotliContext&) = delete;
  BrotliContext& operator=(const BrotliContext&) = delete;

 protected:
  node_zlib_mode mode_ = NONE;
  const uint8_t* next_in_ = nullptr;
  uint8_t* next_out_ = nullptr;
  size_t avail_in_ = 0;
  size_t avail_out_ = 0;
  BrotliEncoderOperation flush_ = BROTLI_OPERATION_PROCESS;
  // TODO(addaleax): These should not need to be stored here.
  // This is currently only done this way to make implementing ResetStream()
  // easier.
  brotli_alloc_func alloc_ = nullptr;
  brotli_free_func free_ = nullptr;
  void* alloc_opaque_ = nullptr;
};

class BrotliEncoderContext final : public BrotliContext {
 public:
  void Close();
  void DoThreadPoolWork();
  CompressionError Init(brotli_alloc_func alloc,
                        brotli_free_func free,
                        void* opaque);
  CompressionError ResetStream();
  CompressionError SetParams(int key, uint32_t value);
  CompressionError GetErrorInfo() const;

  SET_MEMORY_INFO_NAME(BrotliEncoderContext)
  SET_SELF_SIZE(BrotliEncoderContext)
  SET_NO_MEMORY_INFO()  // state_ is covered through allocation tracking.

 private:
  bool last_result_ = false;
  DeleteFnPtr<BrotliEncoderState, BrotliEncoderDestroyInstance> state_;
};

class BrotliDecoderContext final : public BrotliContext {
 public:
  void Close();
  void DoThreadPoolWork();
  CompressionError Init(brotli_alloc_func alloc,
                        brotli_free_func free,
                        void* opaque);
  CompressionError ResetStream();
  CompressionError SetParams(int key, uint32_t value);
  CompressionError GetErrorInfo() const;

  SET_MEMORY_INFO_NAME(BrotliDecoderContext)
  SET_SELF_SIZE(BrotliDecoderContext)
  SET_NO_MEMORY_INFO()  // state_ is covered through allocation tracking.

 private:
  BrotliDecoderResult last_result_ = BROTLI_DECODER_RESULT_SUCCESS;
  BrotliDecoderErrorCode error_ = BROTLI_DECODER_NO_ERROR;
  std::string error_string_;
  DeleteFnPtr<BrotliDecoderState, BrotliDecoderDestroyInstance> state_;
};

template <typename CompressionContext>
class CompressionStream : public AsyncWrap, public ThreadPoolWork {
 public:
  enum InternalFields {
    kCompressionStreamBaseField = AsyncWrap::kInternalFieldCount,
    kWriteJSCallback,
    kInternalFieldCount
  };

  CompressionStream(Environment* env, Local<Object> wrap)
      : AsyncWrap(env, wrap, AsyncWrap::PROVIDER_ZLIB),
        ThreadPoolWork(env, "zlib"),
        write_result_(nullptr) {
    MakeWeak();
  }

  ~CompressionStream() override {
    CHECK(!write_in_progress_);
    Close();
    CHECK_EQ(zlib_memory_, 0);
    CHECK_EQ(unreported_allocations_, 0);
  }

  Environment* env() const { return this->ThreadPoolWork::env(); }

  void Close() {
    if (write_in_progress_) {
      pending_close_ = true;
      return;
    }

    pending_close_ = false;
    closed_ = true;
    CHECK(init_done_ && "close before init");

    AllocScope alloc_scope(this);
    ctx_.Close();
  }


  static void Close(const FunctionCallbackInfo<Value>& args) {
    CompressionStream* ctx;
    ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());
    ctx->Close();
  }


  // write(flush, in, in_off, in_len, out, out_off, out_len)
  template <bool async>
  static void Write(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    Local<Context> context = env->context();
    CHECK_EQ(args.Length(), 7);

    uint32_t in_off, in_len, out_off, out_len, flush;
    const char* in;
    char* out;

    CHECK_EQ(false, args[0]->IsUndefined() && "must provide flush value");
    if (!args[0]->Uint32Value(context).To(&flush)) return;

    if (flush != Z_NO_FLUSH &&
        flush != Z_PARTIAL_FLUSH &&
        flush != Z_SYNC_FLUSH &&
        flush != Z_FULL_FLUSH &&
        flush != Z_FINISH &&
        flush != Z_BLOCK) {
      UNREACHABLE("Invalid flush value");
    }

    if (args[1]->IsNull()) {
      // just a flush
      in = nullptr;
      in_len = 0;
      in_off = 0;
    } else {
      CHECK(Buffer::HasInstance(args[1]));
      Local<Object> in_buf = args[1].As<Object>();
      if (!args[2]->Uint32Value(context).To(&in_off)) return;
      if (!args[3]->Uint32Value(context).To(&in_len)) return;

      CHECK(Buffer::IsWithinBounds(in_off, in_len, Buffer::Length(in_buf)));
      in = Buffer::Data(in_buf) + in_off;
    }

    CHECK(Buffer::HasInstance(args[4]));
    Local<Object> out_buf = args[4].As<Object>();
    if (!args[5]->Uint32Value(context).To(&out_off)) return;
    if (!args[6]->Uint32Value(context).To(&out_len)) return;
    CHECK(Buffer::IsWithinBounds(out_off, out_len, Buffer::Length(out_buf)));
    out = Buffer::Data(out_buf) + out_off;

    CompressionStream* ctx;
    ASSIGN_OR_RETURN_UNWRAP(&ctx, args.This());

    ctx->Write<async>(flush, in, in_len, out, out_len);
  }

  template <bool async>
  void Write(uint32_t flush,
             const char* in, uint32_t in_len,
             char* out, uint32_t out_len) {
    AllocScope alloc_scope(this);

    CHECK(init_done_ && "write before init");
    CHECK(!closed_ && "already finalized");

    CHECK_EQ(false, write_in_progress_);
    CHECK_EQ(false, pending_close_);
    write_in_progress_ = true;
    Ref();

    ctx_.SetBuffers(in, in_len, out, out_len);
    ctx_.SetFlush(flush);

    if constexpr (!async) {
      // sync version
      AsyncWrap::env()->PrintSyncTrace();
      DoThreadPoolWork();
      if (CheckError()) {
        UpdateWriteResult();
        write_in_progress_ = false;
      }
      Unref();
      return;
    }

    // async version
    ScheduleWork();
  }

  void UpdateWriteResult() {
    ctx_.GetAfterWriteOffsets(&write_result_[1], &write_result_[0]);
  }

  // thread pool!
  // This function may be called multiple times on the uv_work pool
  // for a single write() call, until all of the input bytes have
  // been consumed.
  void DoThreadPoolWork() override {
    ctx_.DoThreadPoolWork();
  }


  bool CheckError() {
    const CompressionError err = ctx_.GetErrorInfo();
    if (!err.IsError()) return true;
    EmitError(err);
    return false;
  }


  // v8 land!
  void AfterThreadPoolWork(int status) override {
    DCHECK(init_done_);
    AllocScope alloc_scope(this);
    auto on_scope_leave = OnScopeLeave([&]() { Unref(); });

    write_in_progress_ = false;

    if (status == UV_ECANCELED) {
      Close();
      return;
    }

    CHECK_EQ(status, 0);

    Environment* env = AsyncWrap::env();
    HandleScope handle_scope(env->isolate());
    Context::Scope context_scope(env->context());

    if (!CheckError())
      return;

    UpdateWriteResult();

    // call the write() cb
    Local<Value> cb =
        object()->GetInternalField(kWriteJSCallback).template As<Value>();
    MakeCallback(cb.As<Function>(), 0, nullptr);

    if (pending_close_)
      Close();
  }

  // TODO(addaleax): Switch to modern error system (node_errors.h).
  void EmitError(const CompressionError& err) {
    Environment* env = AsyncWrap::env();
    // If you hit this assertion, you forgot to enter the v8::Context first.
    CHECK_EQ(env->context(), env->isolate()->GetCurrentContext());

    HandleScope scope(env->isolate());
    Local<Value> args[] = {
      OneByteString(env->isolate(), err.message),
      Integer::New(env->isolate(), err.err),
      OneByteString(env->isolate(), err.code)
    };
    MakeCallback(env->onerror_string(), arraysize(args), args);

    // no hope of rescue.
    write_in_progress_ = false;
    if (pending_close_)
      Close();
  }

  static void Reset(const FunctionCallbackInfo<Value> &args) {
    CompressionStream* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

    AllocScope alloc_scope(wrap);
    const CompressionError err = wrap->context()->ResetStream();
    if (err.IsError())
      wrap->EmitError(err);
  }

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackField("compression context", ctx_);
    tracker->TrackFieldWithSize("zlib_memory",
                                zlib_memory_ + unreported_allocations_);
  }

 protected:
  CompressionContext* context() { return &ctx_; }

  void InitStream(uint32_t* write_result, Local<Function> write_js_callback) {
    write_result_ = write_result;
    object()->SetInternalField(kWriteJSCallback, write_js_callback);
    init_done_ = true;
  }

  // Allocation functions provided to zlib itself. We store the real size of
  // the allocated memory chunk just before the "payload" memory we return
  // to zlib.
  // Because we use zlib off the thread pool, we can not report memory directly
  // to V8; rather, we first store it as "unreported" memory in a separate
  // field and later report it back from the main thread.
  static void* AllocForZlib(void* data, uInt items, uInt size) {
    size_t real_size =
        MultiplyWithOverflowCheck(static_cast<size_t>(items),
                                  static_cast<size_t>(size));
    return AllocForBrotli(data, real_size);
  }

  static void* AllocForBrotli(void* data, size_t size) {
    size += sizeof(size_t);
    CompressionStream* ctx = static_cast<CompressionStream*>(data);
    char* memory = UncheckedMalloc(size);
    if (memory == nullptr) [[unlikely]] {
      return nullptr;
    }
    *reinterpret_cast<size_t*>(memory) = size;
    ctx->unreported_allocations_.fetch_add(size,
                                           std::memory_order_relaxed);
    return memory + sizeof(size_t);
  }

  static void FreeForZlib(void* data, void* pointer) {
    if (pointer == nullptr) [[unlikely]] {
      return;
    }
    CompressionStream* ctx = static_cast<CompressionStream*>(data);
    char* real_pointer = static_cast<char*>(pointer) - sizeof(size_t);
    size_t real_size = *reinterpret_cast<size_t*>(real_pointer);
    ctx->unreported_allocations_.fetch_sub(real_size,
                                           std::memory_order_relaxed);
    free(real_pointer);
  }

  // This is called on the main thread after zlib may have allocated something
  // in order to report it back to V8.
  void AdjustAmountOfExternalAllocatedMemory() {
    ssize_t report =
        unreported_allocations_.exchange(0, std::memory_order_relaxed);
    if (report == 0) return;
    CHECK_IMPLIES(report < 0, zlib_memory_ >= static_cast<size_t>(-report));
    zlib_memory_ += report;
    AsyncWrap::env()->isolate()->AdjustAmountOfExternalAllocatedMemory(report);
  }

  struct AllocScope {
    explicit AllocScope(CompressionStream* stream) : stream(stream) {}
    ~AllocScope() { stream->AdjustAmountOfExternalAllocatedMemory(); }
    CompressionStream* stream;
  };

 private:
  void Ref() {
    if (++refs_ == 1) {
      ClearWeak();
    }
  }

  void Unref() {
    CHECK_GT(refs_, 0);
    if (--refs_ == 0) {
      MakeWeak();
    }
  }

  bool init_done_ = false;
  bool write_in_progress_ = false;
  bool pending_close_ = false;
  bool closed_ = false;
  unsigned int refs_ = 0;
  uint32_t* write_result_ = nullptr;
  std::atomic<ssize_t> unreported_allocations_{0};
  size_t zlib_memory_ = 0;

  CompressionContext ctx_;
};

class ZlibStream final : public CompressionStream<ZlibContext> {
 public:
  ZlibStream(Environment* env, Local<Object> wrap, node_zlib_mode mode)
    : CompressionStream(env, wrap) {
    context()->SetMode(mode);
  }

  static void New(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    CHECK(args[0]->IsInt32());
    node_zlib_mode mode =
        static_cast<node_zlib_mode>(args[0].As<Int32>()->Value());
    new ZlibStream(env, args.This(), mode);
  }

  // just pull the ints out of the args and call the other Init
  static void Init(const FunctionCallbackInfo<Value>& args) {
    // Refs: https://github.com/nodejs/node/issues/16649
    // Refs: https://github.com/nodejs/node/issues/14161
    if (args.Length() == 5) {
      fprintf(stderr,
          "WARNING: You are likely using a version of node-tar or npm that "
          "is incompatible with this version of Node.js.\nPlease use "
          "either the version of npm that is bundled with Node.js, or "
          "a version of npm (> 5.5.1 or < 5.4.0) or node-tar (> 4.0.1) "
          "that is compatible with Node.js 9 and above.\n");
    }
    CHECK(args.Length() == 7 &&
      "init(windowBits, level, memLevel, strategy, writeResult, writeCallback,"
      " dictionary)");

    ZlibStream* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());

    Local<Context> context = args.GetIsolate()->GetCurrentContext();

    // windowBits is special. On the compression side, 0 is an invalid value.
    // But on the decompression side, a value of 0 for windowBits tells zlib
    // to use the window size in the zlib header of the compressed stream.
    uint32_t window_bits;
    if (!args[0]->Uint32Value(context).To(&window_bits)) return;

    int32_t level;
    if (!args[1]->Int32Value(context).To(&level)) return;

    uint32_t mem_level;
    if (!args[2]->Uint32Value(context).To(&mem_level)) return;

    uint32_t strategy;
    if (!args[3]->Uint32Value(context).To(&strategy)) return;

    CHECK(args[4]->IsUint32Array());
    Local<Uint32Array> array = args[4].As<Uint32Array>();
    Local<ArrayBuffer> ab = array->Buffer();
    uint32_t* write_result = static_cast<uint32_t*>(ab->Data());

    CHECK(args[5]->IsFunction());
    Local<Function> write_js_callback = args[5].As<Function>();

    std::vector<unsigned char> dictionary;
    if (Buffer::HasInstance(args[6])) {
      unsigned char* data =
          reinterpret_cast<unsigned char*>(Buffer::Data(args[6]));
      dictionary = std::vector<unsigned char>(
          data,
          data + Buffer::Length(args[6]));
    }

    wrap->InitStream(write_result, write_js_callback);

    AllocScope alloc_scope(wrap);
    wrap->context()->SetAllocationFunctions(
        AllocForZlib, FreeForZlib, static_cast<CompressionStream*>(wrap));
    wrap->context()->Init(level, window_bits, mem_level, strategy,
                          std::move(dictionary));
  }

  static void Params(const FunctionCallbackInfo<Value>& args) {
    CHECK(args.Length() == 2 && "params(level, strategy)");
    ZlibStream* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
    Local<Context> context = args.GetIsolate()->GetCurrentContext();
    int level;
    if (!args[0]->Int32Value(context).To(&level)) return;
    int strategy;
    if (!args[1]->Int32Value(context).To(&strategy)) return;

    AllocScope alloc_scope(wrap);
    const CompressionError err = wrap->context()->SetParams(level, strategy);
    if (err.IsError())
      wrap->EmitError(err);
  }

  SET_MEMORY_INFO_NAME(ZlibStream)
  SET_SELF_SIZE(ZlibStream)
};

template <typename CompressionContext>
class BrotliCompressionStream final :
  public CompressionStream<CompressionContext> {
 public:
  BrotliCompressionStream(Environment* env,
                          Local<Object> wrap,
                          node_zlib_mode mode)
    : CompressionStream<CompressionContext>(env, wrap) {
    context()->SetMode(mode);
  }

  inline CompressionContext* context() {
    return this->CompressionStream<CompressionContext>::context();
  }
  typedef typename CompressionStream<CompressionContext>::AllocScope AllocScope;

  static void New(const FunctionCallbackInfo<Value>& args) {
    Environment* env = Environment::GetCurrent(args);
    CHECK(args[0]->IsInt32());
    node_zlib_mode mode =
        static_cast<node_zlib_mode>(args[0].As<Int32>()->Value());
    new BrotliCompressionStream(env, args.This(), mode);
  }

  static void Init(const FunctionCallbackInfo<Value>& args) {
    BrotliCompressionStream* wrap;
    ASSIGN_OR_RETURN_UNWRAP(&wrap, args.This());
    CHECK(args.Length() == 3 && "init(params, writeResult, writeCallback)");

    CHECK(args[1]->IsUint32Array());
    uint32_t* write_result = reinterpret_cast<uint32_t*>(Buffer::Data(args[1]));

    CHECK(args[2]->IsFunction());
    Local<Function> write_js_callback = args[2].As<Function>();
    wrap->InitStream(write_result, write_js_callback);

    AllocScope alloc_scope(wrap);
    CompressionError err =
        wrap->context()->Init(
          CompressionStream<CompressionContext>::AllocForBrotli,
          CompressionStream<CompressionContext>::FreeForZlib,
          static_cast<CompressionStream<CompressionContext>*>(wrap));
    if (err.IsError()) {
      wrap->EmitError(err);
      // TODO(addaleax): Sometimes we generate better error codes in C++ land,
      // e.g. ERR_BROTLI_PARAM_SET_FAILED -- it's hard to access them with
      // the current bindings setup, though.
      THROW_ERR_ZLIB_INITIALIZATION_FAILED(wrap->env(),
                                           "Initialization failed");
      return;
    }

    CHECK(args[0]->IsUint32Array());
    const uint32_t* data = reinterpret_cast<uint32_t*>(Buffer::Data(args[0]));
    size_t len = args[0].As<Uint32Array>()->Length();

    for (int i = 0; static_cast<size_t>(i) < len; i++) {
      if (data[i] == static_cast<uint32_t>(-1))
        continue;
      err = wrap->context()->SetParams(i, data[i]);
      if (err.IsError()) {
        wrap->EmitError(err);
        THROW_ERR_ZLIB_INITIALIZATION_FAILED(wrap->env(),
                                             "Initialization failed");
        return;
      }
    }
  }

  static void Params(const FunctionCallbackInfo<Value>& args) {
    // Currently a no-op, and not accessed from JS land.
    // At some point Brotli may support changing parameters on the fly,
    // in which case we can implement this and a JS equivalent similar to
    // the zlib Params() function.
  }

  SET_MEMORY_INFO_NAME(BrotliCompressionStream)
  SET_SELF_SIZE(BrotliCompressionStream)
};

using BrotliEncoderStream = BrotliCompressionStream<BrotliEncoderContext>;
using BrotliDecoderStream = BrotliCompressionStream<BrotliDecoderContext>;

void ZlibContext::Close() {
  {
    Mutex::ScopedLock lock(mutex_);
    if (!zlib_init_done_) {
      dictionary_.clear();
      mode_ = NONE;
      return;
    }
  }

  CHECK_LE(mode_, UNZIP);

  int status = Z_OK;
  if (mode_ == DEFLATE || mode_ == GZIP || mode_ == DEFLATERAW) {
    status = deflateEnd(&strm_);
  } else if (mode_ == INFLATE || mode_ == GUNZIP || mode_ == INFLATERAW ||
             mode_ == UNZIP) {
    status = inflateEnd(&strm_);
  }

  CHECK(status == Z_OK || status == Z_DATA_ERROR);
  mode_ = NONE;

  dictionary_.clear();
}


void ZlibContext::DoThreadPoolWork() {
  bool first_init_call = InitZlib();
  if (first_init_call && err_ != Z_OK) {
    return;
  }

  const Bytef* next_expected_header_byte = nullptr;

  // If the avail_out is left at 0, then it means that it ran out
  // of room.  If there was avail_out left over, then it means
  // that all of the input was consumed.
  switch (mode_) {
    case DEFLATE:
    case GZIP:
    case DEFLATERAW:
      err_ = deflate(&strm_, flush_);
      break;
    case UNZIP:
      if (strm_.avail_in > 0) {
        next_expected_header_byte = strm_.next_in;
      }

      switch (gzip_id_bytes_read_) {
        case 0:
          if (next_expected_header_byte == nullptr) {
            break;
          }

          if (*next_expected_header_byte == GZIP_HEADER_ID1) {
            gzip_id_bytes_read_ = 1;
            next_expected_header_byte++;

            if (strm_.avail_in == 1) {
              // The only available byte was already read.
              break;
            }
          } else {
            mode_ = INFLATE;
            break;
          }

          [[fallthrough]];
        case 1:
          if (next_expected_header_byte == nullptr) {
            break;
          }

          if (*next_expected_header_byte == GZIP_HEADER_ID2) {
            gzip_id_bytes_read_ = 2;
            mode_ = GUNZIP;
          } else {
            // There is no actual difference between INFLATE and INFLATERAW
            // (after initialization).
            mode_ = INFLATE;
          }

          break;
        default:
          UNREACHABLE("invalid number of gzip magic number bytes read");
      }

      [[fallthrough]];
    case INFLATE:
    case GUNZIP:
    case INFLATERAW:
      err_ = inflate(&strm_, flush_);

      // If data was encoded with dictionary (INFLATERAW will have it set in
      // SetDictionary, don't repeat that here)
      if (mode_ != INFLATERAW &&
          err_ == Z_NEED_DICT &&
          !dictionary_.empty()) {
        // Load it
        err_ = inflateSetDictionary(&strm_,
                                    dictionary_.data(),
                                    dictionary_.size());
        if (err_ == Z_OK) {
          // And try to decode again
          err_ = inflate(&strm_, flush_);
        } else if (err_ == Z_DATA_ERROR) {
          // Both inflateSetDictionary() and inflate() return Z_DATA_ERROR.
          // Make it possible for After() to tell a bad dictionary from bad
          // input.
          err_ = Z_NEED_DICT;
        }
      }

      while (strm_.avail_in > 0 &&
             mode_ == GUNZIP &&
             err_ == Z_STREAM_END &&
             strm_.next_in[0] != 0x00) {
        // Bytes remain in input buffer. Perhaps this is another compressed
        // member in the same archive, or just trailing garbage.
        // Trailing zero bytes are okay, though, since they are frequently
        // used for padding.

        ResetStream();
        err_ = inflate(&strm_, flush_);
      }
      break;
    default:
      UNREACHABLE();
  }
}


void ZlibContext::SetBuffers(const char* in, uint32_t in_len,
                             char* out, uint32_t out_len) {
  strm_.avail_in = in_len;
  strm_.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in));
  strm_.avail_out = out_len;
  strm_.next_out = reinterpret_cast<Bytef*>(out);
}


void ZlibContext::SetFlush(int flush) {
  flush_ = flush;
}


void ZlibContext::GetAfterWriteOffsets(uint32_t* avail_in,
                                       uint32_t* avail_out) const {
  *avail_in = strm_.avail_in;
  *avail_out = strm_.avail_out;
}


CompressionError ZlibContext::ErrorForMessage(const char* message) const {
  if (strm_.msg != nullptr)
    message = strm_.msg;

  return CompressionError { message, ZlibStrerror(err_), err_ };
}


CompressionError ZlibContext::GetErrorInfo() const {
  // Acceptable error states depend on the type of zlib stream.
  switch (err_) {
  case Z_OK:
  case Z_BUF_ERROR:
    if (strm_.avail_out != 0 && flush_ == Z_FINISH) {
      return ErrorForMessage("unexpected end of file");
    }
  case Z_STREAM_END:
    // normal statuses, not fatal
    break;
  case Z_NEED_DICT:
    if (dictionary_.empty())
      return ErrorForMessage("Missing dictionary");
    else
      return ErrorForMessage("Bad dictionary");
  default:
    // something else.
    return ErrorForMessage("Zlib error");
  }

  return CompressionError {};
}


CompressionError ZlibContext::ResetStream() {
  bool first_init_call = InitZlib();
  if (first_init_call && err_ != Z_OK) {
    return ErrorForMessage("Failed to init stream before reset");
  }

  err_ = Z_OK;

  switch (mode_) {
    case DEFLATE:
    case DEFLATERAW:
    case GZIP:
      err_ = deflateReset(&strm_);
      break;
    case INFLATE:
    case INFLATERAW:
    case GUNZIP:
      err_ = inflateReset(&strm_);
      break;
    default:
      break;
  }

  if (err_ != Z_OK)
    return ErrorForMessage("Failed to reset stream");

  return SetDictionary();
}


void ZlibContext::SetAllocationFunctions(alloc_func alloc,
                                         free_func free,
                                         void* opaque) {
  strm_.zalloc = alloc;
  strm_.zfree = free;
  strm_.opaque = opaque;
}


void ZlibContext::Init(
    int level, int window_bits, int mem_level, int strategy,
    std::vector<unsigned char>&& dictionary) {
  if (!((window_bits == 0) &&
        (mode_ == INFLATE ||
         mode_ == GUNZIP ||
         mode_ == UNZIP))) {
    CHECK(
        (window_bits >= Z_MIN_WINDOWBITS && window_bits <= Z_MAX_WINDOWBITS) &&
        "invalid windowBits");
  }

  CHECK((level >= Z_MIN_LEVEL && level <= Z_MAX_LEVEL) &&
    "invalid compression level");

  CHECK((mem_level >= Z_MIN_MEMLEVEL && mem_level <= Z_MAX_MEMLEVEL) &&
        "invalid memlevel");

  CHECK((strategy == Z_FILTERED || strategy == Z_HUFFMAN_ONLY ||
         strategy == Z_RLE || strategy == Z_FIXED ||
         strategy == Z_DEFAULT_STRATEGY) &&
        "invalid strategy");

  level_ = level;
  window_bits_ = window_bits;
  mem_level_ = mem_level;
  strategy_ = strategy;

  flush_ = Z_NO_FLUSH;

  err_ = Z_OK;

  if (mode_ == GZIP || mode_ == GUNZIP) {
    window_bits_ += 16;
  }

  if (mode_ == UNZIP) {
    window_bits_ += 32;
  }

  if (mode_ == DEFLATERAW || mode_ == INFLATERAW) {
    window_bits_ *= -1;
  }

  dictionary_ = std::move(dictionary);
}

bool ZlibContext::InitZlib() {
  Mutex::ScopedLock lock(mutex_);
  if (zlib_init_done_) {
    return false;
  }

  switch (mode_) {
    case DEFLATE:
    case GZIP:
    case DEFLATERAW:
      err_ = deflateInit2(&strm_,
                          level_,
                          Z_DEFLATED,
                          window_bits_,
                          mem_level_,
                          strategy_);
      break;
    case INFLATE:
    case GUNZIP:
    case INFLATERAW:
    case UNZIP:
      err_ = inflateInit2(&strm_, window_bits_);
      break;
    default:
      UNREACHABLE();
  }

  if (err_ != Z_OK) {
    dictionary_.clear();
    mode_ = NONE;
    return true;
  }

  SetDictionary();
  zlib_init_done_ = true;
  return true;
}


CompressionError ZlibContext::SetDictionary() {
  if (dictionary_.empty())
    return CompressionError {};

  err_ = Z_OK;

  switch (mode_) {
    case DEFLATE:
    case DEFLATERAW:
      err_ = deflateSetDictionary(&strm_,
                                  dictionary_.data(),
                                  dictionary_.size());
      break;
    case INFLATERAW:
      // The other inflate cases will have the dictionary set when inflate()
      // returns Z_NEED_DICT in Process()
      err_ = inflateSetDictionary(&strm_,
                                  dictionary_.data(),
                                  dictionary_.size());
      break;
    default:
      break;
  }

  if (err_ != Z_OK) {
    return ErrorForMessage("Failed to set dictionary");
  }

  return CompressionError {};
}


CompressionError ZlibContext::SetParams(int level, int strategy) {
  bool first_init_call = InitZlib();
  if (first_init_call && err_ != Z_OK) {
    return ErrorForMessage("Failed to init stream before set parameters");
  }

  err_ = Z_OK;

  switch (mode_) {
    case DEFLATE:
    case DEFLATERAW:
      err_ = deflateParams(&strm_, level, strategy);
      break;
    default:
      break;
  }

  if (err_ != Z_OK && err_ != Z_BUF_ERROR) {
    return ErrorForMessage("Failed to set parameters");
  }

  return CompressionError {};
}


void BrotliContext::SetBuffers(const char* in, uint32_t in_len,
                               char* out, uint32_t out_len) {
  next_in_ = reinterpret_cast<const uint8_t*>(in);
  next_out_ = reinterpret_cast<uint8_t*>(out);
  avail_in_ = in_len;
  avail_out_ = out_len;
}


void BrotliContext::SetFlush(int flush) {
  flush_ = static_cast<BrotliEncoderOperation>(flush);
}


void BrotliContext::GetAfterWriteOffsets(uint32_t* avail_in,
                                         uint32_t* avail_out) const {
  *avail_in = avail_in_;
  *avail_out = avail_out_;
}


void BrotliEncoderContext::DoThreadPoolWork() {
  CHECK_EQ(mode_, BROTLI_ENCODE);
  CHECK(state_);
  const uint8_t* next_in = next_in_;
  last_result_ = BrotliEncoderCompressStream(state_.get(),
                                             flush_,
                                             &avail_in_,
                                             &next_in,
                                             &avail_out_,
                                             &next_out_,
                                             nullptr);
  next_in_ += next_in - next_in_;
}


void BrotliEncoderContext::Close() {
  state_.reset();
  mode_ = NONE;
}

CompressionError BrotliEncoderContext::Init(brotli_alloc_func alloc,
                                            brotli_free_func free,
                                            void* opaque) {
  alloc_ = alloc;
  free_ = free;
  alloc_opaque_ = opaque;
  state_.reset(BrotliEncoderCreateInstance(alloc, free, opaque));
  if (!state_) {
    return CompressionError("Could not initialize Brotli instance",
                            "ERR_ZLIB_INITIALIZATION_FAILED",
                            -1);
  } else {
    return CompressionError {};
  }
}

CompressionError BrotliEncoderContext::ResetStream() {
  return Init(alloc_, free_, alloc_opaque_);
}

CompressionError BrotliEncoderContext::SetParams(int key, uint32_t value) {
  if (!BrotliEncoderSetParameter(state_.get(),
                                 static_cast<BrotliEncoderParameter>(key),
                                 value)) {
    return CompressionError("Setting parameter failed",
                            "ERR_BROTLI_PARAM_SET_FAILED",
                            -1);
  } else {
    return CompressionError {};
  }
}

CompressionError BrotliEncoderContext::GetErrorInfo() const {
  if (!last_result_) {
    return CompressionError("Compression failed",
                            "ERR_BROTLI_COMPRESSION_FAILED",
                            -1);
  } else {
    return CompressionError {};
  }
}


void BrotliDecoderContext::Close() {
  state_.reset();
  mode_ = NONE;
}

void BrotliDecoderContext::DoThreadPoolWork() {
  CHECK_EQ(mode_, BROTLI_DECODE);
  CHECK(state_);
  const uint8_t* next_in = next_in_;
  last_result_ = BrotliDecoderDecompressStream(state_.get(),
                                               &avail_in_,
                                               &next_in,
                                               &avail_out_,
                                               &next_out_,
                                               nullptr);
  next_in_ += next_in - next_in_;
  if (last_result_ == BROTLI_DECODER_RESULT_ERROR) {
    error_ = BrotliDecoderGetErrorCode(state_.get());
    error_string_ = std::string("ERR_") + BrotliDecoderErrorString(error_);
  }
}

CompressionError BrotliDecoderContext::Init(brotli_alloc_func alloc,
                                            brotli_free_func free,
                                            void* opaque) {
  alloc_ = alloc;
  free_ = free;
  alloc_opaque_ = opaque;
  state_.reset(BrotliDecoderCreateInstance(alloc, free, opaque));
  if (!state_) {
    return CompressionError("Could not initialize Brotli instance",
                            "ERR_ZLIB_INITIALIZATION_FAILED",
                            -1);
  } else {
    return CompressionError {};
  }
}

CompressionError BrotliDecoderContext::ResetStream() {
  return Init(alloc_, free_, alloc_opaque_);
}

CompressionError BrotliDecoderContext::SetParams(int key, uint32_t value) {
  if (!BrotliDecoderSetParameter(state_.get(),
                                 static_cast<BrotliDecoderParameter>(key),
                                 value)) {
    return CompressionError("Setting parameter failed",
                            "ERR_BROTLI_PARAM_SET_FAILED",
                            -1);
  } else {
    return CompressionError {};
  }
}

CompressionError BrotliDecoderContext::GetErrorInfo() const {
  if (error_ != BROTLI_DECODER_NO_ERROR) {
    return CompressionError("Decompression failed",
                            error_string_.c_str(),
                            static_cast<int>(error_));
  } else if (flush_ == BROTLI_OPERATION_FINISH &&
             last_result_ == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
    // Match zlib's behaviour, as brotli doesn't have its own code for this.
    return CompressionError("unexpected end of file",
                            "Z_BUF_ERROR",
                            Z_BUF_ERROR);
  } else {
    return CompressionError {};
  }
}


template <typename Stream>
struct MakeClass {
  static void Make(Environment* env, Local<Object> target, const char* name) {
    Isolate* isolate = env->isolate();
    Local<FunctionTemplate> z = NewFunctionTemplate(isolate, Stream::New);

    z->InstanceTemplate()->SetInternalFieldCount(
        Stream::kInternalFieldCount);
    z->Inherit(AsyncWrap::GetConstructorTemplate(env));

    SetProtoMethod(isolate, z, "write", Stream::template Write<true>);
    SetProtoMethod(isolate, z, "writeSync", Stream::template Write<false>);
    SetProtoMethod(isolate, z, "close", Stream::Close);

    SetProtoMethod(isolate, z, "init", Stream::Init);
    SetProtoMethod(isolate, z, "params", Stream::Params);
    SetProtoMethod(isolate, z, "reset", Stream::Reset);

    SetConstructorFunction(env->context(), target, name, z);
  }

  static void Make(ExternalReferenceRegistry* registry) {
    registry->Register(Stream::New);
    registry->Register(Stream::template Write<true>);
    registry->Register(Stream::template Write<false>);
    registry->Register(Stream::Close);
    registry->Register(Stream::Init);
    registry->Register(Stream::Params);
    registry->Register(Stream::Reset);
  }
};

template <typename T, typename F>
T CallOnSequence(v8::Isolate* isolate, Local<Value> value, F callback) {
  if (value->IsString()) {
    Utf8Value data(isolate, value);
    return callback(data.out(), data.length());
  } else {
    ArrayBufferViewContents<char> data(value);
    return callback(data.data(), data.length());
  }
}

// TODO(joyeecheung): use fast API
static void CRC32(const FunctionCallbackInfo<Value>& args) {
  CHECK(args[0]->IsArrayBufferView() || args[0]->IsString());
  CHECK(args[1]->IsUint32());
  uint32_t value = args[1].As<v8::Uint32>()->Value();

  uint32_t result = CallOnSequence<uint32_t>(
      args.GetIsolate(),
      args[0],
      [&](const char* data, size_t size) -> uint32_t {
        return crc32(value, reinterpret_cast<const Bytef*>(data), size);
      });

  args.GetReturnValue().Set(result);
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Environment* env = Environment::GetCurrent(context);

  MakeClass<ZlibStream>::Make(env, target, "Zlib");
  MakeClass<BrotliEncoderStream>::Make(env, target, "BrotliEncoder");
  MakeClass<BrotliDecoderStream>::Make(env, target, "BrotliDecoder");

  SetMethod(context, target, "crc32", CRC32);
  target->Set(env->context(),
              FIXED_ONE_BYTE_STRING(env->isolate(), "ZLIB_VERSION"),
              FIXED_ONE_BYTE_STRING(env->isolate(), ZLIB_VERSION)).Check();
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  MakeClass<ZlibStream>::Make(registry);
  MakeClass<BrotliEncoderStream>::Make(registry);
  MakeClass<BrotliDecoderStream>::Make(registry);
  registry->Register(CRC32);
}

}  // anonymous namespace

void DefineZlibConstants(Local<Object> target) {
  NODE_DEFINE_CONSTANT(target, Z_NO_FLUSH);
  NODE_DEFINE_CONSTANT(target, Z_PARTIAL_FLUSH);
  NODE_DEFINE_CONSTANT(target, Z_SYNC_FLUSH);
  NODE_DEFINE_CONSTANT(target, Z_FULL_FLUSH);
  NODE_DEFINE_CONSTANT(target, Z_FINISH);
  NODE_DEFINE_CONSTANT(target, Z_BLOCK);

  // return/error codes
  NODE_DEFINE_CONSTANT(target, Z_OK);
  NODE_DEFINE_CONSTANT(target, Z_STREAM_END);
  NODE_DEFINE_CONSTANT(target, Z_NEED_DICT);
  NODE_DEFINE_CONSTANT(target, Z_ERRNO);
  NODE_DEFINE_CONSTANT(target, Z_STREAM_ERROR);
  NODE_DEFINE_CONSTANT(target, Z_DATA_ERROR);
  NODE_DEFINE_CONSTANT(target, Z_MEM_ERROR);
  NODE_DEFINE_CONSTANT(target, Z_BUF_ERROR);
  NODE_DEFINE_CONSTANT(target, Z_VERSION_ERROR);

  NODE_DEFINE_CONSTANT(target, Z_NO_COMPRESSION);
  NODE_DEFINE_CONSTANT(target, Z_BEST_SPEED);
  NODE_DEFINE_CONSTANT(target, Z_BEST_COMPRESSION);
  NODE_DEFINE_CONSTANT(target, Z_DEFAULT_COMPRESSION);
  NODE_DEFINE_CONSTANT(target, Z_FILTERED);
  NODE_DEFINE_CONSTANT(target, Z_HUFFMAN_ONLY);
  NODE_DEFINE_CONSTANT(target, Z_RLE);
  NODE_DEFINE_CONSTANT(target, Z_FIXED);
  NODE_DEFINE_CONSTANT(target, Z_DEFAULT_STRATEGY);
  NODE_DEFINE_CONSTANT(target, ZLIB_VERNUM);

  NODE_DEFINE_CONSTANT(target, DEFLATE);
  NODE_DEFINE_CONSTANT(target, INFLATE);
  NODE_DEFINE_CONSTANT(target, GZIP);
  NODE_DEFINE_CONSTANT(target, GUNZIP);
  NODE_DEFINE_CONSTANT(target, DEFLATERAW);
  NODE_DEFINE_CONSTANT(target, INFLATERAW);
  NODE_DEFINE_CONSTANT(target, UNZIP);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODE);
  NODE_DEFINE_CONSTANT(target, BROTLI_ENCODE);

  NODE_DEFINE_CONSTANT(target, Z_MIN_WINDOWBITS);
  NODE_DEFINE_CONSTANT(target, Z_MAX_WINDOWBITS);
  NODE_DEFINE_CONSTANT(target, Z_DEFAULT_WINDOWBITS);
  NODE_DEFINE_CONSTANT(target, Z_MIN_CHUNK);
  NODE_DEFINE_CONSTANT(target, Z_MAX_CHUNK);
  NODE_DEFINE_CONSTANT(target, Z_DEFAULT_CHUNK);
  NODE_DEFINE_CONSTANT(target, Z_MIN_MEMLEVEL);
  NODE_DEFINE_CONSTANT(target, Z_MAX_MEMLEVEL);
  NODE_DEFINE_CONSTANT(target, Z_DEFAULT_MEMLEVEL);
  NODE_DEFINE_CONSTANT(target, Z_MIN_LEVEL);
  NODE_DEFINE_CONSTANT(target, Z_MAX_LEVEL);
  NODE_DEFINE_CONSTANT(target, Z_DEFAULT_LEVEL);

  // Brotli constants
  NODE_DEFINE_CONSTANT(target, BROTLI_OPERATION_PROCESS);
  NODE_DEFINE_CONSTANT(target, BROTLI_OPERATION_FLUSH);
  NODE_DEFINE_CONSTANT(target, BROTLI_OPERATION_FINISH);
  NODE_DEFINE_CONSTANT(target, BROTLI_OPERATION_EMIT_METADATA);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_MODE);
  NODE_DEFINE_CONSTANT(target, BROTLI_MODE_GENERIC);
  NODE_DEFINE_CONSTANT(target, BROTLI_MODE_TEXT);
  NODE_DEFINE_CONSTANT(target, BROTLI_MODE_FONT);
  NODE_DEFINE_CONSTANT(target, BROTLI_DEFAULT_MODE);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_QUALITY);
  NODE_DEFINE_CONSTANT(target, BROTLI_MIN_QUALITY);
  NODE_DEFINE_CONSTANT(target, BROTLI_MAX_QUALITY);
  NODE_DEFINE_CONSTANT(target, BROTLI_DEFAULT_QUALITY);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_LGWIN);
  NODE_DEFINE_CONSTANT(target, BROTLI_MIN_WINDOW_BITS);
  NODE_DEFINE_CONSTANT(target, BROTLI_MAX_WINDOW_BITS);
  NODE_DEFINE_CONSTANT(target, BROTLI_LARGE_MAX_WINDOW_BITS);
  NODE_DEFINE_CONSTANT(target, BROTLI_DEFAULT_WINDOW);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_LGBLOCK);
  NODE_DEFINE_CONSTANT(target, BROTLI_MIN_INPUT_BLOCK_BITS);
  NODE_DEFINE_CONSTANT(target, BROTLI_MAX_INPUT_BLOCK_BITS);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_SIZE_HINT);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_LARGE_WINDOW);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_NPOSTFIX);
  NODE_DEFINE_CONSTANT(target, BROTLI_PARAM_NDIRECT);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_RESULT_ERROR);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_RESULT_SUCCESS);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT);
  NODE_DEFINE_CONSTANT(target,
      BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_PARAM_LARGE_WINDOW);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_NO_ERROR);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_SUCCESS);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_NEEDS_MORE_INPUT);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_NEEDS_MORE_OUTPUT);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_EXUBERANT_NIBBLE);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_RESERVED);
  NODE_DEFINE_CONSTANT(target,
      BROTLI_DECODER_ERROR_FORMAT_EXUBERANT_META_NIBBLE);
  NODE_DEFINE_CONSTANT(target,
      BROTLI_DECODER_ERROR_FORMAT_SIMPLE_HUFFMAN_ALPHABET);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_SIMPLE_HUFFMAN_SAME);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_CL_SPACE);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_HUFFMAN_SPACE);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_CONTEXT_MAP_REPEAT);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_BLOCK_LENGTH_1);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_BLOCK_LENGTH_2);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_TRANSFORM);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_DICTIONARY);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_WINDOW_BITS);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_PADDING_1);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_PADDING_2);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_FORMAT_DISTANCE);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_DICTIONARY_NOT_SET);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_INVALID_ARGUMENTS);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_ALLOC_CONTEXT_MODES);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_ALLOC_TREE_GROUPS);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_ALLOC_CONTEXT_MAP);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_ALLOC_RING_BUFFER_1);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_ALLOC_RING_BUFFER_2);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_ALLOC_BLOCK_TYPE_TREES);
  NODE_DEFINE_CONSTANT(target, BROTLI_DECODER_ERROR_UNREACHABLE);
}

}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(zlib, node::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(zlib, node::RegisterExternalReferences)
