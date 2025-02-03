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

#ifndef SRC_CRYPTO_CRYPTO_TLS_H_
#define SRC_CRYPTO_CRYPTO_TLS_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "crypto/crypto_context.h"
#include "crypto/crypto_clienthello.h"

#include "async_wrap.h"
#include "stream_wrap.h"
#include "v8.h"

#include <openssl/ssl.h>

#include <string>
#include <vector>

namespace node {
namespace crypto {

class TLSWrap : public AsyncWrap,
                public StreamBase,
                public StreamListener {
 public:
  enum class Kind {
    kClient,
    kServer
  };

  enum class UnderlyingStreamWriteStatus { kHasActive, kVacancy };

  static void Initialize(v8::Local<v8::Object> target,
                         v8::Local<v8::Value> unused,
                         v8::Local<v8::Context> context,
                         void* priv);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  ~TLSWrap() override;

  inline bool is_cert_cb_running() const { return cert_cb_running_; }
  inline bool is_waiting_cert_cb() const { return cert_cb_ != nullptr; }
  inline bool has_session_callbacks() const { return session_callbacks_; }
  inline void set_cert_cb_running(bool on = true) { cert_cb_running_ = on; }
  inline void set_awaiting_new_session(bool on = true) {
    awaiting_new_session_ = on;
  }
  inline void enable_session_callbacks() { session_callbacks_ = true; }
  inline bool is_server() const { return kind_ == Kind::kServer; }
  inline bool is_client() const { return kind_ == Kind::kClient; }
  inline bool is_awaiting_new_session() const { return awaiting_new_session_; }

  // Implement StreamBase:
  bool IsAlive() override;
  bool IsClosing() override;
  bool IsIPCPipe() override;
  int GetFD() override;
  ShutdownWrap* CreateShutdownWrap(
      v8::Local<v8::Object> req_wrap_object) override;
  AsyncWrap* GetAsyncWrap() override;


  // Implement StreamResource:
  int ReadStart() override;  // Exposed to JS
  int ReadStop() override;   // Exposed to JS
  int DoShutdown(ShutdownWrap* req_wrap) override;
  int DoWrite(WriteWrap* w,
              uv_buf_t* bufs,
              size_t count,
              uv_stream_t* send_handle) override;
  // Return error_ string or nullptr if it's empty.
  const char* Error() const override;
  // Reset error_ string to empty. Not related to "clear text".
  void ClearError() override;

  v8::MaybeLocal<v8::ArrayBufferView> ocsp_response() const;
  void ClearOcspResponse();
  SSL_SESSION* ReleaseSession();

  // Called by the done() callback of the 'newSession' event.
  void NewSessionDoneCb();

  // Implement MemoryRetainer:
  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(TLSWrap)
  SET_SELF_SIZE(TLSWrap)

  std::string diagnostic_name() const override;

 private:
  // OpenSSL structures are opaque. Estimate SSL memory size for OpenSSL 1.1.1b:
  //   SSL: 6224
  //   SSL->SSL3_STATE: 1040
  //   ...some buffers: 42 * 1024
  // NOTE: Actually it is much more than this
  static constexpr int64_t kExternalSize = 6224 + 1040 + 42 * 1024;

  static constexpr int kClearOutChunkSize = 16384;

  // Maximum number of bytes for hello parser
  static constexpr int kMaxHelloLength = 16384;

  // Usual ServerHello + Certificate size
  static constexpr int kInitialClientBufferLength = 4096;

  // Maximum number of buffers passed to uv_write()
  static constexpr int kSimultaneousBufferCount = 10;

  typedef void (*CertCb)(void* arg);

  // Alternative to StreamListener::stream(), that returns a StreamBase instead
  // of a StreamResource.
  inline StreamBase* underlying_stream() const {
    return static_cast<StreamBase*>(stream());
  }

  void WaitForCertCb(CertCb cb, void* arg);

  TLSWrap(Environment* env,
          v8::Local<v8::Object> obj,
          Kind kind,
          StreamBase* stream,
          SecureContext* sc,
          UnderlyingStreamWriteStatus under_stream_ws);

  static void SSLInfoCallback(const SSL* ssl_, int where, int ret);
  void InitSSL();
  // SSL has a "clear" text (unencrypted) side (to/from the node API) and
  // encrypted ("enc") text side (to/from the underlying socket/stream).
  // On each side data flows "in" or "out" of SSL context.
  //
  // EncIn() doesn't exist. Encrypted data is pushed from underlying stream into
  // enc_in_ via the stream listener's OnStreamAlloc()/OnStreamRead() interface.
  void EncOut();  // Write encrypted data from enc_out_ to underlying stream.
  void ClearIn();  // SSL_write() clear data "in" to SSL.
  void ClearOut();  // SSL_read() clear text "out" from SSL.
  void Destroy();

  // Call Done() on outstanding WriteWrap request.
  void InvokeQueued(int status, const char* error_str = nullptr);

  // Drive the SSL state machine by attempting to SSL_read() and SSL_write() to
  // it. Transparent handshakes mean SSL_read() might trigger I/O on the
  // underlying stream even if there is no clear text to read or write.
  void Cycle();

  // Implement StreamListener:
  // Returns buf that points into enc_in_.
  uv_buf_t OnStreamAlloc(size_t size) override;
  void OnStreamRead(ssize_t nread, const uv_buf_t& buf) override;
  void OnStreamAfterWrite(WriteWrap* w, int status) override;

  int SetCACerts(SecureContext* sc);

  static int SelectSNIContextCallback(SSL* s, int* ad, void* arg);

  static void CertCbDone(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void DestroySSL(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EnableCertCb(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EnableALPNCb(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EnableKeylogCallback(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EnableSessionCallbacks(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EnableTrace(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void EndParser(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ExportKeyingMaterial(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetALPNNegotiatedProto(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetCertificate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetX509Certificate(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetCipher(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetEphemeralKeyInfo(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetFinished(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPeerCertificate(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPeerX509Certificate(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPeerFinished(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetProtocol(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetServername(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetSession(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetSharedSigalgs(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetTLSTicket(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetWriteQueueSize(
      const v8::FunctionCallbackInfo<v8::Value>& info);
  static void IsSessionReused(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void LoadSession(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void NewSessionDone(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void OnClientHelloParseEnd(void* arg);
  static void Receive(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Renegotiate(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void RequestOCSP(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetALPNProtocols(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetKeyCert(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetOCSPResponse(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetServername(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetSession(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetVerifyMode(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Start(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void VerifyError(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Wrap(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void WritesIssuedByPrevListenerDone(
      const v8::FunctionCallbackInfo<v8::Value>& args);

#ifdef SSL_set_max_send_fragment
  static void SetMaxSendFragment(
      const v8::FunctionCallbackInfo<v8::Value>& args);
#endif  // SSL_set_max_send_fragment

#ifndef OPENSSL_NO_PSK
  static void EnablePskCallback(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void SetPskIdentityHint(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static unsigned int PskServerCallback(SSL* s,
                                        const char* identity,
                                        unsigned char* psk,
                                        unsigned int max_psk_len);
  static unsigned int PskClientCallback(SSL* s,
                                        const char* hint,
                                        char* identity,
                                        unsigned int max_identity_len,
                                        unsigned char* psk,
                                        unsigned int max_psk_len);
#endif

  Environment* const env_;
  Kind kind_;
  ncrypto::SSLSessionPointer next_sess_;
  ncrypto::SSLPointer ssl_;
  ClientHelloParser hello_parser_;
  v8::Global<v8::ArrayBufferView> ocsp_response_;
  BaseObjectPtr<SecureContext> sni_context_;
  BaseObjectPtr<SecureContext> sc_;

  // BIO buffers hold encrypted data.
  BIO* enc_in_ = nullptr;   // StreamListener fills this for SSL_read().
  BIO* enc_out_ = nullptr;  // SSL_write()/handshake fills this for EncOut().
  // Waiting for ClearIn() to pass to SSL_write().
  std::unique_ptr<v8::BackingStore> pending_cleartext_input_;
  size_t write_size_ = 0;
  BaseObjectPtr<AsyncWrap> current_write_;
  BaseObjectPtr<AsyncWrap> current_empty_write_;
  std::string error_;

  bool session_callbacks_ = false;
  bool awaiting_new_session_ = false;
  bool in_dowrite_ = false;
  bool started_ = false;
  bool shutdown_ = false;
  bool cert_cb_running_ = false;
  bool eof_ = false;

  // TODO(@jasnell): These state flags should be revisited.
  // The established_ flag indicates that the handshake is
  // completed. The write_callback_scheduled_ flag is less
  // clear -- once it is set to true, it is never set to
  // false and it is only set to true after established_
  // is set to true, so it's likely redundant.
  bool established_ = false;
  bool write_callback_scheduled_ = false;

  int cycle_depth_ = 0;

  // SSL_set_cert_cb
  CertCb cert_cb_ = nullptr;
  void* cert_cb_arg_ = nullptr;

  ncrypto::BIOPointer bio_trace_;

  bool has_active_write_issued_by_prev_listener_ = false;

 public:
  std::vector<unsigned char> alpn_protos_;  // Accessed by SelectALPNCallback.
  bool alpn_callback_enabled_ = false;      // Accessed by SelectALPNCallback.
};

}  // namespace crypto
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_CRYPTO_CRYPTO_TLS_H_
