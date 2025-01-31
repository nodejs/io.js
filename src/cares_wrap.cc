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

#include "cares_wrap.h"
#include "ada.h"
#include "async_wrap-inl.h"
#include "base_object-inl.h"
#include "env-inl.h"
#include "memory_tracker-inl.h"
#include "nbytes.h"
#include "node.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "req_wrap-inl.h"
#include "util-inl.h"
#include "uv.h"
#include "v8.h"

#include <cerrno>
#include <cstring>
#include <memory>
#include <vector>
#include <unordered_set>

#ifndef T_CAA
# define T_CAA    257 /* Certification Authority Authorization */
#endif

// OpenBSD does not define these
#ifndef AI_ALL
# define AI_ALL 0
#endif
#ifndef AI_V4MAPPED
# define AI_V4MAPPED 0
#endif


namespace node {
namespace cares_wrap {

using v8::Array;
using v8::Context;
using v8::EscapableHandleScope;
using v8::Exception;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Int32;
using v8::Integer;
using v8::Isolate;
using v8::Just;
using v8::JustVoid;
using v8::Local;
using v8::LocalVector;
using v8::Maybe;
using v8::Nothing;
using v8::Null;
using v8::Object;
using v8::String;
using v8::Uint32;
using v8::Value;

namespace {

Mutex ares_library_mutex;

inline uint16_t cares_get_16bit(const unsigned char* p) {
  return static_cast<uint32_t>(p[0] << 8U) | (static_cast<uint32_t>(p[1]));
}

void ares_poll_cb(uv_poll_t* watcher, int status, int events) {
  NodeAresTask* task = ContainerOf(&NodeAresTask::poll_watcher, watcher);
  ChannelWrap* channel = task->channel;

  /* Reset the idle timer */
  uv_timer_again(channel->timer_handle());

  if (status < 0) {
    /* An error happened. Just pretend that the socket is both readable and */
    /* writable. */
    ares_process_fd(channel->cares_channel(), task->sock, task->sock);
    return;
  }

  /* Process DNS responses */
  ares_process_fd(channel->cares_channel(),
                  events & UV_READABLE ? task->sock : ARES_SOCKET_BAD,
                  events & UV_WRITABLE ? task->sock : ARES_SOCKET_BAD);
}


void ares_poll_close_cb(uv_poll_t* watcher) {
  std::unique_ptr<NodeAresTask> free_me(
        ContainerOf(&NodeAresTask::poll_watcher, watcher));
}


/* Callback from ares when socket operation is started */
void ares_sockstate_cb(void* data, ares_socket_t sock, int read, int write) {
  ChannelWrap* channel = static_cast<ChannelWrap*>(data);
  NodeAresTask* task;

  NodeAresTask lookup_task;
  lookup_task.sock = sock;
  auto it = channel->task_list()->find(&lookup_task);

  task = (it == channel->task_list()->end()) ? nullptr : *it;

  if (read || write) {
    if (!task) {
      /* New socket */
      channel->StartTimer();

      task = NodeAresTask::Create(channel, sock);
      if (task == nullptr) {
        /* This should never happen unless we're out of memory or something */
        /* is seriously wrong. The socket won't be polled, but the query will */
        /* eventually time out. */
        return;
      }

      channel->task_list()->insert(task);
    }

    /* This should never fail. If it fails anyway, the query will eventually */
    /* time out. */
    uv_poll_start(&task->poll_watcher,
                  (read ? UV_READABLE : 0) | (write ? UV_WRITABLE : 0),
                  ares_poll_cb);

  } else {
    if (task != nullptr) {
      channel->task_list()->erase(it);
      channel->env()->CloseHandle(&task->poll_watcher, ares_poll_close_cb);
    }

    if (channel->task_list()->empty()) {
      channel->CloseTimer();
    }
  }
}

Local<Array> HostentToNames(Environment* env, struct hostent* host) {
  EscapableHandleScope scope(env->isolate());

  LocalVector<Value> names(env->isolate());

  for (uint32_t i = 0; host->h_aliases[i] != nullptr; ++i)
    names.emplace_back(OneByteString(env->isolate(), host->h_aliases[i]));

  Local<Array> ret = Array::New(env->isolate(), names.data(), names.size());

  return scope.Escape(ret);
}

Local<Array> HostentToNames(Environment* env,
                            struct hostent* host,
                            Local<Array> names) {
  size_t offset = names->Length();

  for (uint32_t i = 0; host->h_aliases[i] != nullptr; ++i) {
    names->Set(
        env->context(),
        i + offset,
        OneByteString(env->isolate(), host->h_aliases[i])).Check();
  }

  return names;
}

template <typename T>
Local<Array> AddrTTLToArray(
    Environment* env,
    const T* addrttls,
    size_t naddrttls) {
  MaybeStackBuffer<Local<Value>, 8> ttls(naddrttls);
  for (size_t i = 0; i < naddrttls; i++)
    ttls[i] = Integer::NewFromUnsigned(env->isolate(), addrttls[i].ttl);

  return Array::New(env->isolate(), ttls.out(), naddrttls);
}

int ParseGeneralReply(
    Environment* env,
    const unsigned char* buf,
    int len,
    int* type,
    Local<Array> ret,
    void* addrttls = nullptr,
    int* naddrttls = nullptr) {
  HandleScope handle_scope(env->isolate());
  hostent* host;

  int status;
  switch (*type) {
    case ns_t_a:
    case ns_t_cname:
    case ns_t_cname_or_a:
      status = ares_parse_a_reply(buf,
                                  len,
                                  &host,
                                  static_cast<ares_addrttl*>(addrttls),
                                  naddrttls);
      break;
    case ns_t_aaaa:
      status = ares_parse_aaaa_reply(buf,
                                     len,
                                     &host,
                                     static_cast<ares_addr6ttl*>(addrttls),
                                     naddrttls);
      break;
    case ns_t_ns:
      status = ares_parse_ns_reply(buf, len, &host);
      break;
    case ns_t_ptr:
      status = ares_parse_ptr_reply(buf, len, nullptr, 0, AF_INET, &host);
      break;
    default:
      UNREACHABLE("Bad NS type");
  }

  if (status != ARES_SUCCESS)
    return status;

  CHECK_NOT_NULL(host);
  HostEntPointer ptr(host);

  /* If it's `CNAME`, return the CNAME value;
   * And if it's `CNAME_OR_A` and it has value in `h_name` and `h_aliases[0]`,
   * we consider it's a CNAME record, otherwise we consider it's an A record. */
  if ((*type == ns_t_cname_or_a && ptr->h_name && ptr->h_aliases[0]) ||
      *type == ns_t_cname) {
    // A cname lookup always returns a single record but we follow the
    // common API here.
    *type = ns_t_cname;
    ret->Set(env->context(),
             ret->Length(),
             OneByteString(env->isolate(), ptr->h_name)).Check();
    return ARES_SUCCESS;
  }

  if (*type == ns_t_cname_or_a)
    *type = ns_t_a;

  if (*type == ns_t_ns) {
    HostentToNames(env, ptr.get(), ret);
  } else if (*type == ns_t_ptr) {
    uint32_t offset = ret->Length();
    for (uint32_t i = 0; ptr->h_aliases[i] != nullptr; i++) {
      auto alias = OneByteString(env->isolate(), ptr->h_aliases[i]);
      ret->Set(env->context(), i + offset, alias).Check();
    }
  } else {
    uint32_t offset = ret->Length();
    char ip[INET6_ADDRSTRLEN];
    for (uint32_t i = 0; ptr->h_addr_list[i] != nullptr; ++i) {
      uv_inet_ntop(ptr->h_addrtype, ptr->h_addr_list[i], ip, sizeof(ip));
      auto address = OneByteString(env->isolate(), ip);
      ret->Set(env->context(), i + offset, address).Check();
    }
  }

  return ARES_SUCCESS;
}

int ParseMxReply(
    Environment* env,
    const unsigned char* buf,
    int len,
    Local<Array> ret,
    bool need_type = false) {
  HandleScope handle_scope(env->isolate());

  struct ares_mx_reply* mx_start;
  int status = ares_parse_mx_reply(buf, len, &mx_start);
  if (status != ARES_SUCCESS)
    return status;

  uint32_t offset = ret->Length();
  ares_mx_reply* current = mx_start;
  for (uint32_t i = 0; current != nullptr; ++i, current = current->next) {
    Local<Object> mx_record = Object::New(env->isolate());
    mx_record->Set(env->context(),
                   env->exchange_string(),
                   OneByteString(env->isolate(), current->host)).Check();
    mx_record->Set(env->context(),
                   env->priority_string(),
                   Integer::New(env->isolate(), current->priority)).Check();
    if (need_type)
      mx_record->Set(env->context(),
                     env->type_string(),
                     env->dns_mx_string()).Check();

    ret->Set(env->context(), i + offset, mx_record).Check();
  }

  ares_free_data(mx_start);
  return ARES_SUCCESS;
}

int ParseCaaReply(
    Environment* env,
    const unsigned char* buf,
    int len,
    Local<Array> ret,
    bool need_type = false) {
  HandleScope handle_scope(env->isolate());

  struct ares_caa_reply* caa_start;
  int status = ares_parse_caa_reply(buf, len, &caa_start);
  if (status != ARES_SUCCESS)
    return status;

  uint32_t offset = ret->Length();
  ares_caa_reply* current = caa_start;
  for (uint32_t i = 0; current != nullptr; ++i, current = current->next) {
    Local<Object> caa_record = Object::New(env->isolate());

    caa_record->Set(env->context(),
                    env->dns_critical_string(),
                    Integer::New(env->isolate(), current->critical)).Check();
    caa_record->Set(env->context(),
                    OneByteString(env->isolate(), current->property),
                    OneByteString(env->isolate(), current->value)).Check();
    if (need_type)
      caa_record->Set(env->context(),
                      env->type_string(),
                      env->dns_caa_string()).Check();

    ret->Set(env->context(), i + offset, caa_record).Check();
  }

  ares_free_data(caa_start);
  return ARES_SUCCESS;
}

int ParseTxtReply(
    Environment* env,
    const unsigned char* buf,
    int len,
    Local<Array> ret,
    bool need_type = false) {
  HandleScope handle_scope(env->isolate());

  struct ares_txt_ext* txt_out;

  int status = ares_parse_txt_reply_ext(buf, len, &txt_out);
  if (status != ARES_SUCCESS)
    return status;

  Local<Array> txt_chunk;

  struct ares_txt_ext* current = txt_out;
  uint32_t i = 0, j;
  uint32_t offset = ret->Length();
  for (j = 0; current != nullptr; current = current->next) {
    Local<String> txt =
        OneByteString(env->isolate(), current->txt, current->length);

    // New record found - write out the current chunk
    if (current->record_start) {
      if (!txt_chunk.IsEmpty()) {
        if (need_type) {
          Local<Object> elem = Object::New(env->isolate());
          elem->Set(env->context(), env->entries_string(), txt_chunk).Check();
          elem->Set(env->context(),
                    env->type_string(),
                    env->dns_txt_string()).Check();
          ret->Set(env->context(), offset + i++, elem).Check();
        } else {
          ret->Set(env->context(), offset + i++, txt_chunk).Check();
        }
      }

      txt_chunk = Array::New(env->isolate());
      j = 0;
    }

    txt_chunk->Set(env->context(), j++, txt).Check();
  }

  // Push last chunk if it isn't empty
  if (!txt_chunk.IsEmpty()) {
    if (need_type) {
      Local<Object> elem = Object::New(env->isolate());
      elem->Set(env->context(), env->entries_string(), txt_chunk).Check();
      elem->Set(env->context(),
                env->type_string(),
                env->dns_txt_string()).Check();
      ret->Set(env->context(), offset + i, elem).Check();
    } else {
      ret->Set(env->context(), offset + i, txt_chunk).Check();
    }
  }

  ares_free_data(txt_out);
  return ARES_SUCCESS;
}


int ParseSrvReply(
    Environment* env,
    const unsigned char* buf,
    int len,
    Local<Array> ret,
    bool need_type = false) {
  HandleScope handle_scope(env->isolate());

  struct ares_srv_reply* srv_start;
  int status = ares_parse_srv_reply(buf, len, &srv_start);
  if (status != ARES_SUCCESS)
    return status;

  ares_srv_reply* current = srv_start;
  int offset = ret->Length();
  for (uint32_t i = 0; current != nullptr; ++i, current = current->next) {
    Local<Object> srv_record = Object::New(env->isolate());
    srv_record->Set(env->context(),
                    env->name_string(),
                    OneByteString(env->isolate(), current->host)).Check();
    srv_record->Set(env->context(),
                    env->port_string(),
                    Integer::New(env->isolate(), current->port)).Check();
    srv_record->Set(env->context(),
                    env->priority_string(),
                    Integer::New(env->isolate(), current->priority)).Check();
    srv_record->Set(env->context(),
                    env->weight_string(),
                    Integer::New(env->isolate(), current->weight)).Check();
    if (need_type)
      srv_record->Set(env->context(),
                      env->type_string(),
                      env->dns_srv_string()).Check();

    ret->Set(env->context(), i + offset, srv_record).Check();
  }

  ares_free_data(srv_start);
  return ARES_SUCCESS;
}


int ParseNaptrReply(
    Environment* env,
    const unsigned char* buf,
    int len,
    Local<Array> ret,
    bool need_type = false) {
  HandleScope handle_scope(env->isolate());

  ares_naptr_reply* naptr_start;
  int status = ares_parse_naptr_reply(buf, len, &naptr_start);

  if (status != ARES_SUCCESS)
    return status;

  ares_naptr_reply* current = naptr_start;
  int offset = ret->Length();
  for (uint32_t i = 0; current != nullptr; ++i, current = current->next) {
    Local<Object> naptr_record = Object::New(env->isolate());
    naptr_record->Set(env->context(),
                      env->flags_string(),
                      OneByteString(env->isolate(), current->flags)).Check();
    naptr_record->Set(env->context(),
                      env->service_string(),
                      OneByteString(env->isolate(),
                                    current->service)).Check();
    naptr_record->Set(env->context(),
                      env->regexp_string(),
                      OneByteString(env->isolate(),
                                    current->regexp)).Check();
    naptr_record->Set(env->context(),
                      env->replacement_string(),
                      OneByteString(env->isolate(),
                                    current->replacement)).Check();
    naptr_record->Set(env->context(),
                      env->order_string(),
                      Integer::New(env->isolate(), current->order)).Check();
    naptr_record->Set(env->context(),
                      env->preference_string(),
                      Integer::New(env->isolate(),
                                   current->preference)).Check();
    if (need_type)
      naptr_record->Set(env->context(),
                        env->type_string(),
                        env->dns_naptr_string()).Check();

    ret->Set(env->context(), i + offset, naptr_record).Check();
  }

  ares_free_data(naptr_start);
  return ARES_SUCCESS;
}


int ParseSoaReply(
    Environment* env,
    unsigned char* buf,
    int len,
    Local<Object>* ret) {
  EscapableHandleScope handle_scope(env->isolate());

  // Manage memory using standardard smart pointer std::unique_tr
  struct AresDeleter {
    void operator()(char* ptr) const noexcept { ares_free_string(ptr); }
  };
  using ares_unique_ptr = std::unique_ptr<char[], AresDeleter>;

  // Can't use ares_parse_soa_reply() here which can only parse single record
  const unsigned int ancount = cares_get_16bit(buf + 6);
  unsigned char* ptr = buf + NS_HFIXEDSZ;
  char* name_temp = nullptr;
  long temp_len;  // NOLINT(runtime/int)
  int status = ares_expand_name(ptr, buf, len, &name_temp, &temp_len);
  if (status != ARES_SUCCESS) {
    // returns EBADRESP in case of invalid input
    return status == ARES_EBADNAME ? ARES_EBADRESP : status;
  }

  const ares_unique_ptr name(name_temp);

  if (ptr + temp_len + NS_QFIXEDSZ > buf + len) {
    return ARES_EBADRESP;
  }
  ptr += temp_len + NS_QFIXEDSZ;

  for (unsigned int i = 0; i < ancount; i++) {
    char* rr_name_temp = nullptr;
    long rr_temp_len;  // NOLINT(runtime/int)
    int status2 = ares_expand_name(ptr, buf, len, &rr_name_temp, &rr_temp_len);

    if (status2 != ARES_SUCCESS)
      return status2 == ARES_EBADNAME ? ARES_EBADRESP : status2;

    const ares_unique_ptr rr_name(rr_name_temp);

    ptr += rr_temp_len;
    if (ptr + NS_RRFIXEDSZ > buf + len) {
      return ARES_EBADRESP;
    }

    const int rr_type = cares_get_16bit(ptr);
    const int rr_len = cares_get_16bit(ptr + 8);
    ptr += NS_RRFIXEDSZ;

    // only need SOA
    if (rr_type == ns_t_soa) {
      char* nsname_temp = nullptr;
      long nsname_temp_len;  // NOLINT(runtime/int)

      int status3 = ares_expand_name(ptr, buf, len,
                                     &nsname_temp,
                                     &nsname_temp_len);
      if (status3 != ARES_SUCCESS) {
        return status3 == ARES_EBADNAME ? ARES_EBADRESP : status3;
      }
      const ares_unique_ptr nsname(nsname_temp);
      ptr += nsname_temp_len;

      char* hostmaster_temp = nullptr;
      long hostmaster_temp_len;  // NOLINT(runtime/int)
      int status4 = ares_expand_name(ptr, buf, len,
                                     &hostmaster_temp,
                                     &hostmaster_temp_len);
      if (status4 != ARES_SUCCESS) {
        return status4 == ARES_EBADNAME ? ARES_EBADRESP : status4;
      }
      const ares_unique_ptr hostmaster(hostmaster_temp);
      ptr += hostmaster_temp_len;

      if (ptr + 5 * 4 > buf + len) {
        return ARES_EBADRESP;
      }

      const unsigned int serial = nbytes::ReadUint32BE(ptr + 0 * 4);
      const unsigned int refresh = nbytes::ReadUint32BE(ptr + 1 * 4);
      const unsigned int retry = nbytes::ReadUint32BE(ptr + 2 * 4);
      const unsigned int expire = nbytes::ReadUint32BE(ptr + 3 * 4);
      const unsigned int minttl = nbytes::ReadUint32BE(ptr + 4 * 4);

      Local<Object> soa_record = Object::New(env->isolate());
      soa_record->Set(env->context(),
                      env->nsname_string(),
                      OneByteString(env->isolate(), nsname.get())).Check();
      soa_record->Set(env->context(),
                      env->hostmaster_string(),
                      OneByteString(env->isolate(),
                                    hostmaster.get())).Check();
      soa_record->Set(env->context(),
                      env->serial_string(),
                      Integer::NewFromUnsigned(env->isolate(), serial)).Check();
      soa_record->Set(env->context(),
                      env->refresh_string(),
                      Integer::New(env->isolate(), refresh)).Check();
      soa_record->Set(env->context(),
                      env->retry_string(),
                      Integer::New(env->isolate(), retry)).Check();
      soa_record->Set(env->context(),
                      env->expire_string(),
                      Integer::New(env->isolate(), expire)).Check();
      soa_record->Set(env->context(),
                      env->minttl_string(),
                      Integer::NewFromUnsigned(env->isolate(), minttl)).Check();
      soa_record->Set(env->context(),
                      env->type_string(),
                      env->dns_soa_string()).Check();


      *ret = handle_scope.Escape(soa_record);
      break;
    }

    ptr += rr_len;
  }

  return ARES_SUCCESS;
}
}  // anonymous namespace

ChannelWrap::ChannelWrap(
      Environment* env,
      Local<Object> object,
      int timeout,
      int tries)
    : AsyncWrap(env, object, PROVIDER_DNSCHANNEL),
      timeout_(timeout),
      tries_(tries) {
  MakeWeak();

  Setup();
}

void ChannelWrap::MemoryInfo(MemoryTracker* tracker) const {
  if (timer_handle_ != nullptr)
    tracker->TrackField("timer_handle", *timer_handle_);
  tracker->TrackField("task_list", task_list_, "NodeAresTask::List");
}

void ChannelWrap::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsInt32());
  CHECK(args[1]->IsInt32());
  const int timeout = args[0].As<Int32>()->Value();
  const int tries = args[1].As<Int32>()->Value();
  Environment* env = Environment::GetCurrent(args);
  new ChannelWrap(env, args.This(), timeout, tries);
}

GetAddrInfoReqWrap::GetAddrInfoReqWrap(Environment* env,
                                       Local<Object> req_wrap_obj,
                                       uint8_t order)
    : ReqWrap(env, req_wrap_obj, AsyncWrap::PROVIDER_GETADDRINFOREQWRAP),
      order_(order) {}

GetNameInfoReqWrap::GetNameInfoReqWrap(
    Environment* env,
    Local<Object> req_wrap_obj)
    : ReqWrap(env, req_wrap_obj, AsyncWrap::PROVIDER_GETNAMEINFOREQWRAP) {}

/* This is called once per second by loop->timer. It is used to constantly */
/* call back into c-ares for possibly processing timeouts. */
void ChannelWrap::AresTimeout(uv_timer_t* handle) {
  ChannelWrap* channel = static_cast<ChannelWrap*>(handle->data);
  CHECK_EQ(channel->timer_handle(), handle);
  ares_process_fd(channel->cares_channel(), ARES_SOCKET_BAD, ARES_SOCKET_BAD);
}


void NodeAresTask::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("channel", channel);
}

/* Allocates and returns a new NodeAresTask */
NodeAresTask* NodeAresTask::Create(ChannelWrap* channel, ares_socket_t sock) {
  auto task = new NodeAresTask();

  task->channel = channel;
  task->sock = sock;

  if (uv_poll_init_socket(channel->env()->event_loop(),
                          &task->poll_watcher, sock) < 0) {
    /* This should never happen. */
    delete task;
    return nullptr;
  }

  return task;
}

void ChannelWrap::Setup() {
  struct ares_options options;
  memset(&options, 0, sizeof(options));
  options.flags = ARES_FLAG_NOCHECKRESP;
  options.sock_state_cb = ares_sockstate_cb;
  options.sock_state_cb_data = this;
  options.timeout = timeout_;
  options.tries = tries_;

  int r;
  if (!library_inited_) {
    Mutex::ScopedLock lock(ares_library_mutex);
    // Multiple calls to ares_library_init() increase a reference counter,
    // so this is a no-op except for the first call to it.
    r = ares_library_init(ARES_LIB_INIT_ALL);
    if (r != ARES_SUCCESS)
      return env()->ThrowError(ToErrorCodeString(r));
  }

  /* We do the call to ares_init_option for caller. */
  const int optmask =
      ARES_OPT_FLAGS | ARES_OPT_TIMEOUTMS |
      ARES_OPT_SOCK_STATE_CB | ARES_OPT_TRIES;
  r = ares_init_options(&channel_, &options, optmask);

  if (r != ARES_SUCCESS) {
    Mutex::ScopedLock lock(ares_library_mutex);
    ares_library_cleanup();
    return env()->ThrowError(ToErrorCodeString(r));
  }

  library_inited_ = true;
}

void ChannelWrap::StartTimer() {
  if (timer_handle_ == nullptr) {
    timer_handle_ = new uv_timer_t();
    timer_handle_->data = static_cast<void*>(this);
    uv_timer_init(env()->event_loop(), timer_handle_);
  } else if (uv_is_active(reinterpret_cast<uv_handle_t*>(timer_handle_))) {
    return;
  }
  int timeout = timeout_;
  if (timeout == 0) timeout = 1;
  if (timeout < 0 || timeout > 1000) timeout = 1000;
  uv_timer_start(timer_handle_, AresTimeout, timeout, timeout);
}

void ChannelWrap::CloseTimer() {
  if (timer_handle_ == nullptr)
    return;

  env()->CloseHandle(timer_handle_, [](uv_timer_t* handle) { delete handle; });
  timer_handle_ = nullptr;
}

ChannelWrap::~ChannelWrap() {
  ares_destroy(channel_);

  if (library_inited_) {
    Mutex::ScopedLock lock(ares_library_mutex);
    // This decreases the reference counter increased by ares_library_init().
    ares_library_cleanup();
  }

  CloseTimer();
}


void ChannelWrap::ModifyActivityQueryCount(int count) {
  active_query_count_ += count;
  CHECK_GE(active_query_count_, 0);
}


/**
 * This function is to check whether current servers are fallback servers
 * when cares initialized.
 *
 * The fallback servers of cares is [ "127.0.0.1" ] with no user additional
 * setting.
 */
void ChannelWrap::EnsureServers() {
  /* if last query is OK or servers are set by user self, do not check */
  if (query_last_ok_ || !is_servers_default_) {
    return;
  }

  ares_addr_port_node* servers = nullptr;

  ares_get_servers_ports(channel_, &servers);

  /* if no server or multi-servers, ignore */
  if (servers == nullptr) return;
  if (servers->next != nullptr) {
    ares_free_data(servers);
    is_servers_default_ = false;
    return;
  }

  /* if the only server is not 127.0.0.1, ignore */
  if (servers[0].family != AF_INET ||
      servers[0].addr.addr4.s_addr != htonl(INADDR_LOOPBACK) ||
      servers[0].tcp_port != 0 ||
      servers[0].udp_port != 0) {
    ares_free_data(servers);
    is_servers_default_ = false;
    return;
  }

  ares_free_data(servers);
  servers = nullptr;

  /* destroy channel and reset channel */
  ares_destroy(channel_);

  CloseTimer();
  Setup();
}

int AnyTraits::Send(QueryWrap<AnyTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_ANY);
  return ARES_SUCCESS;
}

int ATraits::Send(QueryWrap<ATraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_A);
  return ARES_SUCCESS;
}

int AaaaTraits::Send(QueryWrap<AaaaTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_AAAA);
  return ARES_SUCCESS;
}

int CaaTraits::Send(QueryWrap<CaaTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_CAA);
  return ARES_SUCCESS;
}

int CnameTraits::Send(QueryWrap<CnameTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_CNAME);
  return ARES_SUCCESS;
}

int MxTraits::Send(QueryWrap<MxTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_MX);
  return ARES_SUCCESS;
}

int NsTraits::Send(QueryWrap<NsTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_NS);
  return ARES_SUCCESS;
}

int TxtTraits::Send(QueryWrap<TxtTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_TXT);
  return ARES_SUCCESS;
}

int SrvTraits::Send(QueryWrap<SrvTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_SRV);
  return ARES_SUCCESS;
}

int PtrTraits::Send(QueryWrap<PtrTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_PTR);
  return ARES_SUCCESS;
}

int NaptrTraits::Send(QueryWrap<NaptrTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_NAPTR);
  return ARES_SUCCESS;
}

int SoaTraits::Send(QueryWrap<SoaTraits>* wrap, const char* name) {
  wrap->AresQuery(name, ARES_CLASS_IN, ARES_REC_TYPE_SOA);
  return ARES_SUCCESS;
}

int AnyTraits::Parse(
    QueryAnyWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Array> ret = Array::New(env->isolate());
  int type, status, old_count;

  /* Parse A records or CNAME records */
  ares_addrttl addrttls[256];
  int naddrttls = arraysize(addrttls);

  type = ns_t_cname_or_a;
  status = ParseGeneralReply(env,
                             buf,
                             len,
                             &type,
                             ret,
                             addrttls,
                             &naddrttls);
  uint32_t a_count = ret->Length();
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  if (type == ns_t_a) {
    CHECK_EQ(static_cast<uint32_t>(naddrttls), a_count);
    for (uint32_t i = 0; i < a_count; i++) {
      Local<Object> obj = Object::New(env->isolate());
      obj->Set(env->context(),
                env->address_string(),
                ret->Get(env->context(), i).ToLocalChecked()).Check();
      obj->Set(env->context(),
                env->ttl_string(),
                Integer::NewFromUnsigned(
                  env->isolate(), addrttls[i].ttl)).Check();
      obj->Set(env->context(),
                env->type_string(),
                env->dns_a_string()).Check();
      ret->Set(env->context(), i, obj).Check();
    }
  } else {
    for (uint32_t i = 0; i < a_count; i++) {
      Local<Object> obj = Object::New(env->isolate());
      obj->Set(env->context(),
                env->value_string(),
                ret->Get(env->context(), i).ToLocalChecked()).Check();
      obj->Set(env->context(),
                env->type_string(),
                env->dns_cname_string()).Check();
      ret->Set(env->context(), i, obj).Check();
    }
  }

  /* Parse AAAA records */
  ares_addr6ttl addr6ttls[256];
  int naddr6ttls = arraysize(addr6ttls);

  type = ns_t_aaaa;
  status = ParseGeneralReply(env,
                             buf,
                             len,
                             &type,
                             ret,
                             addr6ttls,
                             &naddr6ttls);
  uint32_t aaaa_count = ret->Length() - a_count;
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  CHECK_EQ(aaaa_count, static_cast<uint32_t>(naddr6ttls));
  CHECK_EQ(ret->Length(), a_count + aaaa_count);
  for (uint32_t i = a_count; i < ret->Length(); i++) {
    Local<Object> obj = Object::New(env->isolate());
    obj->Set(env->context(),
              env->address_string(),
              ret->Get(env->context(), i).ToLocalChecked()).Check();
    obj->Set(env->context(),
              env->ttl_string(),
              Integer::NewFromUnsigned(
                env->isolate(), addr6ttls[i - a_count].ttl)).Check();
    obj->Set(env->context(),
              env->type_string(),
              env->dns_aaaa_string()).Check();
    ret->Set(env->context(), i, obj).Check();
  }

  /* Parse MX records */
  status = ParseMxReply(env, buf, len, ret, true);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  /* Parse NS records */
  type = ns_t_ns;
  old_count = ret->Length();
  status = ParseGeneralReply(env, buf, len, &type, ret);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  for (uint32_t i = old_count; i < ret->Length(); i++) {
    Local<Object> obj = Object::New(env->isolate());
    obj->Set(env->context(),
             env->value_string(),
             ret->Get(env->context(), i).ToLocalChecked()).Check();
    obj->Set(env->context(),
              env->type_string(),
              env->dns_ns_string()).Check();
    ret->Set(env->context(), i, obj).Check();
  }

  /* Parse TXT records */
  status = ParseTxtReply(env, buf, len, ret, true);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  /* Parse SRV records */
  status = ParseSrvReply(env, buf, len, ret, true);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  /* Parse PTR records */
  type = ns_t_ptr;
  old_count = ret->Length();
  status = ParseGeneralReply(env, buf, len, &type, ret);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;
  for (uint32_t i = old_count; i < ret->Length(); i++) {
    Local<Object> obj = Object::New(env->isolate());
    obj->Set(env->context(),
              env->value_string(),
              ret->Get(env->context(), i).ToLocalChecked()).Check();
    obj->Set(env->context(),
              env->type_string(),
              env->dns_ptr_string()).Check();
    ret->Set(env->context(), i, obj).Check();
  }

  /* Parse NAPTR records */
  status = ParseNaptrReply(env, buf, len, ret, true);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  /* Parse SOA records */
  Local<Object> soa_record = Local<Object>();
  status = ParseSoaReply(env, buf, len, &soa_record);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  if (!soa_record.IsEmpty())
    ret->Set(env->context(), ret->Length(), soa_record).Check();

  /* Parse CAA records */
  status = ParseCaaReply(env, buf, len, ret, true);
  if (status != ARES_SUCCESS && status != ARES_ENODATA)
    return status;

  wrap->CallOnComplete(ret);
  return ARES_SUCCESS;
}

int ATraits::Parse(
    QueryAWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  ares_addrttl addrttls[256];
  int naddrttls = arraysize(addrttls), status;
  Local<Array> ret = Array::New(env->isolate());

  int type = ns_t_a;
  status = ParseGeneralReply(env,
                             buf,
                             len,
                             &type,
                             ret,
                             addrttls,
                             &naddrttls);
  if (status != ARES_SUCCESS)
    return status;

  Local<Array> ttls = AddrTTLToArray<ares_addrttl>(env, addrttls, naddrttls);

  wrap->CallOnComplete(ret, ttls);
  return ARES_SUCCESS;
}

int AaaaTraits::Parse(
    QueryAaaaWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  ares_addr6ttl addrttls[256];
  int naddrttls = arraysize(addrttls), status;
  Local<Array> ret = Array::New(env->isolate());

  int type = ns_t_aaaa;
  status = ParseGeneralReply(env,
                             buf,
                             len,
                             &type,
                             ret,
                             addrttls,
                             &naddrttls);
  if (status != ARES_SUCCESS)
    return status;

  Local<Array> ttls = AddrTTLToArray<ares_addr6ttl>(env, addrttls, naddrttls);

  wrap->CallOnComplete(ret, ttls);
  return ARES_SUCCESS;
}

int CaaTraits::Parse(
    QueryCaaWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Array> ret = Array::New(env->isolate());
  int status = ParseCaaReply(env, buf, len, ret);
  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(ret);
  return ARES_SUCCESS;
}

int CnameTraits::Parse(
    QueryCnameWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Array> ret = Array::New(env->isolate());
  int type = ns_t_cname;
  int status = ParseGeneralReply(env, buf, len, &type, ret);
  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(ret);
  return ARES_SUCCESS;
}

int MxTraits::Parse(
    QueryMxWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Array> mx_records = Array::New(env->isolate());
  int status = ParseMxReply(env, buf, len, mx_records);

  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(mx_records);
  return ARES_SUCCESS;
}

int NsTraits::Parse(
    QueryNsWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  int type = ns_t_ns;
  Local<Array> names = Array::New(env->isolate());
  int status = ParseGeneralReply(env, buf, len, &type, names);
  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(names);
  return ARES_SUCCESS;
}

int TxtTraits::Parse(
    QueryTxtWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Array> txt_records = Array::New(env->isolate());
  int status = ParseTxtReply(env, buf, len, txt_records);
  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(txt_records);
  return ARES_SUCCESS;
}

int SrvTraits::Parse(
    QuerySrvWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }

  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Array> srv_records = Array::New(env->isolate());
  int status = ParseSrvReply(env, buf, len, srv_records);
  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(srv_records);
  return ARES_SUCCESS;
}

int PtrTraits::Parse(
    QueryPtrWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }
  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  int type = ns_t_ptr;
  Local<Array> aliases = Array::New(env->isolate());

  int status = ParseGeneralReply(env, buf, len, &type, aliases);
  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(aliases);
  return ARES_SUCCESS;
}

int NaptrTraits::Parse(
    QueryNaptrWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }
  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Array> naptr_records = Array::New(env->isolate());
  int status = ParseNaptrReply(env, buf, len, naptr_records);
  if (status != ARES_SUCCESS)
    return status;

  wrap->CallOnComplete(naptr_records);
  return ARES_SUCCESS;
}

int SoaTraits::Parse(
    QuerySoaWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }
  unsigned char* buf = response->buf.data;
  int len = response->buf.size;

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  ares_soa_reply* soa_out;
  int status = ares_parse_soa_reply(buf, len, &soa_out);

  if (status != ARES_SUCCESS)
    return status;

  Local<Object> soa_record = Object::New(env->isolate());

  soa_record->Set(env->context(),
                  env->nsname_string(),
                  OneByteString(env->isolate(), soa_out->nsname)).Check();
  soa_record->Set(env->context(),
                  env->hostmaster_string(),
                  OneByteString(env->isolate(), soa_out->hostmaster)).Check();
  soa_record->Set(env->context(),
                  env->serial_string(),
                  Integer::NewFromUnsigned(
                      env->isolate(), soa_out->serial)).Check();
  soa_record->Set(env->context(),
                  env->refresh_string(),
                  Integer::New(env->isolate(), soa_out->refresh)).Check();
  soa_record->Set(env->context(),
                  env->retry_string(),
                  Integer::New(env->isolate(), soa_out->retry)).Check();
  soa_record->Set(env->context(),
                  env->expire_string(),
                  Integer::New(env->isolate(), soa_out->expire)).Check();
  soa_record->Set(env->context(),
                  env->minttl_string(),
                  Integer::NewFromUnsigned(
                      env->isolate(), soa_out->minttl)).Check();

  ares_free_data(soa_out);

  wrap->CallOnComplete(soa_record);
  return ARES_SUCCESS;
}

int ReverseTraits::Send(GetHostByAddrWrap* wrap, const char* name) {
  int length, family;
  char address_buffer[sizeof(struct in6_addr)];

  if (uv_inet_pton(AF_INET, name, &address_buffer) == 0) {
    length = sizeof(struct in_addr);
    family = AF_INET;
  } else if (uv_inet_pton(AF_INET6, name, &address_buffer) == 0) {
    length = sizeof(struct in6_addr);
    family = AF_INET6;
  } else {
    return UV_EINVAL;  // So errnoException() reports a proper error.
  }

  TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(
      TRACING_CATEGORY_NODE2(dns, native), "reverse", wrap,
      "name", TRACE_STR_COPY(name),
      "family", family == AF_INET ? "ipv4" : "ipv6");

  ares_gethostbyaddr(
      wrap->channel()->cares_channel(),
      address_buffer,
      length,
      family,
      GetHostByAddrWrap::Callback,
      wrap->MakeCallbackPointer());
  return ARES_SUCCESS;
}

int ReverseTraits::Parse(
    GetHostByAddrWrap* wrap,
    const std::unique_ptr<ResponseData>& response) {
  if (!response->is_host) [[unlikely]] {
    return ARES_EBADRESP;
  }
  struct hostent* host = response->host.get();

  Environment* env = wrap->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());
  wrap->CallOnComplete(HostentToNames(env, host));
  return ARES_SUCCESS;
}

namespace {
template <class Wrap>
static void Query(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  ChannelWrap* channel;
  ASSIGN_OR_RETURN_UNWRAP(&channel, args.This());

  CHECK_EQ(false, args.IsConstructCall());
  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsString());

  Local<Object> req_wrap_obj = args[0].As<Object>();
  Local<String> string = args[1].As<String>();
  auto wrap = std::make_unique<Wrap>(channel, req_wrap_obj);

  node::Utf8Value utf8name(env->isolate(), string);
  auto plain_name = utf8name.ToStringView();
  std::string name = ada::idna::to_ascii(plain_name);
  channel->ModifyActivityQueryCount(1);
  int err = wrap->Send(name.c_str());
  if (err) {
    channel->ModifyActivityQueryCount(-1);
  } else {
    // Release ownership of the pointer allowing the ownership to be transferred
    USE(wrap.release());
  }

  args.GetReturnValue().Set(err);
}


void AfterGetAddrInfo(uv_getaddrinfo_t* req, int status, struct addrinfo* res) {
  auto cleanup = OnScopeLeave([&]() { uv_freeaddrinfo(res); });
  BaseObjectPtr<GetAddrInfoReqWrap> req_wrap{
      static_cast<GetAddrInfoReqWrap*>(req->data)};
  Environment* env = req_wrap->env();

  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Value> argv[] = {
    Integer::New(env->isolate(), status),
    Null(env->isolate())
  };

  uint32_t n = 0;
  const uint8_t order = req_wrap->order();

  if (status == 0) {
    Local<Array> results = Array::New(env->isolate());

    auto add = [&](bool want_ipv4, bool want_ipv6) -> Maybe<void> {
      for (auto p = res; p != nullptr; p = p->ai_next) {
        CHECK_EQ(p->ai_socktype, SOCK_STREAM);

        const char* addr;
        if (want_ipv4 && p->ai_family == AF_INET) {
          addr = reinterpret_cast<char*>(
              &(reinterpret_cast<struct sockaddr_in*>(p->ai_addr)->sin_addr));
        } else if (want_ipv6 && p->ai_family == AF_INET6) {
          addr = reinterpret_cast<char*>(
              &(reinterpret_cast<struct sockaddr_in6*>(p->ai_addr)->sin6_addr));
        } else {
          continue;
        }

        char ip[INET6_ADDRSTRLEN];
        if (uv_inet_ntop(p->ai_family, addr, ip, sizeof(ip)))
          continue;

        Local<String> s = OneByteString(env->isolate(), ip);
        if (results->Set(env->context(), n, s).IsNothing())
          return Nothing<void>();
        n++;
      }
      return JustVoid();
    };

    switch (order) {
      case DNS_ORDER_IPV4_FIRST:
        if (add(true, false).IsNothing() || add(false, true).IsNothing())
          return;

        break;
      case DNS_ORDER_IPV6_FIRST:
        if (add(false, true).IsNothing() || add(true, false).IsNothing())
          return;

        break;
      default:
        if (add(true, true).IsNothing()) return;

        break;
    }

    // No responses were found to return
    if (n == 0) {
      argv[0] = Integer::New(env->isolate(), UV_EAI_NODATA);
    }

    argv[1] = results;
  }

  TRACE_EVENT_NESTABLE_ASYNC_END2(TRACING_CATEGORY_NODE2(dns, native),
                                  "lookup",
                                  req_wrap.get(),
                                  "count",
                                  n,
                                  "order",
                                  order);

  // Make the callback into JavaScript
  req_wrap->MakeCallback(env->oncomplete_string(), arraysize(argv), argv);
}


void AfterGetNameInfo(uv_getnameinfo_t* req,
                      int status,
                      const char* hostname,
                      const char* service) {
  BaseObjectPtr<GetNameInfoReqWrap> req_wrap{
      static_cast<GetNameInfoReqWrap*>(req->data)};
  Environment* env = req_wrap->env();

  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  Local<Value> argv[] = {
    Integer::New(env->isolate(), status),
    Null(env->isolate()),
    Null(env->isolate())
  };

  if (status == 0) {
    // Success
    Local<String> js_hostname = OneByteString(env->isolate(), hostname);
    Local<String> js_service = OneByteString(env->isolate(), service);
    argv[1] = js_hostname;
    argv[2] = js_service;
  }

  TRACE_EVENT_NESTABLE_ASYNC_END2(
      TRACING_CATEGORY_NODE2(dns, native), "lookupService", req_wrap.get(),
      "hostname", TRACE_STR_COPY(hostname),
      "service", TRACE_STR_COPY(service));

  // Make the callback into JavaScript
  req_wrap->MakeCallback(env->oncomplete_string(), arraysize(argv), argv);
}

void CanonicalizeIP(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  node::Utf8Value ip(isolate, args[0]);

  int af;
  unsigned char result[sizeof(ares_addr_port_node::addr)];
  if (uv_inet_pton(af = AF_INET, *ip, result) != 0 &&
      uv_inet_pton(af = AF_INET6, *ip, result) != 0)
    return;

  char canonical_ip[INET6_ADDRSTRLEN];
  CHECK_EQ(0, uv_inet_ntop(af, result, canonical_ip, sizeof(canonical_ip)));
  Local<String> val = String::NewFromUtf8(isolate, canonical_ip)
      .ToLocalChecked();
  args.GetReturnValue().Set(val);
}

void ConvertIpv6StringToBuffer(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  node::Utf8Value ip(isolate, args[0]);
  unsigned char dst[16];  // IPv6 addresses are 128 bits (16 bytes)

  if (uv_inet_pton(AF_INET6, *ip, dst) != 0) {
    isolate->ThrowException(Exception::Error(
        FIXED_ONE_BYTE_STRING(isolate, "Invalid IPv6 address")));
    return;
  }

  Local<Object> buffer;
  if (node::Buffer::Copy(
          isolate, reinterpret_cast<const char*>(dst), sizeof(dst))
          .ToLocal(&buffer)) {
    args.GetReturnValue().Set(buffer);
  }
}

void GetAddrInfo(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsString());
  CHECK(args[2]->IsInt32());
  CHECK(args[4]->IsUint32());
  Local<Object> req_wrap_obj = args[0].As<Object>();
  node::Utf8Value hostname(env->isolate(), args[1]);
  std::string ascii_hostname = ada::idna::to_ascii(hostname.ToStringView());

  int32_t flags = 0;
  if (args[3]->IsInt32()) {
    flags = args[3].As<Int32>()->Value();
  }

  int family;

  switch (args[2].As<Int32>()->Value()) {
    case 0:
      family = AF_UNSPEC;
      break;
    case 4:
      family = AF_INET;
      break;
    case 6:
      family = AF_INET6;
      break;
    default:
      UNREACHABLE("bad address family");
  }

  Local<Uint32> order = args[4].As<Uint32>();

  auto req_wrap =
      std::make_unique<GetAddrInfoReqWrap>(env, req_wrap_obj, order->Value());

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = family;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = flags;

  TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(TRACING_CATEGORY_NODE2(dns, native),
                                    "lookup",
                                    req_wrap.get(),
                                    "hostname",
                                    TRACE_STR_COPY(ascii_hostname.data()),
                                    "family",
                                    family == AF_INET    ? "ipv4"
                                    : family == AF_INET6 ? "ipv6"
                                                         : "unspec");

  int err = req_wrap->Dispatch(
      uv_getaddrinfo, AfterGetAddrInfo, ascii_hostname.data(), nullptr, &hints);
  if (err == 0)
    // Release ownership of the pointer allowing the ownership to be transferred
    USE(req_wrap.release());

  args.GetReturnValue().Set(err);
}


void GetNameInfo(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);

  CHECK(args[0]->IsObject());
  CHECK(args[1]->IsString());
  CHECK(args[2]->IsUint32());
  Local<Object> req_wrap_obj = args[0].As<Object>();
  node::Utf8Value ip(env->isolate(), args[1]);
  const unsigned port = args[2]->Uint32Value(env->context()).FromJust();
  struct sockaddr_storage addr;

  CHECK(uv_ip4_addr(*ip, port, reinterpret_cast<sockaddr_in*>(&addr)) == 0 ||
        uv_ip6_addr(*ip, port, reinterpret_cast<sockaddr_in6*>(&addr)) == 0);

  auto req_wrap = std::make_unique<GetNameInfoReqWrap>(env, req_wrap_obj);

  TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(
      TRACING_CATEGORY_NODE2(dns, native), "lookupService", req_wrap.get(),
      "ip", TRACE_STR_COPY(*ip), "port", port);

  int err = req_wrap->Dispatch(uv_getnameinfo,
                               AfterGetNameInfo,
                               reinterpret_cast<struct sockaddr*>(&addr),
                               NI_NAMEREQD);
  if (err == 0)
    // Release ownership of the pointer allowing the ownership to be transferred
    USE(req_wrap.release());

  args.GetReturnValue().Set(err);
}


void GetServers(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  ChannelWrap* channel;
  ASSIGN_OR_RETURN_UNWRAP(&channel, args.This());

  Local<Array> server_array = Array::New(env->isolate());

  ares_addr_port_node* servers;

  int r = ares_get_servers_ports(channel->cares_channel(), &servers);
  CHECK_EQ(r, ARES_SUCCESS);
  auto cleanup = OnScopeLeave([&]() { ares_free_data(servers); });

  ares_addr_port_node* cur = servers;

  for (uint32_t i = 0; cur != nullptr; ++i, cur = cur->next) {
    char ip[INET6_ADDRSTRLEN];

    const void* caddr = static_cast<const void*>(&cur->addr);
    int err = uv_inet_ntop(cur->family, caddr, ip, sizeof(ip));
    CHECK_EQ(err, 0);

    Local<Value> ret[] = {
      OneByteString(env->isolate(), ip),
      Integer::New(env->isolate(), cur->udp_port)
    };

    if (server_array->Set(env->context(), i,
                          Array::New(env->isolate(), ret, arraysize(ret)))
          .IsNothing()) {
      return;
    }
  }

  args.GetReturnValue().Set(server_array);
}


void SetServers(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  ChannelWrap* channel;
  ASSIGN_OR_RETURN_UNWRAP(&channel, args.This());

  if (channel->active_query_count()) {
    return args.GetReturnValue().Set(DNS_ESETSRVPENDING);
  }

  CHECK(args[0]->IsArray());

  Local<Array> arr = args[0].As<Array>();

  uint32_t len = arr->Length();

  if (len == 0) {
    int rv = ares_set_servers(channel->cares_channel(), nullptr);
    return args.GetReturnValue().Set(rv);
  }

  std::vector<ares_addr_port_node> servers(len);
  ares_addr_port_node* last = nullptr;

  int err;

  for (uint32_t i = 0; i < len; i++) {
    Local<Value> val;
    if (!arr->Get(env->context(), i).ToLocal(&val)) return;
    CHECK(val->IsArray());

    Local<Array> elm = val.As<Array>();

    Local<Value> familyValue;
    Local<Value> ipValue;
    Local<Value> portValue;

    if (!elm->Get(env->context(), 0).ToLocal(&familyValue)) return;
    if (!elm->Get(env->context(), 1).ToLocal(&ipValue)) return;
    if (!elm->Get(env->context(), 2).ToLocal(&portValue)) return;

    CHECK(familyValue->Int32Value(env->context()).FromJust());
    CHECK(ipValue->IsString());
    CHECK(portValue->Int32Value(env->context()).FromJust());

    int fam = familyValue->Int32Value(env->context()).FromJust();
    node::Utf8Value ip(env->isolate(), ipValue);
    int port = portValue->Int32Value(env->context()).FromJust();

    ares_addr_port_node* cur = &servers[i];

    cur->tcp_port = cur->udp_port = port;
    switch (fam) {
      case 4:
        cur->family = AF_INET;
        err = uv_inet_pton(AF_INET, *ip, &cur->addr);
        break;
      case 6:
        cur->family = AF_INET6;
        err = uv_inet_pton(AF_INET6, *ip, &cur->addr);
        break;
      default:
        UNREACHABLE("Bad address family");
    }

    if (err)
      break;

    cur->next = nullptr;

    if (last != nullptr)
      last->next = cur;

    last = cur;
  }

  if (err == 0)
    err = ares_set_servers_ports(channel->cares_channel(), servers.data());
  else
    err = ARES_EBADSTR;

  if (err == ARES_SUCCESS)
    channel->set_is_servers_default(false);

  args.GetReturnValue().Set(err);
}

void SetLocalAddress(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  ChannelWrap* channel;
  ASSIGN_OR_RETURN_UNWRAP(&channel, args.This());

  CHECK_EQ(args.Length(), 2);
  CHECK(args[0]->IsString());

  Isolate* isolate = args.GetIsolate();
  node::Utf8Value ip0(isolate, args[0]);

  unsigned char addr0[sizeof(struct in6_addr)];
  unsigned char addr1[sizeof(struct in6_addr)];
  int type0 = 0;

  // This function accepts 2 arguments.  The first may be either an IPv4
  // address or an IPv6 address.  If present, the second argument must be the
  // other type of address.  Otherwise, the unspecified type of IP is set
  // to 0 (any).

  if (uv_inet_pton(AF_INET, *ip0, &addr0) == 0) {
    ares_set_local_ip4(channel->cares_channel(), nbytes::ReadUint32BE(addr0));
    type0 = 4;
  } else if (uv_inet_pton(AF_INET6, *ip0, &addr0) == 0) {
    ares_set_local_ip6(channel->cares_channel(), addr0);
    type0 = 6;
  } else {
    THROW_ERR_INVALID_ARG_VALUE(env, "Invalid IP address.");
    return;
  }

  if (!args[1]->IsUndefined()) {
    CHECK(args[1]->IsString());
    node::Utf8Value ip1(isolate, args[1]);

    if (uv_inet_pton(AF_INET, *ip1, &addr1) == 0) {
      if (type0 == 4) {
        THROW_ERR_INVALID_ARG_VALUE(env, "Cannot specify two IPv4 addresses.");
        return;
      } else {
        ares_set_local_ip4(channel->cares_channel(),
                           nbytes::ReadUint32BE(addr1));
      }
    } else if (uv_inet_pton(AF_INET6, *ip1, &addr1) == 0) {
      if (type0 == 6) {
        THROW_ERR_INVALID_ARG_VALUE(env, "Cannot specify two IPv6 addresses.");
        return;
      } else {
        ares_set_local_ip6(channel->cares_channel(), addr1);
      }
    } else {
      THROW_ERR_INVALID_ARG_VALUE(env, "Invalid IP address.");
      return;
    }
  } else {
    // No second arg specified
    if (type0 == 4) {
      memset(&addr1, 0, sizeof(addr1));
      ares_set_local_ip6(channel->cares_channel(), addr1);
    } else {
      ares_set_local_ip4(channel->cares_channel(), 0);
    }
  }
}

void Cancel(const FunctionCallbackInfo<Value>& args) {
  ChannelWrap* channel;
  ASSIGN_OR_RETURN_UNWRAP(&channel, args.This());

  TRACE_EVENT_INSTANT0(TRACING_CATEGORY_NODE2(dns, native),
      "cancel", TRACE_EVENT_SCOPE_THREAD);

  ares_cancel(channel->cares_channel());
}

const char EMSG_ESETSRVPENDING[] = "There are pending queries.";
void StrError(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  int code = args[0]->Int32Value(env->context()).FromJust();
  const char* errmsg = (code == DNS_ESETSRVPENDING) ?
    EMSG_ESETSRVPENDING :
    ares_strerror(code);
  args.GetReturnValue().Set(OneByteString(env->isolate(), errmsg));
}

}  // namespace

inline void safe_free_hostent(struct hostent* host) {
  int idx;

  if (host->h_addr_list != nullptr) {
    idx = 0;
    while (host->h_addr_list[idx]) {
      free(host->h_addr_list[idx++]);
    }
    free(host->h_addr_list);
    host->h_addr_list = nullptr;
  }

  if (host->h_aliases != nullptr) {
    idx = 0;
    while (host->h_aliases[idx]) {
      free(host->h_aliases[idx++]);
    }
    free(host->h_aliases);
    host->h_aliases = nullptr;
  }

  free(host->h_name);
  free(host);
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();

  SetMethod(context, target, "getaddrinfo", GetAddrInfo);
  SetMethod(context, target, "getnameinfo", GetNameInfo);
  SetMethodNoSideEffect(context, target, "canonicalizeIP", CanonicalizeIP);
  SetMethodNoSideEffect(
      context, target, "convertIpv6StringToBuffer", ConvertIpv6StringToBuffer);

  SetMethod(context, target, "strerror", StrError);

  target->Set(env->context(), FIXED_ONE_BYTE_STRING(env->isolate(), "AF_INET"),
              Integer::New(env->isolate(), AF_INET)).Check();
  target->Set(env->context(), FIXED_ONE_BYTE_STRING(env->isolate(), "AF_INET6"),
              Integer::New(env->isolate(), AF_INET6)).Check();
  target->Set(env->context(), FIXED_ONE_BYTE_STRING(env->isolate(),
                                                    "AF_UNSPEC"),
              Integer::New(env->isolate(), AF_UNSPEC)).Check();
  target->Set(env->context(), FIXED_ONE_BYTE_STRING(env->isolate(),
                                                    "AI_ADDRCONFIG"),
              Integer::New(env->isolate(), AI_ADDRCONFIG)).Check();
  target->Set(env->context(), FIXED_ONE_BYTE_STRING(env->isolate(),
                                                    "AI_ALL"),
              Integer::New(env->isolate(), AI_ALL)).Check();
  target->Set(env->context(), FIXED_ONE_BYTE_STRING(env->isolate(),
                                                    "AI_V4MAPPED"),
              Integer::New(env->isolate(), AI_V4MAPPED)).Check();
  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(env->isolate(), "DNS_ORDER_VERBATIM"),
            Integer::New(env->isolate(), DNS_ORDER_VERBATIM))
      .Check();
  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(env->isolate(), "DNS_ORDER_IPV4_FIRST"),
            Integer::New(env->isolate(), DNS_ORDER_IPV4_FIRST))
      .Check();
  target
      ->Set(env->context(),
            FIXED_ONE_BYTE_STRING(env->isolate(), "DNS_ORDER_IPV6_FIRST"),
            Integer::New(env->isolate(), DNS_ORDER_IPV6_FIRST))
      .Check();

  Local<FunctionTemplate> aiw =
      BaseObject::MakeLazilyInitializedJSTemplate(env);
  aiw->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetConstructorFunction(context, target, "GetAddrInfoReqWrap", aiw);

  Local<FunctionTemplate> niw =
      BaseObject::MakeLazilyInitializedJSTemplate(env);
  niw->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetConstructorFunction(context, target, "GetNameInfoReqWrap", niw);

  Local<FunctionTemplate> qrw =
      BaseObject::MakeLazilyInitializedJSTemplate(env);
  qrw->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetConstructorFunction(context, target, "QueryReqWrap", qrw);

  Local<FunctionTemplate> channel_wrap =
      NewFunctionTemplate(isolate, ChannelWrap::New);
  channel_wrap->InstanceTemplate()->SetInternalFieldCount(
      ChannelWrap::kInternalFieldCount);
  channel_wrap->Inherit(AsyncWrap::GetConstructorTemplate(env));

  SetProtoMethod(isolate, channel_wrap, "queryAny", Query<QueryAnyWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryA", Query<QueryAWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryAaaa", Query<QueryAaaaWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryCaa", Query<QueryCaaWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryCname", Query<QueryCnameWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryMx", Query<QueryMxWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryNs", Query<QueryNsWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryTxt", Query<QueryTxtWrap>);
  SetProtoMethod(isolate, channel_wrap, "querySrv", Query<QuerySrvWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryPtr", Query<QueryPtrWrap>);
  SetProtoMethod(isolate, channel_wrap, "queryNaptr", Query<QueryNaptrWrap>);
  SetProtoMethod(isolate, channel_wrap, "querySoa", Query<QuerySoaWrap>);
  SetProtoMethod(
      isolate, channel_wrap, "getHostByAddr", Query<GetHostByAddrWrap>);

  SetProtoMethodNoSideEffect(isolate, channel_wrap, "getServers", GetServers);
  SetProtoMethod(isolate, channel_wrap, "setServers", SetServers);
  SetProtoMethod(isolate, channel_wrap, "setLocalAddress", SetLocalAddress);
  SetProtoMethod(isolate, channel_wrap, "cancel", Cancel);

  SetConstructorFunction(context, target, "ChannelWrap", channel_wrap);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(GetAddrInfo);
  registry->Register(GetNameInfo);
  registry->Register(CanonicalizeIP);
  registry->Register(ConvertIpv6StringToBuffer);
  registry->Register(StrError);
  registry->Register(ChannelWrap::New);

  registry->Register(Query<QueryAnyWrap>);
  registry->Register(Query<QueryAWrap>);
  registry->Register(Query<QueryAaaaWrap>);
  registry->Register(Query<QueryCaaWrap>);
  registry->Register(Query<QueryCnameWrap>);
  registry->Register(Query<QueryMxWrap>);
  registry->Register(Query<QueryNsWrap>);
  registry->Register(Query<QueryTxtWrap>);
  registry->Register(Query<QuerySrvWrap>);
  registry->Register(Query<QueryPtrWrap>);
  registry->Register(Query<QueryNaptrWrap>);
  registry->Register(Query<QuerySoaWrap>);
  registry->Register(Query<GetHostByAddrWrap>);

  registry->Register(GetServers);
  registry->Register(SetServers);
  registry->Register(SetLocalAddress);
  registry->Register(Cancel);
}

}  // namespace cares_wrap
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(cares_wrap, node::cares_wrap::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(cares_wrap,
                                node::cares_wrap::RegisterExternalReferences)
