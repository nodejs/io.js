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

#ifndef SRC_UDP_WRAP_H_
#define SRC_UDP_WRAP_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "handle_wrap.h"
#include "req_wrap.h"
#include "node_sockaddr.h"
#include "uv.h"
#include "v8.h"

namespace node {

class ExternalReferenceRegistry;
class UDPWrapBase;

// A listener that can be attached to an `UDPWrapBase` object and generally
// manages its I/O activity. This is similar to `StreamListener`.
class UDPListener {
 public:
  virtual ~UDPListener();

  // Called right before data is received from the socket. Must return a
  // buffer suitable for reading data into, that is then passed to OnRecv.
  virtual uv_buf_t OnAlloc(size_t suggested_size) = 0;

  // Called right after data is received from the socket, and includes
  // information about the source address. If `nread` is negative, an error
  // has occurred, and it represents a libuv error code.
  virtual void OnRecv(ssize_t nread,
                      const uv_buf_t& buf,
                      const sockaddr* addr,
                      unsigned int flags) = 0;

  // Called when an asynchronous request for writing data is created.
  // The `msg_size` value contains the total size of the data to be sent,
  // but may be ignored by the implementation of this Method.
  // The return value is later passed to OnSendDone.
  virtual ReqWrap<uv_udp_send_t>* CreateSendWrap(size_t msg_size) = 0;

  // Called when an asynchronous request for writing data has finished.
  // If status is negative, an error has occurred, and it represents a libuv
  // error code.
  virtual void OnSendDone(ReqWrap<uv_udp_send_t>* wrap, int status) = 0;

  // Optional callback that is called after the socket has been bound.
  virtual void OnAfterBind() {}

  inline UDPWrapBase* udp() const { return wrap_; }

 protected:
  UDPWrapBase* wrap_ = nullptr;

  friend class UDPWrapBase;
};

class UDPWrapBase {
 public:
  // While UDPWrapBase itself does not extend from HandleWrap, classes
  // derived from it will (like UDPWrap)
  enum InternalFields {
    kUDPWrapBaseField = HandleWrap::kInternalFieldCount,
    kInternalFieldCount
  };
  virtual ~UDPWrapBase();

  // Start emitting OnAlloc() + OnRecv() events on the listener.
  virtual int RecvStart() = 0;

  // Stop emitting OnAlloc() + OnRecv() events on the listener.
  virtual int RecvStop() = 0;

  // Send a chunk of data over this socket. This may call CreateSendWrap()
  // on the listener if an async transmission is necessary.
  virtual ssize_t Send(uv_buf_t* bufs,
                       size_t nbufs,
                       const sockaddr* addr) = 0;

  virtual SocketAddress GetPeerName() = 0;
  virtual SocketAddress GetSockName() = 0;

  // Returns an AsyncWrap object with the same lifetime as this object.
  virtual AsyncWrap* GetAsyncWrap() = 0;

  void set_listener(UDPListener* listener);
  UDPListener* listener() const;

  static UDPWrapBase* FromObject(v8::Local<v8::Object> obj);

  static void RecvStart(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void RecvStop(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void AddMethods(Environment* env, v8::Local<v8::FunctionTemplate> t);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

 private:
  UDPListener* listener_ = nullptr;
};

class UDPWrap final : public HandleWrap,
                      public UDPWrapBase,
                      public UDPListener {
 public:
  enum SocketType {
    SOCKET
  };
  static void Initialize(v8::Local<v8::Object> target,
                         v8::Local<v8::Value> unused,
                         v8::Local<v8::Context> context,
                         void* priv);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);
  static void GetFD(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Open(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Bind(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Connect(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Send(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Bind6(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Connect6(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Send6(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Disconnect(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void AddMembership(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void DropMembership(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void AddSourceSpecificMembership(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void DropSourceSpecificMembership(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetMulticastInterface(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void BufferSize(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetSendQueueSize(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetSendQueueCount(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // UDPListener implementation
  uv_buf_t OnAlloc(size_t suggested_size) override;
  void OnRecv(ssize_t nread,
              const uv_buf_t& buf,
              const sockaddr* addr,
              unsigned int flags) override;
  ReqWrap<uv_udp_send_t>* CreateSendWrap(size_t msg_size) override;
  void OnSendDone(ReqWrap<uv_udp_send_t>* wrap, int status) override;

  // UDPWrapBase implementation
  int RecvStart() override;
  int RecvStop() override;
  ssize_t Send(uv_buf_t* bufs,
               size_t nbufs,
               const sockaddr* addr) override;

  SocketAddress GetPeerName() override;
  SocketAddress GetSockName() override;

  AsyncWrap* GetAsyncWrap() override;

  inline uv_udp_t* GetLibuvHandle() { return &handle_; }

  static v8::MaybeLocal<v8::Object> Instantiate(Environment* env,
                                                AsyncWrap* parent,
                                                SocketType type);
  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(UDPWrap)
  SET_SELF_SIZE(UDPWrap)

 private:
  typedef uv_udp_t HandleType;

  template <typename T,
            int (*F)(const typename T::HandleType*, sockaddr*, int*)>
  friend void GetSockOrPeerName(const v8::FunctionCallbackInfo<v8::Value>&);

  UDPWrap(Environment* env, v8::Local<v8::Object> object);

  static void DoBind(const v8::FunctionCallbackInfo<v8::Value>& args,
                     int family);
  static void DoConnect(const v8::FunctionCallbackInfo<v8::Value>& args,
                     int family);
  static void DoSend(const v8::FunctionCallbackInfo<v8::Value>& args,
                     int family);
  static void SetMembership(const v8::FunctionCallbackInfo<v8::Value>& args,
                            uv_membership membership);
  static void SetSourceMembership(
      const v8::FunctionCallbackInfo<v8::Value>& args,
      uv_membership membership);

  static void OnAlloc(uv_handle_t* handle,
                      size_t suggested_size,
                      uv_buf_t* buf);
  static void OnRecv(uv_udp_t* handle,
                     ssize_t nread,
                     const uv_buf_t* buf,
                     const struct sockaddr* addr,
                     unsigned int flags);

  uv_udp_t handle_;

  bool current_send_has_callback_;
  v8::Local<v8::Object> current_send_req_wrap_;
};

int sockaddr_for_family(int address_family,
                        const char* address,
                        const unsigned short port,
                        sockaddr_storage* addr);

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_UDP_WRAP_H_
