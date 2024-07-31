#ifndef SRC_CARES_WRAP_H_
#define SRC_CARES_WRAP_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#define CARES_STATICLIB

#include "async_wrap.h"
#include "base_object.h"
#include "env.h"
#include "memory_tracker.h"
#include "node.h"
#include "node_internals.h"
#include "util.h"

#include "ares.h"
#include "v8.h"
#include "uv.h"

#include <unordered_set>

#ifdef __POSIX__
# include <netdb.h>
#endif  // __POSIX__

# include <ares_nameser.h>

namespace node {
namespace cares_wrap {

constexpr int ns_t_cname_or_a = -1;
constexpr int DNS_ESETSRVPENDING = -1000;
constexpr uint8_t DNS_ORDER_VERBATIM = 0;
constexpr uint8_t DNS_ORDER_IPV4_FIRST = 1;
constexpr uint8_t DNS_ORDER_IPV6_FIRST = 2;

class ChannelWrap;

inline void safe_free_hostent(struct hostent* host);

using HostEntPointer = DeleteFnPtr<hostent, ares_free_hostent>;
using SafeHostEntPointer = DeleteFnPtr<hostent, safe_free_hostent>;

inline const char* ToErrorCodeString(int status) {
  switch (status) {
#define V(code) case ARES_##code: return #code;
    V(EADDRGETNETWORKPARAMS)
    V(EBADFAMILY)
    V(EBADFLAGS)
    V(EBADHINTS)
    V(EBADNAME)
    V(EBADQUERY)
    V(EBADRESP)
    V(EBADSTR)
    V(ECANCELLED)
    V(ECONNREFUSED)
    V(EDESTRUCTION)
    V(EFILE)
    V(EFORMERR)
    V(ELOADIPHLPAPI)
    V(ENODATA)
    V(ENOMEM)
    V(ENONAME)
    V(ENOTFOUND)
    V(ENOTIMP)
    V(ENOTINITIALIZED)
    V(EOF)
    V(EREFUSED)
    V(ESERVFAIL)
    V(ETIMEOUT)
#undef V
  }

  return "UNKNOWN_ARES_ERROR";
}

inline void cares_wrap_hostent_cpy(
    struct hostent* dest,
    const struct hostent* src) {
  dest->h_addr_list = nullptr;
  dest->h_addrtype = 0;
  dest->h_aliases = nullptr;
  dest->h_length = 0;
  dest->h_name = nullptr;

  /* copy `h_name` */
  size_t name_size = strlen(src->h_name) + 1;
  dest->h_name = node::Malloc<char>(name_size);
  memcpy(dest->h_name, src->h_name, name_size);

  /* copy `h_aliases` */
  size_t alias_count;
  for (alias_count = 0;
      src->h_aliases[alias_count] != nullptr;
      alias_count++) {
  }

  dest->h_aliases = node::Malloc<char*>(alias_count + 1);
  for (size_t i = 0; i < alias_count; i++) {
    const size_t cur_alias_size = strlen(src->h_aliases[i]) + 1;
    dest->h_aliases[i] = node::Malloc(cur_alias_size);
    memcpy(dest->h_aliases[i], src->h_aliases[i], cur_alias_size);
  }
  dest->h_aliases[alias_count] = nullptr;

  /* copy `h_addr_list` */
  size_t list_count;
  for (list_count = 0;
      src->h_addr_list[list_count] != nullptr;
      list_count++) {
  }

  dest->h_addr_list = node::Malloc<char*>(list_count + 1);
  for (size_t i = 0; i < list_count; i++) {
    dest->h_addr_list[i] = node::Malloc(src->h_length);
    memcpy(dest->h_addr_list[i], src->h_addr_list[i], src->h_length);
  }
  dest->h_addr_list[list_count] = nullptr;

  /* work after work */
  dest->h_length = src->h_length;
  dest->h_addrtype = src->h_addrtype;
}


struct NodeAresTask final : public MemoryRetainer {
  ChannelWrap* channel;
  ares_socket_t sock;
  uv_poll_t poll_watcher;

  inline void MemoryInfo(MemoryTracker* trakcer) const override;
  SET_MEMORY_INFO_NAME(NodeAresTask)
  SET_SELF_SIZE(NodeAresTask)

  struct Hash {
    inline size_t operator()(NodeAresTask* a) const {
      return std::hash<ares_socket_t>()(a->sock);
    }
  };

  struct Equal {
    inline bool operator()(NodeAresTask* a, NodeAresTask* b) const {
      return a->sock == b->sock;
    }
  };

  static NodeAresTask* Create(ChannelWrap* channel, ares_socket_t sock);

  using List = std::unordered_set<NodeAresTask*, Hash, Equal>;
};

class ChannelWrap final : public AsyncWrap {
 public:
  ChannelWrap(
      Environment* env,
      v8::Local<v8::Object> object,
      int timeout,
      int tries);
  ~ChannelWrap() override;

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  void Setup();
  void EnsureServers();
  void StartTimer();
  void CloseTimer();

  void ModifyActivityQueryCount(int count);

  inline uv_timer_t* timer_handle() { return timer_handle_; }
  inline ares_channel cares_channel() { return channel_; }
  inline void set_query_last_ok(bool ok) { query_last_ok_ = ok; }
  inline void set_is_servers_default(bool is_default) {
    is_servers_default_ = is_default;
  }
  inline int active_query_count() { return active_query_count_; }
  inline NodeAresTask::List* task_list() { return &task_list_; }

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(ChannelWrap)
  SET_SELF_SIZE(ChannelWrap)

  static void AresTimeout(uv_timer_t* handle);

 private:
  uv_timer_t* timer_handle_ = nullptr;
  ares_channel channel_ = nullptr;
  bool query_last_ok_ = true;
  bool is_servers_default_ = true;
  bool library_inited_ = false;
  int timeout_;
  int tries_;
  int active_query_count_ = 0;
  NodeAresTask::List task_list_;
};

class GetAddrInfoReqWrap final : public ReqWrap<uv_getaddrinfo_t> {
 public:
  GetAddrInfoReqWrap(Environment* env,
                     v8::Local<v8::Object> req_wrap_obj,
                     uint8_t order);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(GetAddrInfoReqWrap)
  SET_SELF_SIZE(GetAddrInfoReqWrap)

  uint8_t order() const { return order_; }

 private:
  const uint8_t order_;
};

class GetNameInfoReqWrap final : public ReqWrap<uv_getnameinfo_t> {
 public:
  GetNameInfoReqWrap(Environment* env, v8::Local<v8::Object> req_wrap_obj);

  SET_NO_MEMORY_INFO()
  SET_MEMORY_INFO_NAME(GetNameInfoReqWrap)
  SET_SELF_SIZE(GetNameInfoReqWrap)
};

struct ResponseData final {
  int status;
  bool is_host;
  SafeHostEntPointer host;
  MallocedBuffer<unsigned char> buf;
};

template <typename Traits>
class QueryWrap final : public AsyncWrap {
 public:
  QueryWrap(ChannelWrap* channel, v8::Local<v8::Object> req_wrap_obj)
      : AsyncWrap(channel->env(), req_wrap_obj, AsyncWrap::PROVIDER_QUERYWRAP),
        channel_(channel),
        trace_name_(Traits::name) {}

  ~QueryWrap() {
    CHECK_EQ(false, persistent().IsEmpty());

    // Let Callback() know that this object no longer exists.
    if (callback_ptr_ != nullptr)
      *callback_ptr_ = nullptr;
  }

  int Send(const char* name) {
    return Traits::Send(this, name);
  }

  void AresQuery(const char* name, int dnsclass, int type) {
    channel_->EnsureServers();
    TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(
      TRACING_CATEGORY_NODE2(dns, native), trace_name_, this,
      "name", TRACE_STR_COPY(name));
    ares_query(
        channel_->cares_channel(),
        name,
        dnsclass,
        type,
        Callback,
        MakeCallbackPointer());
  }

  void ParseError(int status) {
    CHECK_NE(status, ARES_SUCCESS);
    v8::HandleScope handle_scope(env()->isolate());
    v8::Context::Scope context_scope(env()->context());
    const char* code = ToErrorCodeString(status);
    v8::Local<v8::Value> arg = OneByteString(env()->isolate(), code);
    TRACE_EVENT_NESTABLE_ASYNC_END1(
        TRACING_CATEGORY_NODE2(dns, native), trace_name_, this,
        "error", status);
    MakeCallback(env()->oncomplete_string(), 1, &arg);
  }

  const BaseObjectPtr<ChannelWrap>& channel() const { return channel_; }

  void AfterResponse() {
    CHECK(response_data_);

    int status = response_data_->status;

    if (status != ARES_SUCCESS)
      return ParseError(status);

    status = Traits::Parse(this, response_data_);

    if (status != ARES_SUCCESS)
      ParseError(status);
  }

  void* MakeCallbackPointer() {
    CHECK_NULL(callback_ptr_);
    callback_ptr_ = new QueryWrap<Traits>*(this);
    return callback_ptr_;
  }

  static QueryWrap<Traits>* FromCallbackPointer(void* arg) {
    std::unique_ptr<QueryWrap<Traits>*> wrap_ptr {
        static_cast<QueryWrap<Traits>**>(arg)
    };
    QueryWrap<Traits>* wrap = *wrap_ptr.get();
    if (wrap == nullptr) return nullptr;
    wrap->callback_ptr_ = nullptr;
    return wrap;
  }

  static void Callback(
      void* arg,
      int status,
      int timeouts,
      unsigned char* answer_buf,
      int answer_len) {
    QueryWrap<Traits>* wrap = FromCallbackPointer(arg);
    if (wrap == nullptr) return;

    unsigned char* buf_copy = nullptr;
    if (status == ARES_SUCCESS) {
      buf_copy = node::Malloc<unsigned char>(answer_len);
      memcpy(buf_copy, answer_buf, answer_len);
    }

    wrap->response_data_ = std::make_unique<ResponseData>();
    ResponseData* data = wrap->response_data_.get();
    data->status = status;
    data->is_host = false;
    data->buf = MallocedBuffer<unsigned char>(buf_copy, answer_len);

    wrap->QueueResponseCallback(status);
  }

  static void Callback(
      void* arg,
      int status,
      int timeouts,
      struct hostent* host) {
    QueryWrap<Traits>* wrap = FromCallbackPointer(arg);
    if (wrap == nullptr) return;

    struct hostent* host_copy = nullptr;
    if (status == ARES_SUCCESS) {
      host_copy = node::Malloc<hostent>(1);
      cares_wrap_hostent_cpy(host_copy, host);
    }

    wrap->response_data_ = std::make_unique<ResponseData>();
    ResponseData* data = wrap->response_data_.get();
    data->status = status;
    data->host.reset(host_copy);
    data->is_host = true;

    wrap->QueueResponseCallback(status);
  }

  void QueueResponseCallback(int status) {
    BaseObjectPtr<QueryWrap<Traits>> strong_ref{this};
    env()->SetImmediate([this, strong_ref](Environment*) {
      AfterResponse();

      // Delete once strong_ref goes out of scope.
      Detach();
    });

    channel_->set_query_last_ok(status != ARES_ECONNREFUSED);
    channel_->ModifyActivityQueryCount(-1);
  }

  void CallOnComplete(
      v8::Local<v8::Value> answer,
      v8::Local<v8::Value> extra = v8::Local<v8::Value>()) {
    v8::HandleScope handle_scope(env()->isolate());
    v8::Context::Scope context_scope(env()->context());
    v8::Local<v8::Value> argv[] = {
      v8::Integer::New(env()->isolate(), 0),
      answer,
      extra
    };
    const int argc = arraysize(argv) - extra.IsEmpty();
    TRACE_EVENT_NESTABLE_ASYNC_END0(
        TRACING_CATEGORY_NODE2(dns, native), trace_name_, this);

    MakeCallback(env()->oncomplete_string(), argc, argv);
  }

  void MemoryInfo(MemoryTracker* tracker) const override {
    tracker->TrackField("channel", channel_);
    if (response_data_) {
      tracker->TrackFieldWithSize("response", response_data_->buf.size);
    }
  }

  SET_MEMORY_INFO_NAME(QueryWrap)
  SET_SELF_SIZE(QueryWrap<Traits>)

 private:
  BaseObjectPtr<ChannelWrap> channel_;

  std::unique_ptr<ResponseData> response_data_;
  const char* trace_name_;
  // Pointer to pointer to 'this' that can be reset from the destructor,
  // in order to let Callback() know that 'this' no longer exists.
  QueryWrap<Traits>** callback_ptr_ = nullptr;
};

struct AnyTraits final {
  static constexpr const char* name = "resolveAny";
  static int Send(QueryWrap<AnyTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<AnyTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct ATraits final {
  static constexpr const char* name = "resolve4";
  static int Send(QueryWrap<ATraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<ATraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct AaaaTraits final {
  static constexpr const char* name = "resolve6";
  static int Send(QueryWrap<AaaaTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<AaaaTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct CaaTraits final {
  static constexpr const char* name = "resolveCaa";
  static int Send(QueryWrap<CaaTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<CaaTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct CnameTraits final {
  static constexpr const char* name = "resolveCname";
  static int Send(QueryWrap<CnameTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<CnameTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct MxTraits final {
  static constexpr const char* name = "resolveMx";
  static int Send(QueryWrap<MxTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<MxTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct NsTraits final {
  static constexpr const char* name = "resolveNs";
  static int Send(QueryWrap<NsTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<NsTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct TxtTraits final {
  static constexpr const char* name = "resolveTxt";
  static int Send(QueryWrap<TxtTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<TxtTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct SrvTraits final {
  static constexpr const char* name = "resolveSrv";
  static int Send(QueryWrap<SrvTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<SrvTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct PtrTraits final {
  static constexpr const char* name = "resolvePtr";
  static int Send(QueryWrap<PtrTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<PtrTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct NaptrTraits final {
  static constexpr const char* name = "resolveNaptr";
  static int Send(QueryWrap<NaptrTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<NaptrTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct SoaTraits final {
  static constexpr const char* name = "resolveSoa";
  static int Send(QueryWrap<SoaTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<SoaTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

struct ReverseTraits final {
  static constexpr const char* name = "reverse";
  static int Send(QueryWrap<ReverseTraits>* wrap, const char* name);
  static int Parse(
      QueryWrap<ReverseTraits>* wrap,
      const std::unique_ptr<ResponseData>& response);
};

using QueryAnyWrap = QueryWrap<AnyTraits>;
using QueryAWrap = QueryWrap<ATraits>;
using QueryAaaaWrap = QueryWrap<AaaaTraits>;
using QueryCaaWrap = QueryWrap<CaaTraits>;
using QueryCnameWrap = QueryWrap<CnameTraits>;
using QueryMxWrap = QueryWrap<MxTraits>;
using QueryNsWrap = QueryWrap<NsTraits>;
using QueryTxtWrap = QueryWrap<TxtTraits>;
using QuerySrvWrap = QueryWrap<SrvTraits>;
using QueryPtrWrap = QueryWrap<PtrTraits>;
using QueryNaptrWrap = QueryWrap<NaptrTraits>;
using QuerySoaWrap = QueryWrap<SoaTraits>;
using GetHostByAddrWrap = QueryWrap<ReverseTraits>;

}  // namespace cares_wrap
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_CARES_WRAP_H_
