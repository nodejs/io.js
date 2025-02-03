#include "inspector_socket.h"
#include "llhttp.h"

#include "nbytes.h"
#include "simdutf.h"
#include "util-inl.h"

#include "openssl/sha.h"  // Sha-1 hash

#include <algorithm>
#include <cstring>
#include <map>

#define ACCEPT_KEY_LENGTH nbytes::Base64EncodedSize(20)

#define DUMP_READS 0
#define DUMP_WRITES 0

namespace node {
namespace inspector {

class TcpHolder {
 public:
  static void DisconnectAndDispose(TcpHolder* holder);
  using Pointer = DeleteFnPtr<TcpHolder, DisconnectAndDispose>;

  static Pointer Accept(uv_stream_t* server,
                        InspectorSocket::DelegatePointer delegate);
  void SetHandler(ProtocolHandler* handler);
  int WriteRaw(const std::vector<char>& buffer, uv_write_cb write_cb);
  uv_tcp_t* tcp() {
    return &tcp_;
  }
  InspectorSocket::Delegate* delegate();

 private:
  static TcpHolder* From(void* handle) {
    return node::ContainerOf(&TcpHolder::tcp_,
                             reinterpret_cast<uv_tcp_t*>(handle));
  }
  static void OnClosed(uv_handle_t* handle);
  static void OnDataReceivedCb(uv_stream_t* stream, ssize_t nread,
                               const uv_buf_t* buf);
  explicit TcpHolder(InspectorSocket::DelegatePointer delegate);
  ~TcpHolder() = default;
  void ReclaimUvBuf(const uv_buf_t* buf, ssize_t read);

  uv_tcp_t tcp_;
  const InspectorSocket::DelegatePointer delegate_;
  ProtocolHandler* handler_;
  std::vector<char> buffer;
};


class ProtocolHandler {
 public:
  ProtocolHandler(InspectorSocket* inspector, TcpHolder::Pointer tcp);

  virtual void AcceptUpgrade(const std::string& accept_key) = 0;
  virtual void OnData(std::vector<char>* data) = 0;
  virtual void OnEof() = 0;
  virtual void Write(const std::vector<char> data) = 0;
  virtual void CancelHandshake() = 0;

  std::string GetHost() const;

  InspectorSocket* inspector() {
    return inspector_;
  }
  virtual void Shutdown() = 0;

 protected:
  virtual ~ProtocolHandler() = default;
  int WriteRaw(const std::vector<char>& buffer, uv_write_cb write_cb);
  InspectorSocket::Delegate* delegate();

  InspectorSocket* const inspector_;
  TcpHolder::Pointer tcp_;
};

namespace {

#if DUMP_READS || DUMP_WRITES
static void dump_hex(const char* buf, size_t len) {
  const char* ptr = buf;
  const char* end = ptr + len;
  const char* cptr;
  char c;
  int i;

  while (ptr < end) {
    cptr = ptr;
    for (i = 0; i < 16 && ptr < end; i++) {
      printf("%2.2X  ", static_cast<unsigned char>(*(ptr++)));
    }
    for (i = 72 - (i * 4); i > 0; i--) {
      printf(" ");
    }
    for (i = 0; i < 16 && cptr < end; i++) {
      c = *(cptr++);
      printf("%c", (c > 0x19) ? c : '.');
    }
    printf("\n");
  }
  printf("\n\n");
}
#endif

class WriteRequest {
 public:
  WriteRequest(ProtocolHandler* handler, const std::vector<char>& buffer)
      : handler(handler)
      , storage(buffer)
      , req(uv_write_t())
      , buf(uv_buf_init(storage.data(), storage.size())) {}

  static WriteRequest* from_write_req(uv_write_t* req) {
    return node::ContainerOf(&WriteRequest::req, req);
  }

  static void Cleanup(uv_write_t* req, int status) {
    delete WriteRequest::from_write_req(req);
  }

  ProtocolHandler* const handler;
  std::vector<char> storage;
  uv_write_t req;
  uv_buf_t buf;
};

void allocate_buffer(uv_handle_t* stream, size_t len, uv_buf_t* buf) {
  *buf = uv_buf_init(new char[len], len);
}

static void remove_from_beginning(std::vector<char>* buffer, size_t count) {
  buffer->erase(buffer->begin(), buffer->begin() + count);
}

static const char CLOSE_FRAME[] = {'\x88', '\x00'};

enum ws_decode_result {
  FRAME_OK, FRAME_INCOMPLETE, FRAME_CLOSE, FRAME_ERROR
};

static void generate_accept_string(const std::string& client_key,
                                   char (*buffer)[ACCEPT_KEY_LENGTH]) {
  // Magic string from websockets spec.
  static const char ws_magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string input(client_key + ws_magic);
  char hash[SHA_DIGEST_LENGTH];

  CHECK(ACCEPT_KEY_LENGTH >= nbytes::Base64EncodedSize(SHA_DIGEST_LENGTH) &&
        "not enough space provided for base64 encode");
  USE(SHA1(reinterpret_cast<const unsigned char*>(input.data()),
           input.size(),
           reinterpret_cast<unsigned char*>(hash)));
  simdutf::binary_to_base64(hash, sizeof(hash), *buffer);
}

static std::string TrimPort(const std::string& host) {
  size_t last_colon_pos = host.rfind(':');
  if (last_colon_pos == std::string::npos)
    return host;
  size_t bracket = host.rfind(']');
  if (bracket == std::string::npos || last_colon_pos > bracket)
    return host.substr(0, last_colon_pos);
  return host;
}

static bool IsIPAddress(const std::string& host) {
  // To avoid DNS rebinding attacks, we are aware of the following requirements:
  // * the host name must be an IP address (CVE-2018-7160, CVE-2022-32212),
  // * the IP address must be routable (hackerone.com/reports/1632921), and
  // * the IP address must be formatted unambiguously (CVE-2022-43548).

  // The logic below assumes that the string is null-terminated, so ensure that
  // we did not somehow end up with null characters within the string.
  if (host.find('\0') != std::string::npos) return false;

  // All IPv6 addresses must be enclosed in square brackets, and anything
  // enclosed in square brackets must be an IPv6 address.
  if (host.length() >= 4 && host.front() == '[' && host.back() == ']') {
    // INET6_ADDRSTRLEN is the maximum length of the dual format (including the
    // terminating null character), which is the longest possible representation
    // of an IPv6 address: xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:ddd.ddd.ddd.ddd
    if (host.length() - 2 >= INET6_ADDRSTRLEN) return false;

    // Annoyingly, libuv's implementation of inet_pton() deviates from other
    // implementations of the function in that it allows '%' in IPv6 addresses.
    if (host.find('%') != std::string::npos) return false;

    // Parse the IPv6 address to ensure it is syntactically valid.
    char ipv6_str[INET6_ADDRSTRLEN];
    std::copy(host.begin() + 1, host.end() - 1, ipv6_str);
    ipv6_str[host.length() - 2] = '\0';
    unsigned char ipv6[sizeof(struct in6_addr)];
    if (uv_inet_pton(AF_INET6, ipv6_str, ipv6) != 0) return false;

    // The only non-routable IPv6 address is ::/128. It should not be necessary
    // to explicitly reject it because it will still be enclosed in square
    // brackets and not even macOS should make DNS requests in that case, but
    // history has taught us that we cannot be careful enough.
    // Note that RFC 4291 defines both "IPv4-Compatible IPv6 Addresses" and
    // "IPv4-Mapped IPv6 Addresses", which means that there are IPv6 addresses
    // (other than ::/128) that represent non-routable IPv4 addresses. However,
    // this translation assumes that the host is interpreted as an IPv6 address
    // in the first place, at which point DNS rebinding should not be an issue.
    if (std::all_of(ipv6, ipv6 + sizeof(ipv6), [](auto b) { return b == 0; })) {
      return false;
    }

    // It is a syntactically valid and routable IPv6 address enclosed in square
    // brackets. No client should be able to misinterpret this.
    return true;
  }

  // Anything not enclosed in square brackets must be an IPv4 address. It is
  // important here that inet_pton() accepts only the so-called dotted-decimal
  // notation, which is a strict subset of the so-called numbers-and-dots
  // notation that is allowed by inet_aton() and inet_addr(). This subset does
  // not allow hexadecimal or octal number formats.
  unsigned char ipv4[sizeof(struct in_addr)];
  if (uv_inet_pton(AF_INET, host.c_str(), ipv4) != 0) return false;

  // The only strictly non-routable IPv4 address is 0.0.0.0, and macOS will make
  // DNS requests for this IP address, so we need to explicitly reject it. In
  // fact, we can safely reject all of 0.0.0.0/8 (see Section 3.2 of RFC 791 and
  // Section 3.2.1.3 of RFC 1122).
  // Note that inet_pton() stores the IPv4 address in network byte order.
  if (ipv4[0] == 0) return false;

  // It is a routable IPv4 address in dotted-decimal notation.
  return true;
}

// Constants for hybi-10 frame format.

typedef int OpCode;

const OpCode kOpCodeContinuation = 0x0;
const OpCode kOpCodeText = 0x1;
const OpCode kOpCodeBinary = 0x2;
const OpCode kOpCodeClose = 0x8;
const OpCode kOpCodePing = 0x9;
const OpCode kOpCodePong = 0xA;

const unsigned char kFinalBit = 0x80;
const unsigned char kReserved1Bit = 0x40;
const unsigned char kReserved2Bit = 0x20;
const unsigned char kReserved3Bit = 0x10;
const unsigned char kOpCodeMask = 0xF;
const unsigned char kMaskBit = 0x80;
const unsigned char kPayloadLengthMask = 0x7F;

const size_t kMaxSingleBytePayloadLength = 125;
const size_t kTwoBytePayloadLengthField = 126;
const size_t kEightBytePayloadLengthField = 127;
const size_t kMaskingKeyWidthInBytes = 4;

static std::vector<char> encode_frame_hybi17(const std::vector<char>& message) {
  std::vector<char> frame;
  OpCode op_code = kOpCodeText;
  frame.push_back(kFinalBit | op_code);
  const size_t data_length = message.size();
  if (data_length <= kMaxSingleBytePayloadLength) {
    frame.push_back(static_cast<char>(data_length));
  } else if (data_length <= 0xFFFF) {
    frame.push_back(kTwoBytePayloadLengthField);
    frame.push_back((data_length & 0xFF00) >> 8);
    frame.push_back(data_length & 0xFF);
  } else {
    frame.push_back(kEightBytePayloadLengthField);
    char extended_payload_length[8];
    size_t remaining = data_length;
    // Fill the length into extended_payload_length in the network byte order.
    for (int i = 0; i < 8; ++i) {
      extended_payload_length[7 - i] = remaining & 0xFF;
      remaining >>= 8;
    }
    frame.insert(frame.end(), extended_payload_length,
                 extended_payload_length + 8);
    CHECK_EQ(0, remaining);
  }
  frame.insert(frame.end(), message.begin(), message.end());
  return frame;
}

static ws_decode_result decode_frame_hybi17(const std::vector<char>& buffer,
                                            bool client_frame,
                                            int* bytes_consumed,
                                            std::vector<char>* output,
                                            bool* compressed) {
  *bytes_consumed = 0;
  if (buffer.size() < 2)
    return FRAME_INCOMPLETE;

  auto it = buffer.begin();

  unsigned char first_byte = *it++;
  unsigned char second_byte = *it++;

  bool final = (first_byte & kFinalBit) != 0;
  bool reserved1 = (first_byte & kReserved1Bit) != 0;
  bool reserved2 = (first_byte & kReserved2Bit) != 0;
  bool reserved3 = (first_byte & kReserved3Bit) != 0;
  int op_code = first_byte & kOpCodeMask;
  bool masked = (second_byte & kMaskBit) != 0;
  *compressed = reserved1;
  if (!final || reserved2 || reserved3)
    return FRAME_ERROR;  // Only compression extension is supported.

  bool closed = false;
  switch (op_code) {
    case kOpCodeClose:
      closed = true;
      break;
    case kOpCodeText:
      break;
    case kOpCodeBinary:        // We don't support binary frames yet.
    case kOpCodeContinuation:  // We don't support binary frames yet.
    case kOpCodePing:          // We don't support binary frames yet.
    case kOpCodePong:          // We don't support binary frames yet.
    default:
      return FRAME_ERROR;
  }

  // In Hybi-17 spec client MUST mask its frame.
  if (client_frame && !masked) {
    return FRAME_ERROR;
  }

  uint64_t payload_length64 = second_byte & kPayloadLengthMask;
  if (payload_length64 > kMaxSingleBytePayloadLength) {
    int extended_payload_length_size;
    if (payload_length64 == kTwoBytePayloadLengthField) {
      extended_payload_length_size = 2;
    } else if (payload_length64 == kEightBytePayloadLengthField) {
      extended_payload_length_size = 8;
    } else {
      return FRAME_ERROR;
    }
    if ((buffer.end() - it) < extended_payload_length_size)
      return FRAME_INCOMPLETE;
    payload_length64 = 0;
    for (int i = 0; i < extended_payload_length_size; ++i) {
      payload_length64 <<= 8;
      payload_length64 |= static_cast<unsigned char>(*it++);
    }
  }

  static const uint64_t max_payload_length = 0x7FFFFFFFFFFFFFFFull;
  static const size_t max_length = SIZE_MAX;
  if (payload_length64 > max_payload_length ||
      payload_length64 > max_length - kMaskingKeyWidthInBytes) {
    // WebSocket frame length too large.
    return FRAME_ERROR;
  }
  size_t payload_length = static_cast<size_t>(payload_length64);

  if (buffer.size() - kMaskingKeyWidthInBytes < payload_length)
    return FRAME_INCOMPLETE;

  std::vector<char>::const_iterator masking_key = it;
  std::vector<char>::const_iterator payload = it + kMaskingKeyWidthInBytes;
  for (size_t i = 0; i < payload_length; ++i)  // Unmask the payload.
    output->insert(output->end(),
                   payload[i] ^ masking_key[i % kMaskingKeyWidthInBytes]);

  size_t pos = it + kMaskingKeyWidthInBytes + payload_length - buffer.begin();
  *bytes_consumed = pos;
  return closed ? FRAME_CLOSE : FRAME_OK;
}

// WS protocol
class WsHandler : public ProtocolHandler {
 public:
  WsHandler(InspectorSocket* inspector, TcpHolder::Pointer tcp)
            : ProtocolHandler(inspector, std::move(tcp)),
              OnCloseSent(&WsHandler::WaitForCloseReply),
              OnCloseReceived(&WsHandler::CloseFrameReceived),
              dispose_(false) { }

  void AcceptUpgrade(const std::string& accept_key) override { }
  void CancelHandshake() override {}

  void OnEof() override {
    tcp_.reset();
    if (dispose_)
      delete this;
  }

  void OnData(std::vector<char>* data) override {
    // 1. Parse.
    int processed = 0;
    do {
      processed = ParseWsFrames(*data);
      // 2. Fix the data size & length
      if (processed > 0) {
        remove_from_beginning(data, processed);
      }
    } while (processed > 0 && !data->empty());
  }

  void Write(const std::vector<char> data) override {
    std::vector<char> output = encode_frame_hybi17(data);
    WriteRaw(output, WriteRequest::Cleanup);
  }

 protected:
  void Shutdown() override {
    if (tcp_) {
      dispose_ = true;
      SendClose();
    } else {
      delete this;
    }
  }

 private:
  using Callback = void (WsHandler::*)();

  static void OnCloseFrameWritten(uv_write_t* req, int status) {
    WriteRequest* wr = WriteRequest::from_write_req(req);
    WsHandler* handler = static_cast<WsHandler*>(wr->handler);
    delete wr;
    Callback cb = handler->OnCloseSent;
    (handler->*cb)();
  }

  void WaitForCloseReply() {
    OnCloseReceived = &WsHandler::OnEof;
  }

  void SendClose() {
    WriteRaw(std::vector<char>(CLOSE_FRAME, CLOSE_FRAME + sizeof(CLOSE_FRAME)),
             OnCloseFrameWritten);
  }

  void CloseFrameReceived() {
    OnCloseSent = &WsHandler::OnEof;
    SendClose();
  }

  int ParseWsFrames(const std::vector<char>& buffer) {
    int bytes_consumed = 0;
    std::vector<char> output;
    bool compressed = false;

    ws_decode_result r =  decode_frame_hybi17(buffer,
                                              true /* client_frame */,
                                              &bytes_consumed, &output,
                                              &compressed);
    // Compressed frame means client is ignoring the headers and misbehaves
    if (compressed || r == FRAME_ERROR) {
      OnEof();
      bytes_consumed = 0;
    } else if (r == FRAME_CLOSE) {
      (this->*OnCloseReceived)();
      bytes_consumed = 0;
    } else if (r == FRAME_OK) {
      delegate()->OnWsFrame(output);
    }
    return bytes_consumed;
  }


  Callback OnCloseSent;
  Callback OnCloseReceived;
  bool dispose_;
};

// HTTP protocol
class HttpEvent {
 public:
  HttpEvent(const std::string& path, bool upgrade, bool isGET,
            const std::string& ws_key, const std::string& host)
            : path(path), upgrade(upgrade), isGET(isGET), ws_key(ws_key),
              host(host) { }

  std::string path;
  bool upgrade;
  bool isGET;
  std::string ws_key;
  std::string host;
};

class HttpHandler : public ProtocolHandler {
 public:
  explicit HttpHandler(InspectorSocket* inspector, TcpHolder::Pointer tcp)
                       : ProtocolHandler(inspector, std::move(tcp)),
                         parsing_value_(false) {
    llhttp_init(&parser_, HTTP_REQUEST, &parser_settings);
    llhttp_settings_init(&parser_settings);
    parser_settings.on_header_field = OnHeaderField;
    parser_settings.on_header_value = OnHeaderValue;
    parser_settings.on_message_complete = OnMessageComplete;
    parser_settings.on_url = OnPath;
  }

  void AcceptUpgrade(const std::string& accept_key) override {
    char accept_string[ACCEPT_KEY_LENGTH];
    generate_accept_string(accept_key, &accept_string);
    const char accept_ws_prefix[] = "HTTP/1.1 101 Switching Protocols\r\n"
                                    "Upgrade: websocket\r\n"
                                    "Connection: Upgrade\r\n"
                                    "Sec-WebSocket-Accept: ";
    const char accept_ws_suffix[] = "\r\n\r\n";
    std::vector<char> reply(accept_ws_prefix,
                            accept_ws_prefix + sizeof(accept_ws_prefix) - 1);
    reply.insert(reply.end(), accept_string,
                 accept_string + sizeof(accept_string));
    reply.insert(reply.end(), accept_ws_suffix,
                 accept_ws_suffix + sizeof(accept_ws_suffix) - 1);
    if (WriteRaw(reply, WriteRequest::Cleanup) >= 0) {
      inspector_->SwitchProtocol(new WsHandler(inspector_, std::move(tcp_)));
    } else {
      tcp_.reset();
    }
  }

  void CancelHandshake() override {
    const char HANDSHAKE_FAILED_RESPONSE[] =
        "HTTP/1.0 400 Bad Request\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n\r\n"
        "WebSockets request was expected\r\n";
    WriteRaw(std::vector<char>(HANDSHAKE_FAILED_RESPONSE,
             HANDSHAKE_FAILED_RESPONSE + sizeof(HANDSHAKE_FAILED_RESPONSE) - 1),
             ThenCloseAndReportFailure);
  }


  void OnEof() override {
    tcp_.reset();
  }

  void OnData(std::vector<char>* data) override {
    llhttp_errno_t err;
    err = llhttp_execute(&parser_, data->data(), data->size());

    if (err == HPE_PAUSED_UPGRADE) {
      err = HPE_OK;
      llhttp_resume_after_upgrade(&parser_);
    }
    data->clear();
    if (err != HPE_OK) {
      CancelHandshake();
    }
    // Event handling may delete *this
    std::vector<HttpEvent> events;
    std::swap(events, events_);
    for (const HttpEvent& event : events) {
      if (!IsAllowedHost(event.host) || !event.isGET) {
        CancelHandshake();
        return;
      } else if (!event.upgrade) {
        delegate()->OnHttpGet(event.host, event.path);
      } else if (event.ws_key.empty()) {
        CancelHandshake();
        return;
      } else {
        delegate()->OnSocketUpgrade(event.host, event.path, event.ws_key);
      }
    }
  }

  void Write(const std::vector<char> data) override {
    WriteRaw(data, WriteRequest::Cleanup);
  }

 protected:
  void Shutdown() override {
    delete this;
  }

 private:
  static void ThenCloseAndReportFailure(uv_write_t* req, int status) {
    ProtocolHandler* handler = WriteRequest::from_write_req(req)->handler;
    WriteRequest::Cleanup(req, status);
    handler->inspector()->SwitchProtocol(nullptr);
  }

  static int OnHeaderValue(llhttp_t* parser, const char* at, size_t length) {
    HttpHandler* handler = From(parser);
    handler->parsing_value_ = true;
    handler->headers_[handler->current_header_].append(at, length);
    return 0;
  }

  static int OnHeaderField(llhttp_t* parser, const char* at, size_t length) {
    HttpHandler* handler = From(parser);
    if (handler->parsing_value_) {
      handler->parsing_value_ = false;
      handler->current_header_.clear();
    }
    handler->current_header_.append(at, length);
    return 0;
  }

  static int OnPath(llhttp_t* parser, const char* at, size_t length) {
    HttpHandler* handler = From(parser);
    handler->path_.append(at, length);
    return 0;
  }

  static HttpHandler* From(llhttp_t* parser) {
    return node::ContainerOf(&HttpHandler::parser_, parser);
  }

  static int OnMessageComplete(llhttp_t* parser) {
    // Event needs to be fired after the parser is done.
    HttpHandler* handler = From(parser);
    handler->events_.emplace_back(handler->path_,
                                  parser->upgrade,
                                  parser->method == HTTP_GET,
                                  handler->HeaderValue("Sec-WebSocket-Key"),
                                  handler->HeaderValue("Host"));
    handler->path_ = "";
    handler->parsing_value_ = false;
    handler->headers_.clear();
    handler->current_header_ = "";
    return 0;
  }

  std::string HeaderValue(const std::string& header) const {
    bool header_found = false;
    std::string value;
    for (const auto& header_value : headers_) {
      if (node::StringEqualNoCaseN(header_value.first.data(), header.data(),
                                   header.length())) {
        if (header_found)
          return "";
        value = header_value.second;
        header_found = true;
      }
    }
    return value;
  }

  bool IsAllowedHost(const std::string& host_with_port) const {
    std::string host = TrimPort(host_with_port);
    return host.empty() || IsIPAddress(host)
           || node::StringEqualNoCase(host.data(), "localhost");
  }

  bool parsing_value_;
  llhttp_t parser_;
  llhttp_settings_t parser_settings;
  std::vector<HttpEvent> events_;
  std::string current_header_;
  std::map<std::string, std::string> headers_;
  std::string path_;
};

}  // namespace

// Any protocol
ProtocolHandler::ProtocolHandler(InspectorSocket* inspector,
                                 TcpHolder::Pointer tcp)
                                 : inspector_(inspector), tcp_(std::move(tcp)) {
  CHECK_NOT_NULL(tcp_);
  tcp_->SetHandler(this);
}

int ProtocolHandler::WriteRaw(const std::vector<char>& buffer,
                              uv_write_cb write_cb) {
  return tcp_->WriteRaw(buffer, write_cb);
}

InspectorSocket::Delegate* ProtocolHandler::delegate() {
  return tcp_->delegate();
}

std::string ProtocolHandler::GetHost() const {
  char ip[INET6_ADDRSTRLEN];
  sockaddr_storage addr;
  int len = sizeof(addr);
  int err = uv_tcp_getsockname(tcp_->tcp(),
                               reinterpret_cast<struct sockaddr*>(&addr),
                               &len);
  if (err != 0)
    return "";
  if (addr.ss_family == AF_INET6) {
    const sockaddr_in6* v6 = reinterpret_cast<const sockaddr_in6*>(&addr);
    err = uv_ip6_name(v6, ip, sizeof(ip));
  } else {
    const sockaddr_in* v4 = reinterpret_cast<const sockaddr_in*>(&addr);
    err = uv_ip4_name(v4, ip, sizeof(ip));
  }
  if (err != 0)
    return "";
  return ip;
}

// RAII uv_tcp_t wrapper
TcpHolder::TcpHolder(InspectorSocket::DelegatePointer delegate)
                     : tcp_(),
                       delegate_(std::move(delegate)),
                       handler_(nullptr) { }

// static
TcpHolder::Pointer TcpHolder::Accept(
    uv_stream_t* server,
    InspectorSocket::DelegatePointer delegate) {
  TcpHolder* result = new TcpHolder(std::move(delegate));
  uv_stream_t* tcp = reinterpret_cast<uv_stream_t*>(&result->tcp_);
  int err = uv_tcp_init(server->loop, &result->tcp_);
  if (err == 0) {
    err = uv_accept(server, tcp);
  }
  if (err == 0) {
    err = uv_read_start(tcp, allocate_buffer, OnDataReceivedCb);
  }
  if (err == 0) {
    return TcpHolder::Pointer(result);
  } else {
    delete result;
    return nullptr;
  }
}

void TcpHolder::SetHandler(ProtocolHandler* handler) {
  handler_ = handler;
}

int TcpHolder::WriteRaw(const std::vector<char>& buffer, uv_write_cb write_cb) {
#if DUMP_WRITES
  printf("%s (%ld bytes):\n", __FUNCTION__, buffer.size());
  dump_hex(buffer.data(), buffer.size());
  printf("\n");
#endif

  // Freed in write_request_cleanup
  WriteRequest* wr = new WriteRequest(handler_, buffer);
  uv_stream_t* stream = reinterpret_cast<uv_stream_t*>(&tcp_);
  int err = uv_write(&wr->req, stream, &wr->buf, 1, write_cb);
  if (err < 0)
    delete wr;
  return err < 0;
}

InspectorSocket::Delegate* TcpHolder::delegate() {
  return delegate_.get();
}

// static
void TcpHolder::OnClosed(uv_handle_t* handle) {
  delete From(handle);
}

void TcpHolder::OnDataReceivedCb(uv_stream_t* tcp, ssize_t nread,
                                 const uv_buf_t* buf) {
#if DUMP_READS
  if (nread >= 0) {
    printf("%s (%ld bytes)\n", __FUNCTION__, nread);
    dump_hex(buf->base, nread);
  } else {
    printf("[%s:%d] %s\n", __FUNCTION__, __LINE__, uv_err_name(nread));
  }
#endif
  TcpHolder* holder = From(tcp);
  holder->ReclaimUvBuf(buf, nread);
  if (nread < 0 || nread == UV_EOF) {
    holder->handler_->OnEof();
  } else {
    holder->handler_->OnData(&holder->buffer);
  }
}

// static
void TcpHolder::DisconnectAndDispose(TcpHolder* holder) {
  uv_handle_t* handle = reinterpret_cast<uv_handle_t*>(&holder->tcp_);
  uv_close(handle, OnClosed);
}

void TcpHolder::ReclaimUvBuf(const uv_buf_t* buf, ssize_t read) {
  if (read > 0) {
    buffer.insert(buffer.end(), buf->base, buf->base + read);
  }
  delete[] buf->base;
}

InspectorSocket::~InspectorSocket() = default;

// static
void InspectorSocket::Shutdown(ProtocolHandler* handler) {
  handler->Shutdown();
}

// static
InspectorSocket::Pointer InspectorSocket::Accept(uv_stream_t* server,
                                                 DelegatePointer delegate) {
  auto tcp = TcpHolder::Accept(server, std::move(delegate));
  if (tcp) {
    InspectorSocket* inspector = new InspectorSocket();
    inspector->SwitchProtocol(new HttpHandler(inspector, std::move(tcp)));
    return InspectorSocket::Pointer(inspector);
  } else {
    return InspectorSocket::Pointer(nullptr);
  }
}

void InspectorSocket::AcceptUpgrade(const std::string& ws_key) {
  protocol_handler_->AcceptUpgrade(ws_key);
}

void InspectorSocket::CancelHandshake() {
  protocol_handler_->CancelHandshake();
}

std::string InspectorSocket::GetHost() {
  return protocol_handler_->GetHost();
}

void InspectorSocket::SwitchProtocol(ProtocolHandler* handler) {
  protocol_handler_.reset(std::move(handler));
}

void InspectorSocket::Write(const char* data, size_t len) {
  protocol_handler_->Write(std::vector<char>(data, data + len));
}

}  // namespace inspector
}  // namespace node
