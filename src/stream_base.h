#ifndef SRC_STREAM_BASE_H_
#define SRC_STREAM_BASE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "env.h"
#include "async_wrap.h"
#include "node.h"
#include "util.h"

#include "v8.h"

namespace node {

// Forward declarations
class Environment;
class ShutdownWrap;
class WriteWrap;
class StreamBase;
class StreamResource;
class ExternalReferenceRegistry;

struct StreamWriteResult {
  bool async;
  int err;
  WriteWrap* wrap;
  size_t bytes;
  BaseObjectPtr<AsyncWrap> wrap_obj;
};

using JSMethodFunction = void(const v8::FunctionCallbackInfo<v8::Value>& args);

class StreamReq {
 public:
  // The kSlot internal field here mirrors BaseObject::InternalFields::kSlot
  // here because instances derived from StreamReq will also derive from
  // BaseObject, and the slots are used for the identical purpose.
  enum InternalFields {
    kSlot = BaseObject::kSlot,
    kStreamReqField = BaseObject::kInternalFieldCount,
    kInternalFieldCount
  };

  inline explicit StreamReq(
      StreamBase* stream,
      v8::Local<v8::Object> req_wrap_obj);

  virtual ~StreamReq() = default;
  virtual AsyncWrap* GetAsyncWrap() = 0;
  inline v8::Local<v8::Object> object();

  // TODO(RaisinTen): Update the return type to a Maybe, so that we can indicate
  // if there is a pending exception/termination.
  void Done(int status, const char* error_str = nullptr);
  inline void Dispose();

  StreamBase* stream() const { return stream_; }

  static inline StreamReq* FromObject(v8::Local<v8::Object> req_wrap_obj);

  // Sets all internal fields of `req_wrap_obj` to `nullptr`.
  // This is what the `WriteWrap` and `ShutdownWrap` JS constructors do,
  // and what we use in C++ after creating these objects from their
  // v8::ObjectTemplates, to avoid the overhead of calling the
  // constructor explicitly.
  static inline void ResetObject(v8::Local<v8::Object> req_wrap_obj);

 protected:
  virtual void OnDone(int status) = 0;

  inline void AttachToObject(v8::Local<v8::Object> req_wrap_obj);

 private:
  StreamBase* const stream_;
};

class ShutdownWrap : public StreamReq {
 public:
  inline ShutdownWrap(
      StreamBase* stream,
      v8::Local<v8::Object> req_wrap_obj);

  static inline ShutdownWrap* FromObject(v8::Local<v8::Object> req_wrap_obj);
  template <typename T, bool kIsWeak>
  static inline ShutdownWrap* FromObject(
      const BaseObjectPtrImpl<T, kIsWeak>& base_obj);

  // Call stream()->EmitAfterShutdown() and dispose of this request wrap.
  void OnDone(int status) override;
};

class WriteWrap : public StreamReq {
 public:
  inline void SetBackingStore(std::unique_ptr<v8::BackingStore> bs);

  inline WriteWrap(
      StreamBase* stream,
      v8::Local<v8::Object> req_wrap_obj);

  static inline WriteWrap* FromObject(v8::Local<v8::Object> req_wrap_obj);
  template <typename T, bool kIsWeak>
  static inline WriteWrap* FromObject(
      const BaseObjectPtrImpl<T, kIsWeak>& base_obj);

  // Call stream()->EmitAfterWrite() and dispose of this request wrap.
  void OnDone(int status) override;

 private:
  std::unique_ptr<v8::BackingStore> backing_store_;
};


// This is the generic interface for objects that control Node.js' C++ streams.
// For example, the default `EmitToJSStreamListener` emits a stream's data
// as Buffers in JS, or `TLSWrap` reads and decrypts data from a stream.
class StreamListener {
 public:
  virtual ~StreamListener();

  // This is called when a stream wants to allocate memory before
  // reading data into the freshly allocated buffer (i.e. it is always followed
  // by a `OnStreamRead()` call).
  // This memory may be statically or dynamically allocated; for example,
  // a protocol parser may want to read data into a static buffer if it knows
  // that all data is going to be fully handled during the next
  // `OnStreamRead()` call.
  // The returned buffer does not need to contain `suggested_size` bytes.
  // The default implementation of this method returns a buffer that has exactly
  // the suggested size and is allocated using malloc().
  // It is not valid to return a zero-length buffer from this method.
  // It is not guaranteed that the corresponding `OnStreamRead()` call
  // happens in the same event loop turn as this call.
  virtual uv_buf_t OnStreamAlloc(size_t suggested_size) = 0;

  // `OnStreamRead()` is called when data is available on the socket and has
  // been read into the buffer provided by `OnStreamAlloc()`.
  // The `buf` argument is the return value of `uv_buf_t`, or may be a buffer
  // with base nullptr in case of an error.
  // `nread` is the number of read bytes (which is at most the buffer length),
  // or, if negative, a libuv error code.
  virtual void OnStreamRead(ssize_t nread,
                            const uv_buf_t& buf) = 0;

  // This is called once a write has finished. `status` may be 0 or,
  // if negative, a libuv error code.
  // By default, this is simply passed on to the previous listener
  // (and raises an assertion if there is none).
  virtual void OnStreamAfterWrite(WriteWrap* w, int status);

  // This is called once a shutdown has finished. `status` may be 0 or,
  // if negative, a libuv error code.
  // By default, this is simply passed on to the previous listener
  // (and raises an assertion if there is none).
  virtual void OnStreamAfterShutdown(ShutdownWrap* w, int status);

  // This is called by the stream if it determines that it wants more data
  // to be written to it. Not all streams support this.
  // This callback will not be called as long as there are active writes.
  // It is not supported by all streams; `stream->HasWantsWrite()` returns
  // true if it is supported by a stream.
  virtual void OnStreamWantsWrite(size_t suggested_size) {}

  // This is called immediately before the stream is destroyed.
  virtual void OnStreamDestroy() {}

  // The stream this is currently associated with, or nullptr if there is none.
  StreamResource* stream() const { return stream_; }

 protected:
  // Pass along a read error to the `StreamListener` instance that was active
  // before this one. For example, a protocol parser does not care about read
  // errors and may instead want to let the original handler
  // (e.g. the JS handler) take care of the situation.
  inline void PassReadErrorToPreviousListener(ssize_t nread);

  StreamResource* stream_ = nullptr;
  StreamListener* previous_listener_ = nullptr;

  friend class StreamResource;
};


// An (incomplete) stream listener class that calls the `.oncomplete()`
// method of the JS objects associated with the wrap objects.
class ReportWritesToJSStreamListener : public StreamListener {
 public:
  void OnStreamAfterWrite(WriteWrap* w, int status) override;
  void OnStreamAfterShutdown(ShutdownWrap* w, int status) override;

 private:
  void OnStreamAfterReqFinished(StreamReq* req_wrap, int status);
};


// A default emitter that just pushes data chunks as Buffer instances to
// JS land via the handle’s .ondata method.
class EmitToJSStreamListener : public ReportWritesToJSStreamListener {
 public:
  uv_buf_t OnStreamAlloc(size_t suggested_size) override;
  void OnStreamRead(ssize_t nread, const uv_buf_t& buf) override;
};


// An alternative listener that uses a custom, user-provided buffer
// for reading data.
class CustomBufferJSListener : public ReportWritesToJSStreamListener {
 public:
  uv_buf_t OnStreamAlloc(size_t suggested_size) override;
  void OnStreamRead(ssize_t nread, const uv_buf_t& buf) override;
  void OnStreamDestroy() override { delete this; }

  explicit CustomBufferJSListener(uv_buf_t buffer) : buffer_(buffer) {}

 private:
  uv_buf_t buffer_;
};


// A generic stream, comparable to JS land’s `Duplex` streams.
// A stream is always controlled through one `StreamListener` instance.
class StreamResource {
 public:
  virtual ~StreamResource();

  // These need to be implemented on the readable side of this stream:

  // Start reading from the underlying resource. This is called by the consumer
  // when more data is desired. Use `EmitAlloc()` and `EmitRead()` to
  // pass data along to the consumer.
  virtual int ReadStart() = 0;
  // Stop reading from the underlying resource. This is called by the
  // consumer when its buffers are full and no more data can be handled.
  virtual int ReadStop() = 0;

  // These need to be implemented on the writable side of this stream:
  // All of these methods may return an error code synchronously.
  // In that case, the finish callback should *not* be called.

  // Perform a shutdown operation, and either call req_wrap->Done() when
  // finished and return 0, return 1 for synchronous success, or
  // a libuv error code for synchronous failures.
  virtual int DoShutdown(ShutdownWrap* req_wrap) = 0;
  // Try to write as much data as possible synchronously, and modify
  // `*bufs` and `*count` accordingly. This is a no-op by default.
  // Return 0 for success and a libuv error code for failures.
  virtual int DoTryWrite(uv_buf_t** bufs, size_t* count);
  // Indicates whether this subclass overrides the DoTryWrite
  virtual inline bool HasDoTryWrite() const { return false; }
  // Initiate a write of data.
  // Upon an immediate failure, a libuv error code is returned,
  // w->Done() will never be called and caller should free `bufs`.
  // Otherwise, 0 is returned and w->Done(status) will be called
  // with status set to either
  //  (1) 0 after all data are written, or
  //  (2) a libuv error code when an error occurs
  // in either case, w->Done() will never be called before DoWrite() returns.
  // When 0 is returned:
  //  (1) memory specified by `bufs` and `count` must remain valid until
  //      w->Done() gets called.
  //  (2) `bufs` might or might not be changed, caller should not rely on this.
  //  (3) `bufs` should be freed after w->Done() gets called.
  virtual int DoWrite(WriteWrap* w,
                      uv_buf_t* bufs,
                      size_t count,
                      uv_stream_t* send_handle) = 0;

  // Returns true if the stream supports the `OnStreamWantsWrite()` interface.
  virtual bool HasWantsWrite() const { return false; }

  // Optionally, this may provide an error message to be used for
  // failing writes.
  virtual const char* Error() const;
  // Clear the current error (i.e. that would be returned by Error()).
  virtual void ClearError();

  // Transfer ownership of this stream to `listener`. The previous listener
  // will not receive any more callbacks while the new listener was active.
  inline void PushStreamListener(StreamListener* listener);
  // Remove a listener, and, if this was the currently active one,
  // transfer ownership back to the previous listener.
  void RemoveStreamListener(StreamListener* listener);

 protected:
  // Call the current listener's OnStreamAlloc() method.
  inline uv_buf_t EmitAlloc(size_t suggested_size);
  // Call the current listener's OnStreamRead() method and update the
  // stream's read byte counter.
  inline void EmitRead(
      ssize_t nread,
      const uv_buf_t& buf = uv_buf_init(nullptr, 0));
  // Call the current listener's OnStreamAfterWrite() method.
  inline void EmitAfterWrite(WriteWrap* w, int status);
  // Call the current listener's OnStreamAfterShutdown() method.
  inline void EmitAfterShutdown(ShutdownWrap* w, int status);
  // Call the current listener's OnStreamWantsWrite() method.
  inline void EmitWantsWrite(size_t suggested_size);

  StreamListener* listener_ = nullptr;
  uint64_t bytes_read_ = 0;
  uint64_t bytes_written_ = 0;

  friend class StreamListener;
};


class StreamBase : public StreamResource {
 public:
  // The kSlot field here mirrors that of BaseObject::InternalFields::kSlot
  // because instances deriving from StreamBase generally also derived from
  // BaseObject (it's possible for it not to, however).
  enum InternalFields {
    kSlot = BaseObject::kSlot,
    kStreamBaseField = BaseObject::kInternalFieldCount,
    kOnReadFunctionField,
    kInternalFieldCount
  };

  static void AddMethods(IsolateData* isolate_data,
                         v8::Local<v8::FunctionTemplate> target);
  static void AddMethods(Environment* env,
                         v8::Local<v8::FunctionTemplate> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
  virtual bool IsAlive() = 0;
  virtual bool IsClosing() = 0;
  virtual bool IsIPCPipe();
  virtual int GetFD();

  enum StreamBaseJSChecks { DONT_SKIP_NREAD_CHECKS, SKIP_NREAD_CHECKS };

  v8::MaybeLocal<v8::Value> CallJSOnreadMethod(
      ssize_t nread,
      v8::Local<v8::ArrayBuffer> ab,
      size_t offset = 0,
      StreamBaseJSChecks checks = DONT_SKIP_NREAD_CHECKS);

  // This is named `stream_env` to avoid name clashes, because a lot of
  // subclasses are also `BaseObject`s.
  Environment* stream_env() const { return env_; }

  // TODO(RaisinTen): Update the return type to a Maybe, so that we can indicate
  // if there is a pending exception/termination.
  // Shut down the current stream. This request can use an existing
  // ShutdownWrap object (that was created in JS), or a new one will be created.
  // Returns 1 in case of a synchronous completion, 0 in case of asynchronous
  // completion, and a libuv error case in case of synchronous failure.
  int Shutdown(v8::Local<v8::Object> req_wrap_obj = v8::Local<v8::Object>());

  // TODO(RaisinTen): Update the return type to a Maybe, so that we can indicate
  // if there is a pending exception/termination.
  // Write data to the current stream. This request can use an existing
  // WriteWrap object (that was created in JS), or a new one will be created.
  // This will first try to write synchronously using `DoTryWrite()`, then
  // asynchronously using `DoWrite()`.
  // Caller can pass `skip_try_write` as true if it has already called
  // `DoTryWrite()` and ends up with a partial write, or it knows that the
  // write is too large to finish synchronously.
  // If the return value indicates a synchronous completion, no callback will
  // be invoked.
  StreamWriteResult Write(
      uv_buf_t* bufs,
      size_t count,
      uv_stream_t* send_handle = nullptr,
      v8::Local<v8::Object> req_wrap_obj = v8::Local<v8::Object>(),
      bool skip_try_write = false);

  // These can be overridden by subclasses to get more specific wrap instances.
  // For example, a subclass Foo could create a FooWriteWrap or FooShutdownWrap
  // (inheriting from ShutdownWrap/WriteWrap) that has extra fields, like
  // an associated libuv request.
  virtual ShutdownWrap* CreateShutdownWrap(v8::Local<v8::Object> object);
  virtual WriteWrap* CreateWriteWrap(v8::Local<v8::Object> object);

  // One of these must be implemented
  virtual AsyncWrap* GetAsyncWrap() = 0;
  virtual v8::Local<v8::Object> GetObject();

  static inline StreamBase* FromObject(v8::Local<v8::Object> obj);

 protected:
  inline explicit StreamBase(Environment* env);

  // JS Methods
  int ReadStartJS(const v8::FunctionCallbackInfo<v8::Value>& args);
  int ReadStopJS(const v8::FunctionCallbackInfo<v8::Value>& args);
  int Shutdown(const v8::FunctionCallbackInfo<v8::Value>& args);
  int Writev(const v8::FunctionCallbackInfo<v8::Value>& args);
  int WriteBuffer(const v8::FunctionCallbackInfo<v8::Value>& args);
  template <enum encoding enc>
  int WriteString(const v8::FunctionCallbackInfo<v8::Value>& args);
  int UseUserBuffer(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void GetFD(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetExternal(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetBytesRead(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetBytesWritten(const v8::FunctionCallbackInfo<v8::Value>& args);
  inline void AttachToObject(v8::Local<v8::Object> obj);

  template <int (StreamBase::*Method)(
      const v8::FunctionCallbackInfo<v8::Value>& args)>
  static void JSMethod(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Internal, used only in StreamBase methods + env.cc.
  enum StreamBaseStateFields {
    kReadBytesOrError,
    kArrayBufferOffset,
    kBytesWritten,
    kLastWriteWasAsync,
    kNumStreamBaseStateFields
  };

 private:
  Environment* env_;
  EmitToJSStreamListener default_listener_;

  void SetWriteResult(const StreamWriteResult& res);
  static void AddAccessor(v8::Isolate* isolate,
                          v8::Local<v8::Signature> sig,
                          enum v8::PropertyAttribute attributes,
                          v8::Local<v8::FunctionTemplate> t,
                          JSMethodFunction* getter,
                          JSMethodFunction* setter,
                          v8::Local<v8::String> str);
  static void AddMethod(v8::Isolate* isolate,
                        v8::Local<v8::Signature> sig,
                        enum v8::PropertyAttribute attributes,
                        v8::Local<v8::FunctionTemplate> t,
                        JSMethodFunction* stream_method,
                        v8::Local<v8::String> str);

  friend class WriteWrap;
  friend class ShutdownWrap;
  friend class Environment;  // For kNumStreamBaseStateFields.
};


// These are helpers for creating `ShutdownWrap`/`WriteWrap` instances.
// `OtherBase` must have a constructor that matches the `AsyncWrap`
// constructors’s (Environment*, Local<Object>, AsyncWrap::Provider) signature
// and be a subclass of `AsyncWrap`.
template <typename OtherBase>
class SimpleShutdownWrap : public ShutdownWrap, public OtherBase {
 public:
  SimpleShutdownWrap(StreamBase* stream,
                     v8::Local<v8::Object> req_wrap_obj);

  AsyncWrap* GetAsyncWrap() override { return this; }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(SimpleShutdownWrap)
  SET_SELF_SIZE(SimpleShutdownWrap)

  bool IsNotIndicativeOfMemoryLeakAtExit() const override {
    return OtherBase::IsNotIndicativeOfMemoryLeakAtExit();
  }
};

template <typename OtherBase>
class SimpleWriteWrap : public WriteWrap, public OtherBase {
 public:
  SimpleWriteWrap(StreamBase* stream,
                  v8::Local<v8::Object> req_wrap_obj);

  AsyncWrap* GetAsyncWrap() override { return this; }

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(SimpleWriteWrap)
  SET_SELF_SIZE(SimpleWriteWrap)

  bool IsNotIndicativeOfMemoryLeakAtExit() const override {
    return OtherBase::IsNotIndicativeOfMemoryLeakAtExit();
  }
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_STREAM_BASE_H_
