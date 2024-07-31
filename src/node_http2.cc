#include "node_http2.h"
#include "aliased_buffer-inl.h"
#include "aliased_struct-inl.h"
#include "debug_utils-inl.h"
#include "histogram-inl.h"
#include "memory_tracker-inl.h"
#include "node.h"
#include "node_buffer.h"
#include "node_http_common-inl.h"
#include "node_mem-inl.h"
#include "node_perf.h"
#include "node_revert.h"
#include "stream_base-inl.h"
#include "util-inl.h"

#include "nbytes.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace node {

using v8::Array;
using v8::ArrayBuffer;
using v8::ArrayBufferView;
using v8::BackingStore;
using v8::Boolean;
using v8::Context;
using v8::EscapableHandleScope;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::NewStringType;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Uint8Array;
using v8::Undefined;
using v8::Value;

namespace http2 {

namespace {

const char zero_bytes_256[256] = {};

bool HasHttp2Observer(Environment* env) {
  AliasedUint32Array& observers = env->performance_state()->observers;
  return observers[performance::NODE_PERFORMANCE_ENTRY_TYPE_HTTP2] != 0;
}

}  // anonymous namespace

// These configure the callbacks required by nghttp2 itself. There are
// two sets of callback functions, one that is used if a padding callback
// is set, and other that does not include the padding callback.
const Http2Session::Callbacks Http2Session::callback_struct_saved[2] = {
    Callbacks(false),
    Callbacks(true)};

// The Http2Scope object is used to queue a write to the i/o stream. It is
// used whenever any action is take on the underlying nghttp2 API that may
// push data into nghttp2 outbound data queue.
//
// For example:
//
// Http2Scope h2scope(session);
// nghttp2_submit_ping(session->session(), ... );
//
// When the Http2Scope passes out of scope and is deconstructed, it will
// call Http2Session::MaybeScheduleWrite().
Http2Scope::Http2Scope(Http2Stream* stream) : Http2Scope(stream->session()) {}

Http2Scope::Http2Scope(Http2Session* session) : session_(session) {
  if (!session_) return;

  // If there is another scope further below on the stack, or
  // a write is already scheduled, there's nothing to do.
  if (session_->is_in_scope() || session_->is_write_scheduled()) {
    session_.reset();
    return;
  }
  session_->set_in_scope();
}

Http2Scope::~Http2Scope() {
  if (!session_) return;
  session_->set_in_scope(false);
  if (!session_->is_write_scheduled())
    session_->MaybeScheduleWrite();
}

// The Http2Options object is used during the construction of Http2Session
// instances to configure an appropriate nghttp2_options struct. The class
// uses a single TypedArray instance that is shared with the JavaScript side
// to more efficiently pass values back and forth.
Http2Options::Http2Options(Http2State* http2_state, SessionType type) {
  nghttp2_option* option;
  CHECK_EQ(nghttp2_option_new(&option), 0);
  CHECK_NOT_NULL(option);
  options_.reset(option);

  // Make sure closed connections aren't kept around, taking up memory.
  // Note that this breaks the priority tree, which we don't use.
  nghttp2_option_set_no_closed_streams(option, 1);

  // We manually handle flow control within a session in order to
  // implement backpressure -- that is, we only send WINDOW_UPDATE
  // frames to the remote peer as data is actually consumed by user
  // code. This ensures that the flow of data over the connection
  // does not move too quickly and limits the amount of data we
  // are required to buffer.
  nghttp2_option_set_no_auto_window_update(option, 1);

  // Enable built in support for receiving ALTSVC and ORIGIN frames (but
  // only on client side sessions
  if (type == NGHTTP2_SESSION_CLIENT) {
    nghttp2_option_set_builtin_recv_extension_type(option, NGHTTP2_ALTSVC);
    nghttp2_option_set_builtin_recv_extension_type(option, NGHTTP2_ORIGIN);
  }

  AliasedUint32Array& buffer = http2_state->options_buffer;
  uint32_t flags = buffer[IDX_OPTIONS_FLAGS];

  if (flags & (1 << IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE)) {
    nghttp2_option_set_max_deflate_dynamic_table_size(
        option,
        buffer[IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE]);
  }

  if (flags & (1 << IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS)) {
    nghttp2_option_set_max_reserved_remote_streams(
        option,
        buffer[IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS]);
  }

  if (flags & (1 << IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH)) {
    nghttp2_option_set_max_send_header_block_length(
        option,
        buffer[IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH]);
  }

  // Recommended default
  nghttp2_option_set_peer_max_concurrent_streams(option, 100);
  if (flags & (1 << IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS)) {
    nghttp2_option_set_peer_max_concurrent_streams(
        option,
        buffer[IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS]);
  }

  // The padding strategy sets the mechanism by which we determine how much
  // additional frame padding to apply to DATA and HEADERS frames. Currently
  // this is set on a per-session basis, but eventually we may switch to
  // a per-stream setting, giving users greater control
  if (flags & (1 << IDX_OPTIONS_PADDING_STRATEGY)) {
    PaddingStrategy strategy =
        static_cast<PaddingStrategy>(
            buffer.GetValue(IDX_OPTIONS_PADDING_STRATEGY));
    set_padding_strategy(strategy);
  }

  // The max header list pairs option controls the maximum number of
  // header pairs the session may accept. This is a hard limit.. that is,
  // if the remote peer sends more than this amount, the stream will be
  // automatically closed with an RST_STREAM.
  if (flags & (1 << IDX_OPTIONS_MAX_HEADER_LIST_PAIRS))
    set_max_header_pairs(buffer[IDX_OPTIONS_MAX_HEADER_LIST_PAIRS]);

  // The HTTP2 specification places no limits on the number of HTTP2
  // PING frames that can be sent. In order to prevent PINGS from being
  // abused as an attack vector, however, we place a strict upper limit
  // on the number of unacknowledged PINGS that can be sent at any given
  // time.
  if (flags & (1 << IDX_OPTIONS_MAX_OUTSTANDING_PINGS))
    set_max_outstanding_pings(buffer[IDX_OPTIONS_MAX_OUTSTANDING_PINGS]);

  // The HTTP2 specification places no limits on the number of HTTP2
  // SETTINGS frames that can be sent. In order to prevent PINGS from being
  // abused as an attack vector, however, we place a strict upper limit
  // on the number of unacknowledged SETTINGS that can be sent at any given
  // time.
  if (flags & (1 << IDX_OPTIONS_MAX_OUTSTANDING_SETTINGS))
    set_max_outstanding_settings(buffer[IDX_OPTIONS_MAX_OUTSTANDING_SETTINGS]);

  // The HTTP2 specification places no limits on the amount of memory
  // that a session can consume. In order to prevent abuse, we place a
  // cap on the amount of memory a session can consume at any given time.
  // this is a credit based system. Existing streams may cause the limit
  // to be temporarily exceeded but once over the limit, new streams cannot
  // created.
  // Important: The maxSessionMemory option in javascript is expressed in
  //            terms of MB increments (i.e. the value 1 == 1 MB)
  if (flags & (1 << IDX_OPTIONS_MAX_SESSION_MEMORY))
    set_max_session_memory(buffer[IDX_OPTIONS_MAX_SESSION_MEMORY] *
                           static_cast<uint64_t>(1000000));

  if (flags & (1 << IDX_OPTIONS_MAX_SETTINGS)) {
    nghttp2_option_set_max_settings(
        option,
        static_cast<size_t>(buffer[IDX_OPTIONS_MAX_SETTINGS]));
  }
}

#define GRABSETTING(entries, count, name)                                      \
  do {                                                                         \
    if (flags & (1 << IDX_SETTINGS_ ## name)) {                                \
      uint32_t val = buffer[IDX_SETTINGS_ ## name];                            \
      entries[count++] =                                                       \
          nghttp2_settings_entry {NGHTTP2_SETTINGS_ ## name, val};             \
    } } while (0)

size_t Http2Settings::Init(
    Http2State* http2_state,
    nghttp2_settings_entry* entries) {
  AliasedUint32Array& buffer = http2_state->settings_buffer;
  uint32_t flags = buffer[IDX_SETTINGS_COUNT];

  size_t count = 0;

#define V(name) GRABSETTING(entries, count, name);
  HTTP2_SETTINGS(V)
#undef V
  uint32_t numAddSettings = buffer[IDX_SETTINGS_COUNT + 1];
  if (numAddSettings > 0) {
    uint32_t offset = IDX_SETTINGS_COUNT + 1 + 1;
    for (uint32_t i = 0; i < numAddSettings; i++) {
      uint32_t key = buffer[offset + i * 2 + 0];
      uint32_t val = buffer[offset + i * 2 + 1];
      entries[count++] = nghttp2_settings_entry{(int32_t)key, val};
    }
  }

  return count;
}
#undef GRABSETTING

// The Http2Settings class is used to configure a SETTINGS frame that is
// to be sent to the connected peer. The settings are set using a TypedArray
// that is shared with the JavaScript side.
Http2Settings::Http2Settings(Http2Session* session,
                             Local<Object> obj,
                             Local<Function> callback,
                             uint64_t start_time)
    : AsyncWrap(session->env(), obj, PROVIDER_HTTP2SETTINGS),
      session_(session),
      startTime_(start_time) {
  callback_.Reset(env()->isolate(), callback);
  count_ = Init(session->http2_state(), entries_);
}

Local<Function> Http2Settings::callback() const {
  return callback_.Get(env()->isolate());
}

void Http2Settings::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("callback", callback_);
}

// Generates a Buffer that contains the serialized payload of a SETTINGS
// frame. This can be used, for instance, to create the Base64-encoded
// content of an Http2-Settings header field.
Local<Value> Http2Settings::Pack() {
  return Pack(session_->env(), count_, entries_);
}

Local<Value> Http2Settings::Pack(Http2State* state) {
  nghttp2_settings_entry entries[IDX_SETTINGS_COUNT + MAX_ADDITIONAL_SETTINGS];
  size_t count = Init(state, entries);
  return Pack(state->env(), count, entries);
}

Local<Value> Http2Settings::Pack(
    Environment* env,
    size_t count,
    const nghttp2_settings_entry* entries) {
  EscapableHandleScope scope(env->isolate());
  std::unique_ptr<BackingStore> bs;
  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs = ArrayBuffer::NewBackingStore(env->isolate(), count * 6);
  }
  if (nghttp2_pack_settings_payload(static_cast<uint8_t*>(bs->Data()),
                                    bs->ByteLength(),
                                    entries,
                                    count) < 0) {
    return scope.Escape(Undefined(env->isolate()));
  }
  Local<ArrayBuffer> ab = ArrayBuffer::New(env->isolate(), std::move(bs));
  return scope.Escape(Buffer::New(env, ab, 0, ab->ByteLength())
                          .FromMaybe(Local<Value>()));
}

// Updates the shared TypedArray with the current remote or local settings for
// the session.
void Http2Settings::Update(Http2Session* session, get_setting fn, bool local) {
  AliasedUint32Array& buffer = session->http2_state()->settings_buffer;

#define V(name)                                                                \
  buffer[IDX_SETTINGS_ ## name] =                                              \
      fn(session->session(), NGHTTP2_SETTINGS_ ## name);
  HTTP2_SETTINGS(V)
#undef V
  struct Http2Session::custom_settings_state& custom_settings =
      session->custom_settings(local);
  uint32_t count = 0;
  size_t imax = std::min(custom_settings.number, MAX_ADDITIONAL_SETTINGS);
  for (size_t i = 0; i < imax; i++) {
    // We flag unset the settings with a bit above the allowed range
    if (!(custom_settings.entries[i].settings_id & (~0xffff))) {
      uint32_t settings_id =
          (uint32_t)(custom_settings.entries[i].settings_id & 0xffff);
      size_t j = 0;
      while (j < count) {
        if ((buffer[IDX_SETTINGS_COUNT + 1 + j * 2 + 1] & 0xffff) ==
            settings_id) {
          buffer[IDX_SETTINGS_COUNT + 1 + j * 2 + 1] = settings_id;
          buffer[IDX_SETTINGS_COUNT + 1 + j * 2 + 2] =
              custom_settings.entries[i].value;
          break;
        }
        j++;
      }
      if (j == count && count < MAX_ADDITIONAL_SETTINGS) {
        buffer[IDX_SETTINGS_COUNT + 1 + count * 2 + 1] = settings_id;
        buffer[IDX_SETTINGS_COUNT + 1 + count * 2 + 2] =
            custom_settings.entries[i].value;
        count++;
      }
    }
    // Comment for code review,
    // one might also set the javascript object with an undefined value
  }
  buffer[IDX_SETTINGS_COUNT + 1] = count;
}

// Initializes the shared TypedArray with the default settings values.
void Http2Settings::RefreshDefaults(Http2State* http2_state) {
  AliasedUint32Array& buffer = http2_state->settings_buffer;
  uint32_t flags = 0;

#define V(name)                                                            \
  do {                                                                     \
    buffer[IDX_SETTINGS_ ## name] = DEFAULT_SETTINGS_ ## name;             \
    flags |= 1 << IDX_SETTINGS_ ## name;                                   \
  } while (0);
  HTTP2_SETTINGS(V)
#undef V

  buffer[IDX_SETTINGS_COUNT] = flags;
  buffer[IDX_SETTINGS_COUNT + 1] = 0;  // no additional settings
}


void Http2Settings::Send() {
  Http2Scope h2scope(session_.get());

  // We have to update the local custom settings
  session_->UpdateLocalCustomSettings(count_, &entries_[0]);
  CHECK_EQ(nghttp2_submit_settings(
      session_->session(),
      NGHTTP2_FLAG_NONE,
      &entries_[0],
      count_), 0);
}

void Http2Session::UpdateLocalCustomSettings(size_t count,
                                             nghttp2_settings_entry* entries) {
  size_t number = local_custom_settings_.number;
  for (size_t i = 0; i < count; ++i) {
    nghttp2_settings_entry& s_entry = entries[i];
    if (s_entry.settings_id >= IDX_SETTINGS_COUNT) {
      // look if already included
      size_t j = 0;
      while (j < number) {
        nghttp2_settings_entry& d_entry = local_custom_settings_.entries[j];
        if (d_entry.settings_id == s_entry.settings_id) {
          d_entry.value = s_entry.value;
          break;
        }
        j++;
      }
      if (j == number && number < MAX_ADDITIONAL_SETTINGS) {
        nghttp2_settings_entry& d_entry =
            local_custom_settings_.entries[number];
        d_entry.settings_id = s_entry.settings_id;
        d_entry.value = s_entry.value;
        number++;
      }
    }
  }
  local_custom_settings_.number = number;
}

void Http2Settings::Done(bool ack) {
  uint64_t end = uv_hrtime();
  double duration = (end - startTime_) / 1e6;

  Local<Value> argv[] = {Boolean::New(env()->isolate(), ack),
                         Number::New(env()->isolate(), duration)};
  MakeCallback(callback(), arraysize(argv), argv);
}

// The Http2Priority class initializes an appropriate nghttp2_priority_spec
// struct used when either creating a stream or updating its priority
// settings.
Http2Priority::Http2Priority(Environment* env,
                             Local<Value> parent,
                             Local<Value> weight,
                             Local<Value> exclusive) {
  Local<Context> context = env->context();
  int32_t parent_ = parent->Int32Value(context).ToChecked();
  int32_t weight_ = weight->Int32Value(context).ToChecked();
  bool exclusive_ = exclusive->IsTrue();
  Debug(env, DebugCategory::HTTP2STREAM,
        "Http2Priority: parent: %d, weight: %d, exclusive: %s\n",
        parent_, weight_, exclusive_ ? "yes" : "no");
  nghttp2_priority_spec_init(this, parent_, weight_, exclusive_ ? 1 : 0);
}


const char* Http2Session::TypeName() const {
  switch (session_type_) {
    case NGHTTP2_SESSION_SERVER: return "server";
    case NGHTTP2_SESSION_CLIENT: return "client";
    default:
      // This should never happen
      ABORT();
  }
}

Origins::Origins(
    Environment* env,
    Local<String> origin_string,
    size_t origin_count)
    : count_(origin_count) {
  int origin_string_len = origin_string->Length();
  if (count_ == 0) {
    CHECK_EQ(origin_string_len, 0);
    return;
  }

  {
    NoArrayBufferZeroFillScope no_zero_fill_scope(env->isolate_data());
    bs_ = ArrayBuffer::NewBackingStore(env->isolate(),
                                       alignof(nghttp2_origin_entry) - 1 +
                                       count_ * sizeof(nghttp2_origin_entry) +
                                       origin_string_len);
  }

  // Make sure the start address is aligned appropriately for an nghttp2_nv*.
  char* start = nbytes::AlignUp(static_cast<char*>(bs_->Data()),
                                alignof(nghttp2_origin_entry));
  char* origin_contents = start + (count_ * sizeof(nghttp2_origin_entry));
  nghttp2_origin_entry* const nva =
      reinterpret_cast<nghttp2_origin_entry*>(start);

  CHECK_LE(origin_contents + origin_string_len,
           static_cast<char*>(bs_->Data()) + bs_->ByteLength());
  CHECK_EQ(origin_string->WriteOneByte(
               env->isolate(),
               reinterpret_cast<uint8_t*>(origin_contents),
               0,
               origin_string_len,
               String::NO_NULL_TERMINATION),
           origin_string_len);

  size_t n = 0;
  char* p;
  for (p = origin_contents; p < origin_contents + origin_string_len; n++) {
    if (n >= count_) {
      static uint8_t zero = '\0';
      nva[0].origin = &zero;
      nva[0].origin_len = 1;
      count_ = 1;
      return;
    }

    nva[n].origin = reinterpret_cast<uint8_t*>(p);
    nva[n].origin_len = strlen(p);
    p += nva[n].origin_len + 1;
  }
}

// Sets the various callback functions that nghttp2 will use to notify us
// about significant events while processing http2 stuff.
Http2Session::Callbacks::Callbacks(bool kHasGetPaddingCallback) {
  nghttp2_session_callbacks* callbacks_;
  CHECK_EQ(nghttp2_session_callbacks_new(&callbacks_), 0);
  callbacks.reset(callbacks_);

  nghttp2_session_callbacks_set_on_begin_headers_callback(
    callbacks_, OnBeginHeadersCallback);
  nghttp2_session_callbacks_set_on_header_callback2(
    callbacks_, OnHeaderCallback);
  nghttp2_session_callbacks_set_on_frame_recv_callback(
    callbacks_, OnFrameReceive);
  nghttp2_session_callbacks_set_on_stream_close_callback(
    callbacks_, OnStreamClose);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
    callbacks_, OnDataChunkReceived);
  nghttp2_session_callbacks_set_on_frame_not_send_callback(
    callbacks_, OnFrameNotSent);
  nghttp2_session_callbacks_set_on_invalid_header_callback2(
    callbacks_, OnInvalidHeader);
  nghttp2_session_callbacks_set_error_callback2(callbacks_, OnNghttpError);
  nghttp2_session_callbacks_set_send_data_callback(
    callbacks_, OnSendData);
  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
    callbacks_, OnInvalidFrame);
  nghttp2_session_callbacks_set_on_frame_send_callback(
    callbacks_, OnFrameSent);

  if (kHasGetPaddingCallback) {
    nghttp2_session_callbacks_set_select_padding_callback(
      callbacks_, OnSelectPadding);
  }
}

void Http2Session::StopTrackingRcbuf(nghttp2_rcbuf* buf) {
  StopTrackingMemory(buf);
}

void Http2Session::CheckAllocatedSize(size_t previous_size) const {
  CHECK_GE(current_nghttp2_memory_, previous_size);
}

void Http2Session::IncreaseAllocatedSize(size_t size) {
  current_nghttp2_memory_ += size;
}

void Http2Session::DecreaseAllocatedSize(size_t size) {
  current_nghttp2_memory_ -= size;
}

Http2Session::Http2Session(Http2State* http2_state,
                           Local<Object> wrap,
                           SessionType type)
    : AsyncWrap(http2_state->env(), wrap, AsyncWrap::PROVIDER_HTTP2SESSION),
      js_fields_(http2_state->env()->isolate()),
      session_type_(type),
      http2_state_(http2_state) {
  MakeWeak();
  statistics_.session_type = type;
  statistics_.start_time = uv_hrtime();

  // Capture the configuration options for this session
  Http2Options opts(http2_state, type);

  max_session_memory_ = opts.max_session_memory();

  uint32_t maxHeaderPairs = opts.max_header_pairs();
  max_header_pairs_ =
      type == NGHTTP2_SESSION_SERVER
          ? GetServerMaxHeaderPairs(maxHeaderPairs)
          : GetClientMaxHeaderPairs(maxHeaderPairs);

  max_outstanding_pings_ = opts.max_outstanding_pings();
  max_outstanding_settings_ = opts.max_outstanding_settings();

  local_custom_settings_.number = 0;
  remote_custom_settings_.number = 0;
  // now, import possible custom_settings
  FetchAllowedRemoteCustomSettings();

  padding_strategy_ = opts.padding_strategy();

  bool hasGetPaddingCallback =
      padding_strategy_ != PADDING_STRATEGY_NONE;

  auto fn = type == NGHTTP2_SESSION_SERVER ?
      nghttp2_session_server_new3 :
      nghttp2_session_client_new3;

  nghttp2_mem alloc_info = MakeAllocator();

  // This should fail only if the system is out of memory, which
  // is going to cause lots of other problems anyway, or if any
  // of the options are out of acceptable range, which we should
  // be catching before it gets this far. Either way, crash if this
  // fails.
  nghttp2_session* session;
  CHECK_EQ(fn(
      &session,
      callback_struct_saved[hasGetPaddingCallback ? 1 : 0].callbacks.get(),
      this,
      *opts,
      &alloc_info), 0);
  session_.reset(session);

  outgoing_storage_.reserve(1024);
  outgoing_buffers_.reserve(32);

  Local<Uint8Array> uint8_arr =
      Uint8Array::New(js_fields_.GetArrayBuffer(), 0, kSessionUint8FieldCount);
  USE(wrap->Set(env()->context(), env()->fields_string(), uint8_arr));
}

Http2Session::~Http2Session() {
  CHECK(!is_in_scope());
  Debug(this, "freeing nghttp2 session");
  // Ensure that all `Http2Stream` instances and the memory they hold
  // on to are destroyed before the nghttp2 session is.
  for (const auto& [id, stream] : streams_) {
    stream->Detach();
  }
  streams_.clear();
  // Explicitly reset session_ so the subsequent
  // current_nghttp2_memory_ check passes.
  session_.reset();
  CHECK_EQ(current_nghttp2_memory_, 0);
}

void Http2Session::FetchAllowedRemoteCustomSettings() {
  AliasedUint32Array& buffer = http2_state_->settings_buffer;
  uint32_t numAddSettings = buffer[IDX_SETTINGS_COUNT + 1];
  if (numAddSettings > 0) {
    nghttp2_settings_entry* entries = remote_custom_settings_.entries;
    uint32_t offset = IDX_SETTINGS_COUNT + 1 + 1;
    size_t count = 0;
    for (uint32_t i = 0; i < numAddSettings; i++) {
      uint32_t key =
          (buffer[offset + i * 2 + 0] & 0xffff) |
          (1
           << 16);  // setting the bit 16 indicates, that no values has been set
      entries[count++] = nghttp2_settings_entry{(int32_t)key, 0};
    }
    remote_custom_settings_.number = count;
  }
}

void Http2Session::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("streams", streams_);
  tracker->TrackField("outstanding_pings", outstanding_pings_);
  tracker->TrackField("outstanding_settings", outstanding_settings_);
  tracker->TrackField("outgoing_buffers", outgoing_buffers_);
  tracker->TrackFieldWithSize("stream_buf", stream_buf_.len);
  tracker->TrackFieldWithSize("outgoing_storage", outgoing_storage_.size());
  tracker->TrackFieldWithSize("pending_rst_streams",
                              pending_rst_streams_.size() * sizeof(int32_t));
  tracker->TrackFieldWithSize("nghttp2_memory", current_nghttp2_memory_);
}

std::string Http2Session::diagnostic_name() const {
  return std::string("Http2Session ") + TypeName() + " (" +
         std::to_string(static_cast<int64_t>(get_async_id())) + ")";
}

MaybeLocal<Object> Http2StreamPerformanceEntryTraits::GetDetails(
    Environment* env, const Http2StreamPerformanceEntry& entry) {
  Local<Object> obj = Object::New(env->isolate());

#define SET(name, val)                                                         \
  if (!obj->Set(                                                               \
          env->context(),                                                      \
          env->name(),                                                         \
          Number::New(                                                         \
            env->isolate(),                                                    \
            static_cast<double>(entry.details.val))).IsJust()) {               \
    return MaybeLocal<Object>();                                               \
  }

  SET(bytes_read_string, received_bytes)
  SET(bytes_written_string, sent_bytes)
  SET(id_string, id)
#undef SET

#define SET(name, val)                                                         \
  if (!obj->Set(                                                               \
          env->context(),                                                      \
          env->name(),                                                         \
          Number::New(                                                         \
              env->isolate(),                                                  \
              (entry.details.val - entry.details.start_time) / 1e6))           \
                  .IsJust()) {                                                 \
    return MaybeLocal<Object>();                                               \
  }

  SET(time_to_first_byte_string, first_byte)
  SET(time_to_first_byte_sent_string, first_byte_sent)
  SET(time_to_first_header_string, first_header)
#undef SET

  return obj;
}

MaybeLocal<Object> Http2SessionPerformanceEntryTraits::GetDetails(
    Environment* env,
    const Http2SessionPerformanceEntry& entry) {
  Local<Object> obj = Object::New(env->isolate());

#define SET(name, val)                                                         \
  if (!obj->Set(                                                               \
          env->context(),                                                      \
          env->name(),                                                         \
          Number::New(                                                         \
            env->isolate(),                                                    \
            static_cast<double>(entry.details.val))).IsJust()) {               \
    return MaybeLocal<Object>();                                               \
  }

  SET(bytes_written_string, data_sent)
  SET(bytes_read_string, data_received)
  SET(frames_received_string, frame_count)
  SET(frames_sent_string, frame_sent)
  SET(max_concurrent_streams_string, max_concurrent_streams)
  SET(ping_rtt_string, ping_rtt)
  SET(stream_average_duration_string, stream_average_duration)
  SET(stream_count_string, stream_count)

  if (!obj->Set(
          env->context(),
          env->type_string(),
          OneByteString(
              env->isolate(),
              (entry.details.session_type == NGHTTP2_SESSION_SERVER)
                  ? "server" : "client")).IsJust()) {
    return MaybeLocal<Object>();
  }

#undef SET
  return obj;
}

void Http2Stream::EmitStatistics() {
  CHECK_NOT_NULL(session());
  if (LIKELY(!HasHttp2Observer(env())))
    return;

  double start = statistics_.start_time / 1e6;
  double duration = (PERFORMANCE_NOW() / 1e6) - start;

  std::unique_ptr<Http2StreamPerformanceEntry> entry =
      std::make_unique<Http2StreamPerformanceEntry>(
          "Http2Stream",
          start - (env()->time_origin() / 1e6),
          duration,
          statistics_);

  env()->SetImmediate([entry = std::move(entry)](Environment* env) {
    if (HasHttp2Observer(env))
      entry->Notify(env);
  });
}

void Http2Session::EmitStatistics() {
  if (LIKELY(!HasHttp2Observer(env())))
    return;

  double start = statistics_.start_time / 1e6;
  double duration = (PERFORMANCE_NOW() / 1e6) - start;

  std::unique_ptr<Http2SessionPerformanceEntry> entry =
      std::make_unique<Http2SessionPerformanceEntry>(
          "Http2Session",
          start - (env()->time_origin() / 1e6),
          duration,
          statistics_);

  env()->SetImmediate([entry = std::move(entry)](Environment* env) {
    if (HasHttp2Observer(env))
      entry->Notify(env);
  });
}

// Closes the session and frees the associated resources
void Http2Session::Close(uint32_t code, bool socket_closed) {
  Debug(this, "closing session");

  if (is_closing())
    return;
  set_closing();

  // Stop reading on the i/o stream
  if (stream_ != nullptr) {
    set_reading_stopped();
    stream_->ReadStop();
  }

  // If the socket is not closed, then attempt to send a closing GOAWAY
  // frame. There is no guarantee that this GOAWAY will be received by
  // the peer but the HTTP/2 spec recommends sending it anyway. We'll
  // make a best effort.
  if (!socket_closed) {
    Debug(this, "terminating session with code %d", code);
    CHECK_EQ(nghttp2_session_terminate_session(session_.get(), code), 0);
    SendPendingData();
  } else if (stream_ != nullptr) {
    stream_->RemoveStreamListener(this);
  }

  set_destroyed();

  // If we are writing we will get to make the callback in OnStreamAfterWrite.
  if (!is_write_in_progress()) {
    Debug(this, "make done session callback");
    HandleScope scope(env()->isolate());
    MakeCallback(env()->ondone_string(), 0, nullptr);
    if (stream_ != nullptr) {
      // Start reading again to detect the other end finishing.
      set_reading_stopped(false);
      stream_->ReadStart();
    }
  }

  // If there are outstanding pings, those will need to be canceled, do
  // so on the next iteration of the event loop to avoid calling out into
  // javascript since this may be called during garbage collection.
  while (BaseObjectPtr<Http2Ping> ping = PopPing()) {
    ping->DetachFromSession();
    env()->SetImmediate(
        [ping = std::move(ping)](Environment* env) {
          ping->Done(false);
        });
  }

  statistics_.end_time = uv_hrtime();
  EmitStatistics();
}

// Locates an existing known stream by ID. nghttp2 has a similar method
// but this is faster and does not fail if the stream is not found.
BaseObjectPtr<Http2Stream> Http2Session::FindStream(int32_t id) {
  auto s = streams_.find(id);
  return s != streams_.end() ? s->second : BaseObjectPtr<Http2Stream>();
}

bool Http2Session::CanAddStream() {
  uint32_t maxConcurrentStreams =
      nghttp2_session_get_local_settings(
          session_.get(), NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
  size_t maxSize =
      std::min(streams_.max_size(), static_cast<size_t>(maxConcurrentStreams));
  // We can add a new stream so long as we are less than the current
  // maximum on concurrent streams and there's enough available memory
  return streams_.size() < maxSize &&
         has_available_session_memory(sizeof(Http2Stream));
}

void Http2Session::AddStream(Http2Stream* stream) {
  CHECK_GE(++statistics_.stream_count, 0);
  streams_[stream->id()] = BaseObjectPtr<Http2Stream>(stream);
  size_t size = streams_.size();
  if (size > statistics_.max_concurrent_streams)
    statistics_.max_concurrent_streams = size;
  IncrementCurrentSessionMemory(sizeof(*stream));
}


BaseObjectPtr<Http2Stream> Http2Session::RemoveStream(int32_t id) {
  BaseObjectPtr<Http2Stream> stream;
  if (streams_.empty())
    return stream;
  stream = FindStream(id);
  if (stream) {
    streams_.erase(id);
    DecrementCurrentSessionMemory(sizeof(*stream));
  }
  return stream;
}

// Used as one of the Padding Strategy functions. Will attempt to ensure
// that the total frame size, including header bytes, are 8-byte aligned.
// If maxPayloadLen is smaller than the number of bytes necessary to align,
// will return maxPayloadLen instead.
ssize_t Http2Session::OnDWordAlignedPadding(size_t frameLen,
                                            size_t maxPayloadLen) {
  size_t r = (frameLen + 9) % 8;
  if (r == 0) return frameLen;  // If already a multiple of 8, return.

  size_t pad = frameLen + (8 - r);

  // If maxPayloadLen happens to be less than the calculated pad length,
  // use the max instead, even tho this means the frame will not be
  // aligned.
  pad = std::min(maxPayloadLen, pad);
  Debug(this, "using frame size padding: %d", pad);
  return pad;
}

// Used as one of the Padding Strategy functions. Uses the maximum amount
// of padding allowed for the current frame.
ssize_t Http2Session::OnMaxFrameSizePadding(size_t frameLen,
                                            size_t maxPayloadLen) {
  Debug(this, "using max frame size padding: %d", maxPayloadLen);
  return maxPayloadLen;
}

// Write data received from the i/o stream to the underlying nghttp2_session.
// On each call to nghttp2_session_mem_recv, nghttp2 will begin calling the
// various callback functions. Each of these will typically result in a call
// out to JavaScript so this particular function is rather hot and can be
// quite expensive. This is a potential performance optimization target later.
void Http2Session::ConsumeHTTP2Data() {
  CHECK_NOT_NULL(stream_buf_.base);
  CHECK_LE(stream_buf_offset_, stream_buf_.len);
  size_t read_len = stream_buf_.len - stream_buf_offset_;

  // multiple side effects.
  Debug(this, "receiving %d bytes [wants data? %d]",
        read_len,
        nghttp2_session_want_read(session_.get()));
  set_receive_paused(false);
  custom_recv_error_code_ = nullptr;
  ssize_t ret =
    nghttp2_session_mem_recv(session_.get(),
                             reinterpret_cast<uint8_t*>(stream_buf_.base) +
                                 stream_buf_offset_,
                             read_len);
  CHECK_NE(ret, NGHTTP2_ERR_NOMEM);
  CHECK_IMPLIES(custom_recv_error_code_ != nullptr, ret < 0);

  if (is_receive_paused()) {
    CHECK(is_reading_stopped());

    CHECK_GT(ret, 0);
    CHECK_LE(static_cast<size_t>(ret), read_len);

    // Mark the remainder of the data as available for later consumption.
    // Even if all bytes were received, a paused stream may delay the
    // nghttp2_on_frame_recv_callback which may have an END_STREAM flag.
    stream_buf_offset_ += ret;
    goto done;
  }

  // We are done processing the current input chunk.
  DecrementCurrentSessionMemory(stream_buf_.len);
  stream_buf_offset_ = 0;
  stream_buf_ab_.Reset();
  stream_buf_allocation_.reset();
  stream_buf_ = uv_buf_init(nullptr, 0);

  // Send any data that was queued up while processing the received data.
  if (ret >= 0 && !is_destroyed()) {
    SendPendingData();
  }

done:
  if (UNLIKELY(ret < 0)) {
    Isolate* isolate = env()->isolate();
    Debug(this,
        "fatal error receiving data: %d (%s)",
        ret,
        custom_recv_error_code_ != nullptr ?
            custom_recv_error_code_ : "(no custom error code)");
    Local<Value> args[] = {
      Integer::New(isolate, static_cast<int32_t>(ret)),
      Null(isolate)
    };
    if (custom_recv_error_code_ != nullptr) {
      args[1] = String::NewFromUtf8(
          isolate,
          custom_recv_error_code_,
          NewStringType::kInternalized).ToLocalChecked();
    }
    MakeCallback(
        env()->http2session_on_error_function(),
        arraysize(args),
        args);
  }
}


int32_t GetFrameID(const nghttp2_frame* frame) {
  // If this is a push promise, we want to grab the id of the promised stream
  return (frame->hd.type == NGHTTP2_PUSH_PROMISE) ?
      frame->push_promise.promised_stream_id :
      frame->hd.stream_id;
}


// Called by nghttp2 at the start of receiving a HEADERS frame. We use this
// callback to determine if a new stream is being created or if we are simply
// adding a new block of headers to an existing stream. The header pairs
// themselves are set in the OnHeaderCallback
int Http2Session::OnBeginHeadersCallback(nghttp2_session* handle,
                                         const nghttp2_frame* frame,
                                         void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  int32_t id = GetFrameID(frame);
  Debug(session, "beginning headers for stream %d", id);

  BaseObjectPtr<Http2Stream> stream = session->FindStream(id);
  // The common case is that we're creating a new stream. The less likely
  // case is that we're receiving a set of trailers
  if (LIKELY(!stream)) {
    if (UNLIKELY(!session->CanAddStream() ||
                 Http2Stream::New(session, id, frame->headers.cat) ==
                     nullptr)) {
      if (session->rejected_stream_count_++ >
          session->js_fields_->max_rejected_streams)
        return NGHTTP2_ERR_CALLBACK_FAILURE;
      // Too many concurrent streams being opened
      nghttp2_submit_rst_stream(
          session->session(),
          NGHTTP2_FLAG_NONE,
          id,
          NGHTTP2_ENHANCE_YOUR_CALM);
      return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
    }

    session->rejected_stream_count_ = 0;
  } else if (!stream->is_destroyed()) {
    stream->StartHeaders(frame->headers.cat);
  }
  return 0;
}

// Called by nghttp2 for each header name/value pair in a HEADERS block.
// This had to have been preceded by a call to OnBeginHeadersCallback so
// the Http2Stream is guaranteed to already exist.
int Http2Session::OnHeaderCallback(nghttp2_session* handle,
                                   const nghttp2_frame* frame,
                                   nghttp2_rcbuf* name,
                                   nghttp2_rcbuf* value,
                                   uint8_t flags,
                                   void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  int32_t id = GetFrameID(frame);
  BaseObjectPtr<Http2Stream> stream = session->FindStream(id);
  // If stream is null at this point, either something odd has happened
  // or the stream was closed locally while header processing was occurring.
  // either way, do not proceed and close the stream.
  if (UNLIKELY(!stream))
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;

  // If the stream has already been destroyed, ignore.
  if (!stream->is_destroyed() && !stream->AddHeader(name, value, flags)) {
    // This will only happen if the connected peer sends us more
    // than the allowed number of header items at any given time
    stream->SubmitRstStream(NGHTTP2_ENHANCE_YOUR_CALM);
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }
  return 0;
}


// Called by nghttp2 when a complete HTTP2 frame has been received. There are
// only a handful of frame types that we care about handling here.
int Http2Session::OnFrameReceive(nghttp2_session* handle,
                                 const nghttp2_frame* frame,
                                 void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  session->statistics_.frame_count++;
  Debug(session, "complete frame received: type: %d",
        frame->hd.type);
  switch (frame->hd.type) {
    case NGHTTP2_DATA:
      return session->HandleDataFrame(frame);
    case NGHTTP2_PUSH_PROMISE:
      // Intentional fall-through, handled just like headers frames
    case NGHTTP2_HEADERS:
      session->HandleHeadersFrame(frame);
      break;
    case NGHTTP2_SETTINGS:
      session->HandleSettingsFrame(frame);
      break;
    case NGHTTP2_PRIORITY:
      session->HandlePriorityFrame(frame);
      break;
    case NGHTTP2_GOAWAY:
      session->HandleGoawayFrame(frame);
      break;
    case NGHTTP2_PING:
      session->HandlePingFrame(frame);
      break;
    case NGHTTP2_ALTSVC:
      session->HandleAltSvcFrame(frame);
      break;
    case NGHTTP2_ORIGIN:
      session->HandleOriginFrame(frame);
      break;
    default:
      break;
  }
  return 0;
}

int Http2Session::OnInvalidFrame(nghttp2_session* handle,
                                 const nghttp2_frame* frame,
                                 int lib_error_code,
                                 void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  const uint32_t max_invalid_frames = session->js_fields_->max_invalid_frames;

  Debug(session,
        "invalid frame received (%u/%u), code: %d",
        session->invalid_frame_count_,
        max_invalid_frames,
        lib_error_code);
  if (session->invalid_frame_count_++ > max_invalid_frames) {
    session->custom_recv_error_code_ = "ERR_HTTP2_TOO_MANY_INVALID_FRAMES";
    return 1;
  }

  // If the error is fatal or if error code is ERR_STREAM_CLOSED... emit error
  if (nghttp2_is_fatal(lib_error_code) ||
      lib_error_code == NGHTTP2_ERR_STREAM_CLOSED) {
    Environment* env = session->env();
    Isolate* isolate = env->isolate();
    HandleScope scope(isolate);
    Local<Context> context = env->context();
    Context::Scope context_scope(context);
    Local<Value> arg = Integer::New(isolate, lib_error_code);
    session->MakeCallback(env->http2session_on_error_function(), 1, &arg);
  }
  return 0;
}

// Remove the headers reference.
// Implicitly calls nghttp2_rcbuf_decref
void Http2Session::DecrefHeaders(const nghttp2_frame* frame) {
  int32_t id = GetFrameID(frame);
  BaseObjectPtr<Http2Stream> stream = FindStream(id);

  if (stream && !stream->is_destroyed() && stream->headers_count() > 0) {
    Debug(this, "freeing headers for stream %d", id);
    stream->ClearHeaders();
    CHECK_EQ(stream->headers_count(), 0);
    DecrementCurrentSessionMemory(stream->current_headers_length_);
    stream->current_headers_length_ = 0;
  }
}

uint32_t TranslateNghttp2ErrorCode(const int libErrorCode) {
  switch (libErrorCode) {
  case NGHTTP2_ERR_STREAM_CLOSED:
    return NGHTTP2_STREAM_CLOSED;
  case NGHTTP2_ERR_HEADER_COMP:
    return NGHTTP2_COMPRESSION_ERROR;
  case NGHTTP2_ERR_FRAME_SIZE_ERROR:
    return NGHTTP2_FRAME_SIZE_ERROR;
  case NGHTTP2_ERR_FLOW_CONTROL:
    return NGHTTP2_FLOW_CONTROL_ERROR;
  case NGHTTP2_ERR_REFUSED_STREAM:
    return NGHTTP2_REFUSED_STREAM;
  case NGHTTP2_ERR_PROTO:
  case NGHTTP2_ERR_HTTP_HEADER:
  case NGHTTP2_ERR_HTTP_MESSAGING:
    return NGHTTP2_PROTOCOL_ERROR;
  default:
    return NGHTTP2_INTERNAL_ERROR;
  }
}

// If nghttp2 is unable to send a queued up frame, it will call this callback
// to let us know. If the failure occurred because we are in the process of
// closing down the session or stream, we go ahead and ignore it. We don't
// really care about those and there's nothing we can reasonably do about it
// anyway. Other types of failures are reported up to JavaScript. This should
// be exceedingly rare.
int Http2Session::OnFrameNotSent(nghttp2_session* handle,
                                 const nghttp2_frame* frame,
                                 int error_code,
                                 void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  Environment* env = session->env();
  Debug(session, "frame type %d was not sent, code: %d",
        frame->hd.type, error_code);

  // Do not report if the frame was not sent due to the session closing
  if (error_code == NGHTTP2_ERR_SESSION_CLOSING ||
      error_code == NGHTTP2_ERR_STREAM_CLOSED ||
      error_code == NGHTTP2_ERR_STREAM_CLOSING) {
    // Nghttp2 contains header limit of 65536. When this value is exceeded the
    // pipeline is stopped and we should remove the current headers reference
    // to destroy the session completely.
    // Further information see: https://github.com/nodejs/node/issues/35233
    session->DecrefHeaders(frame);
    return 0;
  }

  Isolate* isolate = env->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env->context();
  Context::Scope context_scope(context);

  Local<Value> argv[3] = {
    Integer::New(isolate, frame->hd.stream_id),
    Integer::New(isolate, frame->hd.type),
    Integer::New(isolate, TranslateNghttp2ErrorCode(error_code))
  };
  session->MakeCallback(
      env->http2session_on_frame_error_function(),
      arraysize(argv), argv);
  return 0;
}

int Http2Session::OnFrameSent(nghttp2_session* handle,
                              const nghttp2_frame* frame,
                              void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  session->statistics_.frame_sent += 1;
  return 0;
}

// Called by nghttp2 when a stream closes.
int Http2Session::OnStreamClose(nghttp2_session* handle,
                                int32_t id,
                                uint32_t code,
                                void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  Environment* env = session->env();
  Isolate* isolate = env->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env->context();
  Context::Scope context_scope(context);
  Debug(session, "stream %d closed with code: %d", id, code);
  BaseObjectPtr<Http2Stream> stream = session->FindStream(id);
  // Intentionally ignore the callback if the stream does not exist or has
  // already been destroyed
  if (!stream || stream->is_destroyed())
    return 0;

  stream->Close(code);

  // It is possible for the stream close to occur before the stream is
  // ever passed on to the javascript side. If that happens, the callback
  // will return false.
  if (env->can_call_into_js()) {
    Local<Value> arg = Integer::NewFromUnsigned(isolate, code);
    MaybeLocal<Value> answer = stream->MakeCallback(
        env->http2session_on_stream_close_function(), 1, &arg);
    if (answer.IsEmpty() || answer.ToLocalChecked()->IsFalse()) {
      // Skip to destroy
      stream->Destroy();
    }
  }
  return 0;
}

// Called by nghttp2 when an invalid header has been received. For now, we
// ignore these. If this callback was not provided, nghttp2 would handle
// invalid headers strictly and would shut down the stream. We are intentionally
// being more lenient here although we may want to revisit this choice later.
int Http2Session::OnInvalidHeader(nghttp2_session* session,
                                  const nghttp2_frame* frame,
                                  nghttp2_rcbuf* name,
                                  nghttp2_rcbuf* value,
                                  uint8_t flags,
                                  void* user_data) {
  // Ignore invalid header fields by default.
  return 0;
}

// When nghttp2 receives a DATA frame, it will deliver the data payload to
// us in discrete chunks. We push these into a linked list stored in the
// Http2Sttream which is flushed out to JavaScript as quickly as possible.
// This can be a particularly hot path.
int Http2Session::OnDataChunkReceived(nghttp2_session* handle,
                                      uint8_t flags,
                                      int32_t id,
                                      const uint8_t* data,
                                      size_t len,
                                      void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  Debug(session, "buffering data chunk for stream %d, size: "
        "%d, flags: %d", id, len, flags);
  Environment* env = session->env();
  HandleScope scope(env->isolate());

  // We should never actually get a 0-length chunk so this check is
  // only a precaution at this point.
  if (len == 0)
    return 0;

  // Notify nghttp2 that we've consumed a chunk of data on the connection
  // so that it can send a WINDOW_UPDATE frame. This is a critical part of
  // the flow control process in http2
  CHECK_EQ(nghttp2_session_consume_connection(handle, len), 0);
  BaseObjectPtr<Http2Stream> stream = session->FindStream(id);

  // If the stream has been destroyed, ignore this chunk
  if (!stream || stream->is_destroyed())
    return 0;

  stream->statistics_.received_bytes += len;

  // Repeatedly ask the stream's owner for memory, and copy the read data
  // into those buffers.
  // The typical case is actually the exception here; Http2StreamListeners
  // know about the HTTP2 session associated with this stream, so they know
  // about the larger from-socket read buffer, so they do not require copying.
  do {
    uv_buf_t buf = stream->EmitAlloc(len);
    ssize_t avail = len;
    if (static_cast<ssize_t>(buf.len) < avail)
      avail = buf.len;

    // `buf.base == nullptr` is the default Http2StreamListener's way
    // of saying that it wants a pointer to the raw original.
    // Since it has access to the original socket buffer from which the data
    // was read in the first place, it can use that to minimize ArrayBuffer
    // allocations.
    if (LIKELY(buf.base == nullptr))
      buf.base = reinterpret_cast<char*>(const_cast<uint8_t*>(data));
    else
      memcpy(buf.base, data, avail);
    data += avail;
    len -= avail;
    stream->EmitRead(avail, buf);

    // If the stream owner (e.g. the JS Http2Stream) wants more data, just
    // tell nghttp2 that all data has been consumed. Otherwise, defer until
    // more data is being requested.
    if (stream->is_reading())
      nghttp2_session_consume_stream(handle, id, avail);
    else
      stream->inbound_consumed_data_while_paused_ += avail;

    // If we have a gathered a lot of data for output, try sending it now.
    if (session->outgoing_length_ > 4096 ||
        stream->available_outbound_length_ > 4096) {
      session->SendPendingData();
    }
  } while (len != 0);

  // If we are currently waiting for a write operation to finish, we should
  // tell nghttp2 that we want to wait before we process more input data.
  if (session->is_write_in_progress()) {
    CHECK(session->is_reading_stopped());
    session->set_receive_paused();
    Debug(session, "receive paused");
    return NGHTTP2_ERR_PAUSE;
  }

  return 0;
}

// Called by nghttp2 when it needs to determine how much padding to use in
// a DATA or HEADERS frame.
ssize_t Http2Session::OnSelectPadding(nghttp2_session* handle,
                                      const nghttp2_frame* frame,
                                      size_t maxPayloadLen,
                                      void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  ssize_t padding = frame->hd.length;

  switch (session->padding_strategy_) {
    case PADDING_STRATEGY_NONE:
      // Fall-through
      break;
    case PADDING_STRATEGY_MAX:
      padding = session->OnMaxFrameSizePadding(padding, maxPayloadLen);
      break;
    case PADDING_STRATEGY_ALIGNED:
      padding = session->OnDWordAlignedPadding(padding, maxPayloadLen);
      break;
  }
  return padding;
}

// We use this currently to determine when an attempt is made to use the http2
// protocol with a non-http2 peer.
int Http2Session::OnNghttpError(nghttp2_session* handle,
                                int lib_error_code,
                                const char* message,
                                size_t len,
                                void* user_data) {
  // Unfortunately, this is currently the only way for us to know if
  // the session errored because the peer is not an http2 peer.
  Http2Session* session = static_cast<Http2Session*>(user_data);
  Debug(session, "Error '%s'", message);
  if (lib_error_code == NGHTTP2_ERR_SETTINGS_EXPECTED) {
    Environment* env = session->env();
    Isolate* isolate = env->isolate();
    HandleScope scope(isolate);
    Local<Context> context = env->context();
    Context::Scope context_scope(context);
    Local<Value> arg = Integer::New(isolate, NGHTTP2_ERR_PROTO);
    session->MakeCallback(env->http2session_on_error_function(), 1, &arg);
  }
  return 0;
}

uv_buf_t Http2StreamListener::OnStreamAlloc(size_t size) {
  // See the comments in Http2Session::OnDataChunkReceived
  // (which is the only possible call site for this method).
  return uv_buf_init(nullptr, size);
}

void Http2StreamListener::OnStreamRead(ssize_t nread, const uv_buf_t& buf) {
  Http2Stream* stream = static_cast<Http2Stream*>(stream_);
  Http2Session* session = stream->session();
  Environment* env = stream->env();
  HandleScope handle_scope(env->isolate());
  Context::Scope context_scope(env->context());

  if (nread < 0) {
    PassReadErrorToPreviousListener(nread);
    return;
  }

  Local<ArrayBuffer> ab;
  if (session->stream_buf_ab_.IsEmpty()) {
    ab = ArrayBuffer::New(env->isolate(),
                          std::move(session->stream_buf_allocation_));
    session->stream_buf_ab_.Reset(env->isolate(), ab);
  } else {
    ab = PersistentToLocal::Strong(session->stream_buf_ab_);
  }

  // There is a single large array buffer for the entire data read from the
  // network; create a slice of that array buffer and emit it as the
  // received data buffer.
  size_t offset = buf.base - session->stream_buf_.base;

  // Verify that the data offset is inside the current read buffer.
  CHECK_GE(offset, session->stream_buf_offset_);
  CHECK_LE(offset, session->stream_buf_.len);
  CHECK_LE(offset + buf.len, session->stream_buf_.len);

  stream->CallJSOnreadMethod(nread, ab, offset);
}


// Called by OnFrameReceived to notify JavaScript land that a complete
// HEADERS frame has been received and processed. This method converts the
// received headers into a JavaScript array and pushes those out to JS.
void Http2Session::HandleHeadersFrame(const nghttp2_frame* frame) {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);

  int32_t id = GetFrameID(frame);
  Debug(this, "handle headers frame for stream %d", id);
  BaseObjectPtr<Http2Stream> stream = FindStream(id);

  // If the stream has already been destroyed, ignore.
  if (!stream || stream->is_destroyed())
    return;

  // The headers are stored as a vector of Http2Header instances.
  // The following converts that into a JS array with the structure:
  // [name1, value1, name2, value2, name3, value3, name3, value4] and so on.
  // That array is passed up to the JS layer and converted into an Object form
  // like {name1: value1, name2: value2, name3: [value3, value4]}. We do it
  // this way for performance reasons (it's faster to generate and pass an
  // array than it is to generate and pass the object).

  MaybeStackBuffer<Local<Value>, 64> headers_v(stream->headers_count() * 2);
  MaybeStackBuffer<Local<Value>, 32> sensitive_v(stream->headers_count());
  size_t sensitive_count = 0;

  stream->TransferHeaders([&](const Http2Header& header, size_t i) {
    headers_v[i * 2] = header.GetName(this).ToLocalChecked();
    headers_v[i * 2 + 1] = header.GetValue(this).ToLocalChecked();
    if (header.flags() & NGHTTP2_NV_FLAG_NO_INDEX)
      sensitive_v[sensitive_count++] = headers_v[i * 2];
  });
  CHECK_EQ(stream->headers_count(), 0);

  DecrementCurrentSessionMemory(stream->current_headers_length_);
  stream->current_headers_length_ = 0;

  Local<Value> args[] = {
    stream->object(),
    Integer::New(isolate, id),
    Integer::New(isolate, stream->headers_category()),
    Integer::New(isolate, frame->hd.flags),
    Array::New(isolate, headers_v.out(), headers_v.length()),
    Array::New(isolate, sensitive_v.out(), sensitive_count),
  };
  MakeCallback(env()->http2session_on_headers_function(),
               arraysize(args), args);
}


// Called by OnFrameReceived when a complete PRIORITY frame has been
// received. Notifies JS land about the priority change. Note that priorities
// are considered advisory only, so this has no real effect other than to
// simply let user code know that the priority has changed.
void Http2Session::HandlePriorityFrame(const nghttp2_frame* frame) {
  if (js_fields_->priority_listener_count == 0) return;
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);

  nghttp2_priority priority_frame = frame->priority;
  int32_t id = GetFrameID(frame);
  Debug(this, "handle priority frame for stream %d", id);
  // Priority frame stream ID should never be <= 0. nghttp2 handles this for us
  nghttp2_priority_spec spec = priority_frame.pri_spec;

  Local<Value> argv[4] = {
    Integer::New(isolate, id),
    Integer::New(isolate, spec.stream_id),
    Integer::New(isolate, spec.weight),
    Boolean::New(isolate, spec.exclusive)
  };
  MakeCallback(env()->http2session_on_priority_function(),
               arraysize(argv), argv);
}


// Called by OnFrameReceived when a complete DATA frame has been received.
// If we know that this was the last DATA frame (because the END_STREAM flag
// is set), then we'll terminate the readable side of the StreamBase.
int Http2Session::HandleDataFrame(const nghttp2_frame* frame) {
  int32_t id = GetFrameID(frame);
  Debug(this, "handling data frame for stream %d", id);
  BaseObjectPtr<Http2Stream> stream = FindStream(id);

  if (stream &&
      !stream->is_destroyed() &&
      frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
    stream->EmitRead(UV_EOF);
  } else if (frame->hd.length == 0) {
    if (invalid_frame_count_++ > js_fields_->max_invalid_frames) {
      custom_recv_error_code_ = "ERR_HTTP2_TOO_MANY_INVALID_FRAMES";
      Debug(this, "rejecting empty-frame-without-END_STREAM flood\n");
      // Consider a flood of 0-length frames without END_STREAM an error.
      return 1;
    }
  }
  return 0;
}


// Called by OnFrameReceived when a complete GOAWAY frame has been received.
void Http2Session::HandleGoawayFrame(const nghttp2_frame* frame) {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);

  nghttp2_goaway goaway_frame = frame->goaway;
  Debug(this, "handling goaway frame");

  Local<Value> argv[3] = {
    Integer::NewFromUnsigned(isolate, goaway_frame.error_code),
    Integer::New(isolate, goaway_frame.last_stream_id),
    Undefined(isolate)
  };

  size_t length = goaway_frame.opaque_data_len;
  if (length > 0) {
    // If the copy fails for any reason here, we just ignore it.
    // The additional goaway data is completely optional and we
    // shouldn't fail if we're not able to process it.
    argv[2] = Buffer::Copy(isolate,
                           reinterpret_cast<char*>(goaway_frame.opaque_data),
                           length).ToLocalChecked();
  }

  MakeCallback(env()->http2session_on_goaway_data_function(),
               arraysize(argv), argv);
}

// Called by OnFrameReceived when a complete ALTSVC frame has been received.
void Http2Session::HandleAltSvcFrame(const nghttp2_frame* frame) {
  if (!(js_fields_->bitfield & (1 << kSessionHasAltsvcListeners))) return;
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);

  int32_t id = GetFrameID(frame);

  nghttp2_extension ext = frame->ext;
  nghttp2_ext_altsvc* altsvc = static_cast<nghttp2_ext_altsvc*>(ext.payload);
  Debug(this, "handling altsvc frame");

  Local<Value> argv[3] = {
    Integer::New(isolate, id),
    OneByteString(isolate, altsvc->origin, altsvc->origin_len),
    OneByteString(isolate, altsvc->field_value, altsvc->field_value_len)
  };

  MakeCallback(env()->http2session_on_altsvc_function(),
               arraysize(argv), argv);
}

void Http2Session::HandleOriginFrame(const nghttp2_frame* frame) {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);

  Debug(this, "handling origin frame");

  nghttp2_extension ext = frame->ext;
  nghttp2_ext_origin* origin = static_cast<nghttp2_ext_origin*>(ext.payload);

  size_t nov = origin->nov;
  std::vector<Local<Value>> origin_v(nov);

  for (size_t i = 0; i < nov; ++i) {
    const nghttp2_origin_entry& entry = origin->ov[i];
    origin_v[i] = OneByteString(isolate, entry.origin, entry.origin_len);
  }
  Local<Value> holder = Array::New(isolate, origin_v.data(), origin_v.size());
  MakeCallback(env()->http2session_on_origin_function(), 1, &holder);
}

// Called by OnFrameReceived when a complete PING frame has been received.
void Http2Session::HandlePingFrame(const nghttp2_frame* frame) {
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);
  Local<Value> arg;
  bool ack = frame->hd.flags & NGHTTP2_FLAG_ACK;
  if (ack) {
    BaseObjectPtr<Http2Ping> ping = PopPing();

    if (!ping) {
      // PING Ack is unsolicited. Treat as a connection error. The HTTP/2
      // spec does not require this, but there is no legitimate reason to
      // receive an unsolicited PING ack on a connection. Either the peer
      // is buggy or malicious, and we're not going to tolerate such
      // nonsense.
      arg = Integer::New(isolate, NGHTTP2_ERR_PROTO);
      MakeCallback(env()->http2session_on_error_function(), 1, &arg);
      return;
    }

    ping->Done(true, frame->ping.opaque_data);
    return;
  }

  if (!(js_fields_->bitfield & (1 << kSessionHasPingListeners))) return;
  // Notify the session that a ping occurred
  arg = Buffer::Copy(
      env(),
      reinterpret_cast<const char*>(frame->ping.opaque_data),
      8).ToLocalChecked();
  MakeCallback(env()->http2session_on_ping_function(), 1, &arg);
}

// Called by OnFrameReceived when a complete SETTINGS frame has been received.
void Http2Session::HandleSettingsFrame(const nghttp2_frame* frame) {
  bool ack = frame->hd.flags & NGHTTP2_FLAG_ACK;
  if (!ack) {
    js_fields_->bitfield &= ~(1 << kSessionRemoteSettingsIsUpToDate);
    // update additional settings
    if (remote_custom_settings_.number > 0) {
      nghttp2_settings_entry* iv = frame->settings.iv;
      size_t niv = frame->settings.niv;
      size_t numsettings = remote_custom_settings_.number;
      for (size_t i = 0; i < niv; ++i) {
        int32_t settings_id = iv[i].settings_id;
        if (settings_id >=
            IDX_SETTINGS_COUNT) {  // unsupported, additional settings
          auto iv_value = iv[i].value;
          for (size_t j = 0; j < numsettings; ++j) {
            if ((remote_custom_settings_.entries[j].settings_id & 0xFFFF) ==
                settings_id) {
              remote_custom_settings_.entries[j].settings_id = settings_id;
              remote_custom_settings_.entries[j].value = iv_value;
              break;
            }
          }
        }
      }
    }
    if (!(js_fields_->bitfield & (1 << kSessionHasRemoteSettingsListeners)))
      return;
    // This is not a SETTINGS acknowledgement, notify and return
    MakeCallback(env()->http2session_on_settings_function(), 0, nullptr);
    return;
  }

  // If this is an acknowledgement, we should have an Http2Settings
  // object for it.
  BaseObjectPtr<Http2Settings> settings = PopSettings();
  if (settings) {
    settings->Done(true);
    return;
  }
  // SETTINGS Ack is unsolicited. Treat as a connection error. The HTTP/2
  // spec does not require this, but there is no legitimate reason to
  // receive an unsolicited SETTINGS ack on a connection. Either the peer
  // is buggy or malicious, and we're not going to tolerate such
  // nonsense.
  // Note that nghttp2 currently prevents this from happening for SETTINGS
  // frames, so this block is purely defensive just in case that behavior
  // changes. Specifically, unlike unsolicited PING acks, unsolicited
  // SETTINGS acks should *never* make it this far.
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);
  Local<Value> arg = Integer::New(isolate, NGHTTP2_ERR_PROTO);
  MakeCallback(env()->http2session_on_error_function(), 1, &arg);
}

// Callback used when data has been written to the stream.
void Http2Session::OnStreamAfterWrite(WriteWrap* w, int status) {
  Debug(this, "write finished with status %d", status);

  CHECK(is_write_in_progress());
  set_write_in_progress(false);

  // Inform all pending writes about their completion.
  ClearOutgoing(status);

  if (is_reading_stopped() &&
      !is_write_in_progress() &&
      nghttp2_session_want_read(session_.get())) {
    set_reading_stopped(false);
    stream_->ReadStart();
  }

  if (is_destroyed()) {
    HandleScope scope(env()->isolate());
    MakeCallback(env()->ondone_string(), 0, nullptr);
    if (stream_ != nullptr) {
      // Start reading again to detect the other end finishing.
      set_reading_stopped(false);
      stream_->ReadStart();
    }
    return;
  }

  // If there is more incoming data queued up, consume it.
  if (stream_buf_offset_ > 0) {
    ConsumeHTTP2Data();
  }

  if (!is_write_scheduled() && !is_destroyed()) {
    // Schedule a new write if nghttp2 wants to send data.
    MaybeScheduleWrite();
  }
}

// If the underlying nghttp2_session struct has data pending in its outbound
// queue, MaybeScheduleWrite will schedule a SendPendingData() call to occur
// on the next iteration of the Node.js event loop (using the SetImmediate
// queue), but only if a write has not already been scheduled.
void Http2Session::MaybeScheduleWrite() {
  CHECK(!is_write_scheduled());
  if (UNLIKELY(!session_))
    return;

  if (nghttp2_session_want_write(session_.get())) {
    HandleScope handle_scope(env()->isolate());
    Debug(this, "scheduling write");
    set_write_scheduled();
    BaseObjectPtr<Http2Session> strong_ref{this};
    env()->SetImmediate([this, strong_ref](Environment* env) {
      if (!session_ || !is_write_scheduled()) {
        // This can happen e.g. when a stream was reset before this turn
        // of the event loop, in which case SendPendingData() is called early,
        // or the session was destroyed in the meantime.
        return;
      }

      // Sending data may call arbitrary JS code, so keep track of
      // async context.
      if (env->can_call_into_js()) {
        HandleScope handle_scope(env->isolate());
        InternalCallbackScope callback_scope(this);
        SendPendingData();
      }
    });
  }
}

void Http2Session::MaybeStopReading() {
  // If the session is already closing we don't want to stop reading as we want
  // to detect when the other peer is actually closed.
  if (is_reading_stopped() || is_closing()) return;
  int want_read = nghttp2_session_want_read(session_.get());
  Debug(this, "wants read? %d", want_read);
  if (want_read == 0 || is_write_in_progress()) {
    set_reading_stopped();
    stream_->ReadStop();
  }
}

// Unset the sending state, finish up all current writes, and reset
// storage for data and metadata that was associated with these writes.
void Http2Session::ClearOutgoing(int status) {
  CHECK(is_sending());

  set_sending(false);

  if (!outgoing_buffers_.empty()) {
    outgoing_storage_.clear();
    outgoing_length_ = 0;

    std::vector<NgHttp2StreamWrite> current_outgoing_buffers_;
    current_outgoing_buffers_.swap(outgoing_buffers_);
    for (const NgHttp2StreamWrite& wr : current_outgoing_buffers_) {
      BaseObjectPtr<AsyncWrap> wrap = std::move(wr.req_wrap);
      if (wrap) {
        // TODO(addaleax): Pass `status` instead of 0, so that we actually error
        // out with the error from the write to the underlying protocol,
        // if one occurred.
        WriteWrap::FromObject(wrap)->Done(0);
      }
    }
  }

  // Now that we've finished sending queued data, if there are any pending
  // RstStreams we should try sending again and then flush them one by one.
  if (!pending_rst_streams_.empty()) {
    std::vector<int32_t> current_pending_rst_streams;
    pending_rst_streams_.swap(current_pending_rst_streams);

    SendPendingData();

    for (int32_t stream_id : current_pending_rst_streams) {
      BaseObjectPtr<Http2Stream> stream = FindStream(stream_id);
      if (LIKELY(stream))
        stream->FlushRstStream();
    }
  }
}

void Http2Session::PushOutgoingBuffer(NgHttp2StreamWrite&& write) {
  outgoing_length_ += write.buf.len;
  outgoing_buffers_.emplace_back(std::move(write));
}

// Queue a given block of data for sending. This always creates a copy,
// so it is used for the cases in which nghttp2 requests sending of a
// small chunk of data.
void Http2Session::CopyDataIntoOutgoing(const uint8_t* src, size_t src_length) {
  size_t offset = outgoing_storage_.size();
  outgoing_storage_.resize(offset + src_length);
  memcpy(&outgoing_storage_[offset], src, src_length);

  // Store with a base of `nullptr` initially, since future resizes
  // of the outgoing_buffers_ vector may invalidate the pointer.
  // The correct base pointers will be set later, before writing to the
  // underlying socket.
  PushOutgoingBuffer(NgHttp2StreamWrite {
    uv_buf_init(nullptr, src_length)
  });
}

// Prompts nghttp2 to begin serializing it's pending data and pushes each
// chunk out to the i/o socket to be sent. This is a particularly hot method
// that will generally be called at least twice be event loop iteration.
// This is a potential performance optimization target later.
// Returns non-zero value if a write is already in progress.
uint8_t Http2Session::SendPendingData() {
  Debug(this, "sending pending data");
  // Do not attempt to send data on the socket if the destroying flag has
  // been set. That means everything is shutting down and the socket
  // will not be usable.
  if (is_destroyed())
    return 0;
  set_write_scheduled(false);

  // SendPendingData should not be called recursively.
  if (is_sending())
    return 1;
  // This is cleared by ClearOutgoing().
  set_sending();

  ssize_t src_length;
  const uint8_t* src;

  CHECK(outgoing_buffers_.empty());
  CHECK(outgoing_storage_.empty());

  // Part One: Gather data from nghttp2

  while ((src_length = nghttp2_session_mem_send(session_.get(), &src)) > 0) {
    Debug(this, "nghttp2 has %d bytes to send", src_length);
    CopyDataIntoOutgoing(src, src_length);
  }

  CHECK_NE(src_length, NGHTTP2_ERR_NOMEM);

  if (stream_ == nullptr) {
    // It would seem nice to bail out earlier, but `nghttp2_session_mem_send()`
    // does take care of things like closing the individual streams after
    // a socket has been torn down, so we still need to call it.
    ClearOutgoing(UV_ECANCELED);
    return 0;
  }

  // Part Two: Pass Data to the underlying stream

  size_t count = outgoing_buffers_.size();
  if (count == 0) {
    ClearOutgoing(0);
    return 0;
  }
  MaybeStackBuffer<uv_buf_t, 32> bufs;
  bufs.AllocateSufficientStorage(count);

  // Set the buffer base pointers for copied data that ended up in the
  // sessions's own storage since it might have shifted around during gathering.
  // (Those are marked by having .base == nullptr.)
  size_t offset = 0;
  size_t i = 0;
  for (const NgHttp2StreamWrite& write : outgoing_buffers_) {
    statistics_.data_sent += write.buf.len;
    if (write.buf.base == nullptr) {
      bufs[i++] = uv_buf_init(
          reinterpret_cast<char*>(outgoing_storage_.data() + offset),
          write.buf.len);
      offset += write.buf.len;
    } else {
      bufs[i++] = write.buf;
    }
  }

  chunks_sent_since_last_write_++;

  CHECK(!is_write_in_progress());
  set_write_in_progress();
  StreamWriteResult res = underlying_stream()->Write(*bufs, count);
  if (!res.async) {
    set_write_in_progress(false);
    ClearOutgoing(res.err);
  }

  MaybeStopReading();

  return 0;
}


// This callback is called from nghttp2 when it wants to send DATA frames for a
// given Http2Stream, when we set the `NGHTTP2_DATA_FLAG_NO_COPY` flag earlier
// in the Http2Stream::Provider::Stream::OnRead callback.
// We take the write information directly out of the stream's data queue.
int Http2Session::OnSendData(
      nghttp2_session* session_,
      nghttp2_frame* frame,
      const uint8_t* framehd,
      size_t length,
      nghttp2_data_source* source,
      void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  BaseObjectPtr<Http2Stream> stream = session->FindStream(frame->hd.stream_id);
  if (!stream) return 0;

  // Send the frame header + a byte that indicates padding length.
  session->CopyDataIntoOutgoing(framehd, 9);
  if (frame->data.padlen > 0) {
    uint8_t padding_byte = frame->data.padlen - 1;
    CHECK_EQ(padding_byte, frame->data.padlen - 1);
    session->CopyDataIntoOutgoing(&padding_byte, 1);
  }

  Debug(session, "nghttp2 has %d bytes to send directly", length);
  while (length > 0) {
    // nghttp2 thinks that there is data available (length > 0), which means
    // we told it so, which means that we *should* have data available.
    CHECK(!stream->queue_.empty());

    NgHttp2StreamWrite& write = stream->queue_.front();
    if (write.buf.len <= length) {
      // This write does not suffice by itself, so we can consume it completely.
      length -= write.buf.len;
      session->PushOutgoingBuffer(std::move(write));
      stream->queue_.pop();
      continue;
    }

    // Slice off `length` bytes of the first write in the queue.
    session->PushOutgoingBuffer(NgHttp2StreamWrite {
      uv_buf_init(write.buf.base, length)
    });
    write.buf.base += length;
    write.buf.len -= length;
    break;
  }

  if (frame->data.padlen > 0) {
    // Send padding if that was requested.
    session->PushOutgoingBuffer(NgHttp2StreamWrite {
      uv_buf_init(const_cast<char*>(zero_bytes_256), frame->data.padlen - 1)
    });
  }

  return 0;
}

// Creates a new Http2Stream and submits a new http2 request.
Http2Stream* Http2Session::SubmitRequest(
    const Http2Priority& priority,
    const Http2Headers& headers,
    int32_t* ret,
    int options) {
  Debug(this, "submitting request");
  Http2Scope h2scope(this);
  Http2Stream* stream = nullptr;
  Http2Stream::Provider::Stream prov(options);
  *ret = nghttp2_submit_request(
      session_.get(),
      &priority,
      headers.data(),
      headers.length(),
      *prov,
      nullptr);
  CHECK_NE(*ret, NGHTTP2_ERR_NOMEM);
  if (LIKELY(*ret > 0))
    stream = Http2Stream::New(this, *ret, NGHTTP2_HCAT_HEADERS, options);
  return stream;
}

uv_buf_t Http2Session::OnStreamAlloc(size_t suggested_size) {
  return env()->allocate_managed_buffer(suggested_size);
}

// Callback used to receive inbound data from the i/o stream
void Http2Session::OnStreamRead(ssize_t nread, const uv_buf_t& buf_) {
  HandleScope handle_scope(env()->isolate());
  Context::Scope context_scope(env()->context());
  Http2Scope h2scope(this);
  CHECK_NOT_NULL(stream_);
  Debug(this, "receiving %d bytes, offset %d", nread, stream_buf_offset_);
  std::unique_ptr<BackingStore> bs = env()->release_managed_buffer(buf_);

  // Only pass data on if nread > 0
  if (nread <= 0) {
    if (nread < 0) {
      PassReadErrorToPreviousListener(nread);
    }
    return;
  }

  CHECK_LE(static_cast<size_t>(nread), bs->ByteLength());

  statistics_.data_received += nread;

  if (LIKELY(stream_buf_offset_ == 0 &&
             static_cast<size_t>(nread) != bs->ByteLength())) {
    // Shrink to the actual amount of used data.
    std::unique_ptr<BackingStore> old_bs = std::move(bs);
    bs = ArrayBuffer::NewBackingStore(env()->isolate(), nread);
    memcpy(static_cast<char*>(bs->Data()),
           static_cast<char*>(old_bs->Data()),
           nread);
  } else {
    // This is a very unlikely case, and should only happen if the ReadStart()
    // call in OnStreamAfterWrite() immediately provides data. If that does
    // happen, we concatenate the data we received with the already-stored
    // pending input data, slicing off the already processed part.
    size_t pending_len = stream_buf_.len - stream_buf_offset_;
    std::unique_ptr<BackingStore> new_bs;
    {
      NoArrayBufferZeroFillScope no_zero_fill_scope(env()->isolate_data());
      new_bs = ArrayBuffer::NewBackingStore(env()->isolate(),
                                            pending_len + nread);
    }
    memcpy(static_cast<char*>(new_bs->Data()),
           stream_buf_.base + stream_buf_offset_,
           pending_len);
    memcpy(static_cast<char*>(new_bs->Data()) + pending_len,
           bs->Data(),
           nread);

    bs = std::move(new_bs);
    nread = bs->ByteLength();
    stream_buf_offset_ = 0;
    stream_buf_ab_.Reset();

    // We have now fully processed the stream_buf_ input chunk (by moving the
    // remaining part into buf, which will be accounted for below).
    DecrementCurrentSessionMemory(stream_buf_.len);
  }

  IncrementCurrentSessionMemory(nread);

  // Remember the current buffer, so that OnDataChunkReceived knows the
  // offset of a DATA frame's data into the socket read buffer.
  stream_buf_ = uv_buf_init(static_cast<char*>(bs->Data()),
                            static_cast<unsigned int>(nread));

  // Store this so we can create an ArrayBuffer for read data from it.
  // DATA frames will be emitted as slices of that ArrayBuffer to avoid having
  // to copy memory.
  stream_buf_allocation_ = std::move(bs);

  ConsumeHTTP2Data();

  MaybeStopReading();
}

bool Http2Session::HasWritesOnSocketForStream(Http2Stream* stream) {
  for (const NgHttp2StreamWrite& wr : outgoing_buffers_) {
    if (wr.req_wrap && WriteWrap::FromObject(wr.req_wrap)->stream() == stream)
      return true;
  }
  return false;
}

// Every Http2Session session is tightly bound to a single i/o StreamBase
// (typically a net.Socket or tls.TLSSocket). The lifecycle of the two is
// tightly coupled with all data transfer between the two happening at the
// C++ layer via the StreamBase API.
void Http2Session::Consume(Local<Object> stream_obj) {
  StreamBase* stream = StreamBase::FromObject(stream_obj);
  stream->PushStreamListener(this);
  Debug(this, "i/o stream consumed");
}

// Allow injecting of data from JS
// This is used when the socket has already some data received
// before our listener was attached
// https://github.com/nodejs/node/issues/35475
void Http2Session::Receive(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  CHECK(args[0]->IsObject());

  ArrayBufferViewContents<char> buffer(args[0]);
  const char* data = buffer.data();
  size_t len = buffer.length();
  Debug(session, "Receiving %zu bytes injected from JS", len);

  // Copy given buffer
  while (len > 0) {
    uv_buf_t buf = session->OnStreamAlloc(len);
    size_t copy = buf.len > len ? len : buf.len;
    memcpy(buf.base, data, copy);
    buf.len = copy;
    session->OnStreamRead(copy, buf);

    data += copy;
    len -= copy;
  }
}

Http2Stream* Http2Stream::New(Http2Session* session,
                              int32_t id,
                              nghttp2_headers_category category,
                              int options) {
  Local<Object> obj;
  if (!session->env()
           ->http2stream_constructor_template()
           ->NewInstance(session->env()->context())
           .ToLocal(&obj)) {
    return nullptr;
  }
  return new Http2Stream(session, obj, id, category, options);
}

Http2Stream::Http2Stream(Http2Session* session,
                         Local<Object> obj,
                         int32_t id,
                         nghttp2_headers_category category,
                         int options)
    : AsyncWrap(session->env(), obj, AsyncWrap::PROVIDER_HTTP2STREAM),
      StreamBase(session->env()),
      session_(session),
      id_(id),
      current_headers_category_(category) {
  MakeWeak();
  StreamBase::AttachToObject(GetObject());
  statistics_.id = id;
  statistics_.start_time = uv_hrtime();

  // Limit the number of header pairs
  max_header_pairs_ = session->max_header_pairs();
  if (max_header_pairs_ == 0) {
    max_header_pairs_ = DEFAULT_MAX_HEADER_LIST_PAIRS;
  }
  current_headers_.reserve(std::min(max_header_pairs_, 12u));

  // Limit the number of header octets
  max_header_length_ =
      std::min(
        nghttp2_session_get_local_settings(
          session->session(),
          NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE),
      MAX_MAX_HEADER_LIST_SIZE);

  if (options & STREAM_OPTION_GET_TRAILERS)
    set_has_trailers();

  PushStreamListener(&stream_listener_);

  if (options & STREAM_OPTION_EMPTY_PAYLOAD)
    Shutdown();
  session->AddStream(this);
}

Http2Stream::~Http2Stream() {
  Debug(this, "tearing down stream");
}

void Http2Stream::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("current_headers", current_headers_);
  tracker->TrackField("queue", queue_);
}

std::string Http2Stream::diagnostic_name() const {
  const Http2Session* sess = session();
  const std::string sname =
      sess ? sess->diagnostic_name() : "session already destroyed";
  return "HttpStream " + std::to_string(id()) + " (" +
         std::to_string(static_cast<int64_t>(get_async_id())) + ") [" + sname +
         "]";
}

// Notify the Http2Stream that a new block of HEADERS is being processed.
void Http2Stream::StartHeaders(nghttp2_headers_category category) {
  Debug(this, "starting headers, category: %d", category);
  CHECK(!this->is_destroyed());
  session_->DecrementCurrentSessionMemory(current_headers_length_);
  current_headers_length_ = 0;
  current_headers_.clear();
  current_headers_category_ = category;
}


nghttp2_stream* Http2Stream::operator*() const { return stream(); }

nghttp2_stream* Http2Stream::stream() const {
  return nghttp2_session_find_stream(session_->session(), id_);
}

void Http2Stream::Close(int32_t code) {
  CHECK(!this->is_destroyed());
  set_closed();
  code_ = code;
  Debug(this, "closed with code %d", code);
}

ShutdownWrap* Http2Stream::CreateShutdownWrap(Local<Object> object) {
  // DoShutdown() always finishes synchronously, so there's no need to create
  // a structure to store asynchronous context.
  return nullptr;
}

int Http2Stream::DoShutdown(ShutdownWrap* req_wrap) {
  if (is_destroyed())
    return UV_EPIPE;

  {
    Http2Scope h2scope(this);
    set_not_writable();
    CHECK_NE(nghttp2_session_resume_data(
        session_->session(), id_),
        NGHTTP2_ERR_NOMEM);
    Debug(this, "writable side shutdown");
  }
  return 1;
}

// Destroy the Http2Stream and render it unusable. Actual resources for the
// Stream will not be freed until the next tick of the Node.js event loop
// using the SetImmediate queue.
void Http2Stream::Destroy() {
  // Do nothing if this stream instance is already destroyed
  if (is_destroyed())
    return;
  if (session_->has_pending_rststream(id_))
    FlushRstStream();
  set_destroyed();

  Debug(this, "destroying stream");

  // Wait until the start of the next loop to delete because there
  // may still be some pending operations queued for this stream.
  BaseObjectPtr<Http2Stream> strong_ref = session_->RemoveStream(id_);
  if (strong_ref) {
    env()->SetImmediate([this, strong_ref = std::move(strong_ref)](
        Environment* env) {
      // Free any remaining outgoing data chunks here. This should be done
      // here because it's possible for destroy to have been called while
      // we still have queued outbound writes.
      while (!queue_.empty()) {
        NgHttp2StreamWrite& head = queue_.front();
        if (head.req_wrap)
          WriteWrap::FromObject(head.req_wrap)->Done(UV_ECANCELED);
        queue_.pop();
      }

      // We can destroy the stream now if there are no writes for it
      // already on the socket. Otherwise, we'll wait for the garbage collector
      // to take care of cleaning up.
      if (session() == nullptr ||
          !session()->HasWritesOnSocketForStream(this)) {
        // Delete once strong_ref goes out of scope.
        Detach();
      }
    });
  }

  statistics_.end_time = uv_hrtime();
  session_->statistics_.stream_average_duration =
      ((statistics_.end_time - statistics_.start_time) /
          session_->statistics_.stream_count) / 1e6;
  EmitStatistics();
}


// Initiates a response on the Http2Stream using data provided via the
// StreamBase Streams API.
int Http2Stream::SubmitResponse(const Http2Headers& headers, int options) {
  CHECK(!this->is_destroyed());
  Http2Scope h2scope(this);
  Debug(this, "submitting response");
  if (options & STREAM_OPTION_GET_TRAILERS)
    set_has_trailers();

  if (!is_writable())
    options |= STREAM_OPTION_EMPTY_PAYLOAD;

  Http2Stream::Provider::Stream prov(this, options);
  int ret = nghttp2_submit_response(
      session_->session(),
      id_,
      headers.data(),
      headers.length(),
      *prov);
  CHECK_NE(ret, NGHTTP2_ERR_NOMEM);
  return ret;
}


// Submit informational headers for a stream.
int Http2Stream::SubmitInfo(const Http2Headers& headers) {
  CHECK(!this->is_destroyed());
  Http2Scope h2scope(this);
  Debug(this, "sending %d informational headers", headers.length());
  int ret = nghttp2_submit_headers(
      session_->session(),
      NGHTTP2_FLAG_NONE,
      id_,
      nullptr,
      headers.data(),
      headers.length(),
      nullptr);
  CHECK_NE(ret, NGHTTP2_ERR_NOMEM);
  return ret;
}

void Http2Stream::OnTrailers() {
  Debug(this, "let javascript know we are ready for trailers");
  CHECK(!this->is_destroyed());
  Isolate* isolate = env()->isolate();
  HandleScope scope(isolate);
  Local<Context> context = env()->context();
  Context::Scope context_scope(context);
  set_has_trailers(false);
  MakeCallback(env()->http2session_on_stream_trailers_function(), 0, nullptr);
}

// Submit informational headers for a stream.
int Http2Stream::SubmitTrailers(const Http2Headers& headers) {
  CHECK(!this->is_destroyed());
  Http2Scope h2scope(this);
  Debug(this, "sending %d trailers", headers.length());
  int ret;
  // Sending an empty trailers frame poses problems in Safari, Edge & IE.
  // Instead we can just send an empty data frame with NGHTTP2_FLAG_END_STREAM
  // to indicate that the stream is ready to be closed.
  if (headers.length() == 0) {
    Http2Stream::Provider::Stream prov(this, 0);
    ret = nghttp2_submit_data(
        session_->session(),
        NGHTTP2_FLAG_END_STREAM,
        id_,
        *prov);
  } else {
    ret = nghttp2_submit_trailer(
        session_->session(),
        id_,
        headers.data(),
        headers.length());
  }
  CHECK_NE(ret, NGHTTP2_ERR_NOMEM);
  return ret;
}

// Submit a PRIORITY frame to the connected peer.
int Http2Stream::SubmitPriority(const Http2Priority& priority,
                                bool silent) {
  CHECK(!this->is_destroyed());
  Http2Scope h2scope(this);
  Debug(this, "sending priority spec");
  int ret = silent ?
      nghttp2_session_change_stream_priority(
          session_->session(),
          id_,
          &priority) :
      nghttp2_submit_priority(
          session_->session(),
          NGHTTP2_FLAG_NONE,
          id_, &priority);
  CHECK_NE(ret, NGHTTP2_ERR_NOMEM);
  return ret;
}

// Closes the Http2Stream by submitting an RST_STREAM frame to the connected
// peer.
void Http2Stream::SubmitRstStream(const uint32_t code) {
  CHECK(!this->is_destroyed());
  code_ = code;

  auto is_stream_cancel = [](const uint32_t code) {
    return code == NGHTTP2_CANCEL;
  };

  // If RST_STREAM frame is received with error code NGHTTP2_CANCEL,
  // add it to the pending list and don't force purge the data. It is
  // to avoids the double free error due to unwanted behavior of nghttp2.

  // Add stream to the pending list only if it is received with scope
  // below in the stack. The pending list may not get processed
  // if RST_STREAM received is not in scope and added to the list
  // causing endpoint to hang.
  if (session_->is_in_scope() && is_stream_cancel(code)) {
      session_->AddPendingRstStream(id_);
      return;
  }


  // If possible, force a purge of any currently pending data here to make sure
  // it is sent before closing the stream. If it returns non-zero then we need
  // to wait until the current write finishes and try again to avoid nghttp2
  // behaviour where it prioritizes RstStream over everything else.
  if (session_->SendPendingData() != 0) {
    session_->AddPendingRstStream(id_);
    return;
  }

  FlushRstStream();
}

void Http2Stream::FlushRstStream() {
  if (is_destroyed())
    return;
  Http2Scope h2scope(this);
  CHECK_EQ(nghttp2_submit_rst_stream(
      session_->session(),
      NGHTTP2_FLAG_NONE,
      id_,
      code_), 0);
}


// Submit a push promise and create the associated Http2Stream if successful.
Http2Stream* Http2Stream::SubmitPushPromise(const Http2Headers& headers,
                                            int32_t* ret,
                                            int options) {
  CHECK(!this->is_destroyed());
  Http2Scope h2scope(this);
  Debug(this, "sending push promise");
  *ret = nghttp2_submit_push_promise(
      session_->session(),
      NGHTTP2_FLAG_NONE,
      id_,
      headers.data(),
      headers.length(),
      nullptr);
  CHECK_NE(*ret, NGHTTP2_ERR_NOMEM);
  Http2Stream* stream = nullptr;
  if (*ret > 0) {
    stream = Http2Stream::New(
        session_.get(), *ret, NGHTTP2_HCAT_HEADERS, options);
  }

  return stream;
}

// Switch the StreamBase into flowing mode to begin pushing chunks of data
// out to JS land.
int Http2Stream::ReadStart() {
  Http2Scope h2scope(this);
  CHECK(!this->is_destroyed());
  set_reading();

  Debug(this, "reading starting");

  // Tell nghttp2 about our consumption of the data that was handed
  // off to JS land.
  nghttp2_session_consume_stream(
      session_->session(),
      id_,
      inbound_consumed_data_while_paused_);
  inbound_consumed_data_while_paused_ = 0;

  return 0;
}

// Switch the StreamBase into paused mode.
int Http2Stream::ReadStop() {
  CHECK(!this->is_destroyed());
  if (!is_reading())
    return 0;
  set_paused();
  Debug(this, "reading stopped");
  return 0;
}

// The Http2Stream class is a subclass of StreamBase. The DoWrite method
// receives outbound chunks of data to send as outbound DATA frames. These
// are queued in an internal linked list of uv_buf_t structs that are sent
// when nghttp2 is ready to serialize the data frame.
//
// Queue the given set of uv_but_t handles for writing to an
// nghttp2_stream. The WriteWrap's Done callback will be invoked once the
// chunks of data have been flushed to the underlying nghttp2_session.
// Note that this does *not* mean that the data has been flushed
// to the socket yet.
int Http2Stream::DoWrite(WriteWrap* req_wrap,
                         uv_buf_t* bufs,
                         size_t nbufs,
                         uv_stream_t* send_handle) {
  CHECK_NULL(send_handle);
  Http2Scope h2scope(this);
  if (!is_writable() || is_destroyed()) {
    return UV_EOF;
  }
  Debug(this, "queuing %d buffers to send", nbufs);
  for (size_t i = 0; i < nbufs; ++i) {
    // Store the req_wrap on the last write info in the queue, so that it is
    // only marked as finished once all buffers associated with it are finished.
    queue_.emplace(NgHttp2StreamWrite {
      BaseObjectPtr<AsyncWrap>(
          i == nbufs - 1 ? req_wrap->GetAsyncWrap() : nullptr),
      bufs[i]
    });
    IncrementAvailableOutboundLength(bufs[i].len);
  }
  CHECK_NE(nghttp2_session_resume_data(
      session_->session(),
      id_), NGHTTP2_ERR_NOMEM);
  return 0;
}

// Ads a header to the Http2Stream. Note that the header name and value are
// provided using a buffer structure provided by nghttp2 that allows us to
// avoid unnecessary memcpy's. Those buffers are ref counted. The ref count
// is incremented here and are decremented when the header name and values
// are garbage collected later.
bool Http2Stream::AddHeader(nghttp2_rcbuf* name,
                            nghttp2_rcbuf* value,
                            uint8_t flags) {
  CHECK(!this->is_destroyed());

  if (Http2RcBufferPointer::IsZeroLength(name))
    return true;  // Ignore empty headers.

  Http2Header header(env(), name, value, flags);
  size_t length = header.length() + 32;
  // A header can only be added if we have not exceeded the maximum number
  // of headers and the session has memory available for it.
  if (!session_->has_available_session_memory(length) ||
      current_headers_.size() == max_header_pairs_ ||
      current_headers_length_ + length > max_header_length_) {
    return false;
  }

  if (statistics_.first_header == 0)
    statistics_.first_header = uv_hrtime();

  current_headers_.push_back(std::move(header));

  current_headers_length_ += length;
  session_->IncrementCurrentSessionMemory(length);
  return true;
}

// A Provider is the thing that provides outbound DATA frame data.
Http2Stream::Provider::Provider(Http2Stream* stream, int options) {
  CHECK(!stream->is_destroyed());
  provider_.source.ptr = stream;
  empty_ = options & STREAM_OPTION_EMPTY_PAYLOAD;
}

Http2Stream::Provider::Provider(int options) {
  provider_.source.ptr = nullptr;
  empty_ = options & STREAM_OPTION_EMPTY_PAYLOAD;
}

Http2Stream::Provider::~Provider() {
  provider_.source.ptr = nullptr;
}

// The Stream Provider pulls data from a linked list of uv_buf_t structs
// built via the StreamBase API and the Streams js API.
Http2Stream::Provider::Stream::Stream(int options)
    : Http2Stream::Provider(options) {
  provider_.read_callback = Http2Stream::Provider::Stream::OnRead;
}

Http2Stream::Provider::Stream::Stream(Http2Stream* stream, int options)
    : Http2Stream::Provider(stream, options) {
  provider_.read_callback = Http2Stream::Provider::Stream::OnRead;
}

ssize_t Http2Stream::Provider::Stream::OnRead(nghttp2_session* handle,
                                              int32_t id,
                                              uint8_t* buf,
                                              size_t length,
                                              uint32_t* flags,
                                              nghttp2_data_source* source,
                                              void* user_data) {
  Http2Session* session = static_cast<Http2Session*>(user_data);
  Debug(session, "reading outbound data for stream %d", id);
  BaseObjectPtr<Http2Stream> stream = session->FindStream(id);
  if (!stream) return 0;
  if (stream->statistics_.first_byte_sent == 0)
    stream->statistics_.first_byte_sent = uv_hrtime();
  CHECK_EQ(id, stream->id());

  size_t amount = 0;          // amount of data being sent in this data frame.

  // Remove all empty chunks from the head of the queue.
  // This is done here so that .write('', cb) is still a meaningful way to
  // find out when the HTTP2 stream wants to consume data, and because the
  // StreamBase API allows empty input chunks.
  while (!stream->queue_.empty() && stream->queue_.front().buf.len == 0) {
    BaseObjectPtr<AsyncWrap> finished =
        std::move(stream->queue_.front().req_wrap);
    stream->queue_.pop();
    if (finished)
      WriteWrap::FromObject(finished)->Done(0);
  }

  if (!stream->queue_.empty()) {
    Debug(session, "stream %d has pending outbound data", id);
    amount = std::min(stream->available_outbound_length_, length);
    Debug(session, "sending %d bytes for data frame on stream %d", amount, id);
    if (amount > 0) {
      // Just return the length, let Http2Session::OnSendData take care of
      // actually taking the buffers out of the queue.
      *flags |= NGHTTP2_DATA_FLAG_NO_COPY;
      stream->DecrementAvailableOutboundLength(amount);
    }
  }

  if (amount == 0 && stream->is_writable()) {
    CHECK(stream->queue_.empty());
    Debug(session, "deferring stream %d", id);
    stream->EmitWantsWrite(length);
    if (stream->available_outbound_length_ > 0 || !stream->is_writable()) {
      // EmitWantsWrite() did something interesting synchronously, restart:
      return OnRead(handle, id, buf, length, flags, source, user_data);
    }
    return NGHTTP2_ERR_DEFERRED;
  }

  if (stream->available_outbound_length_ == 0 && !stream->is_writable()) {
    Debug(session, "no more data for stream %d", id);
    *flags |= NGHTTP2_DATA_FLAG_EOF;
    if (stream->has_trailers()) {
      *flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
      stream->OnTrailers();
    }
  }

  stream->statistics_.sent_bytes += amount;
  return amount;
}

void Http2Stream::IncrementAvailableOutboundLength(size_t amount) {
  available_outbound_length_ += amount;
  session_->IncrementCurrentSessionMemory(amount);
}

void Http2Stream::DecrementAvailableOutboundLength(size_t amount) {
  available_outbound_length_ -= amount;
  session_->DecrementCurrentSessionMemory(amount);
}


// Implementation of the JavaScript API

// Fetches the string description of a nghttp2 error code and passes that
// back to JS land
void HttpErrorString(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  uint32_t val = args[0]->Uint32Value(env->context()).ToChecked();
  args.GetReturnValue().Set(
      OneByteString(
          env->isolate(),
          reinterpret_cast<const uint8_t*>(nghttp2_strerror(val))));
}


// Serializes the settings object into a Buffer instance that
// would be suitable, for instance, for creating the Base64
// output for an HTTP2-Settings header field.
void PackSettings(const FunctionCallbackInfo<Value>& args) {
  Http2State* state = Realm::GetBindingData<Http2State>(args);
  args.GetReturnValue().Set(Http2Settings::Pack(state));
}

// A TypedArray instance is shared between C++ and JS land to contain the
// default SETTINGS. RefreshDefaultSettings updates that TypedArray with the
// default values.
void RefreshDefaultSettings(const FunctionCallbackInfo<Value>& args) {
  Http2State* state = Realm::GetBindingData<Http2State>(args);
  Http2Settings::RefreshDefaults(state);
}

// Sets the next stream ID the Http2Session. If successful, returns true.
void Http2Session::SetNextStreamID(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  int32_t id = args[0]->Int32Value(env->context()).ToChecked();
  if (nghttp2_session_set_next_stream_id(session->session(), id) < 0) {
    Debug(session, "failed to set next stream id to %d", id);
    return args.GetReturnValue().Set(false);
  }
  args.GetReturnValue().Set(true);
  Debug(session, "set next stream id to %d", id);
}

// Set local window size (local endpoints's window size) to the given
// window_size for the stream denoted by 0.
// This function returns 0 if it succeeds, or one of a negative codes
void Http2Session::SetLocalWindowSize(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

  int32_t window_size = args[0]->Int32Value(env->context()).ToChecked();

  int result = nghttp2_session_set_local_window_size(
      session->session(), NGHTTP2_FLAG_NONE, 0, window_size);

  args.GetReturnValue().Set(result);

  Debug(session, "set local window size to %d", window_size);
}

// A TypedArray instance is shared between C++ and JS land to contain the
// SETTINGS (either remote or local). RefreshSettings updates the current
// values established for each of the settings so those can be read in JS land.
template <get_setting fn, bool local>
void Http2Session::RefreshSettings(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  Http2Settings::Update(session, fn, local);
  Debug(session, "settings refreshed for session");
}

// A TypedArray instance is shared between C++ and JS land to contain state
// information of the current Http2Session. This updates the values in the
// TypedArray so those can be read in JS land.
void Http2Session::RefreshState(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  Debug(session, "refreshing state");

  AliasedFloat64Array& buffer = session->http2_state()->session_state_buffer;

  nghttp2_session* s = session->session();

  buffer[IDX_SESSION_STATE_EFFECTIVE_LOCAL_WINDOW_SIZE] =
      nghttp2_session_get_effective_local_window_size(s);
  buffer[IDX_SESSION_STATE_EFFECTIVE_RECV_DATA_LENGTH] =
      nghttp2_session_get_effective_recv_data_length(s);
  buffer[IDX_SESSION_STATE_NEXT_STREAM_ID] =
      nghttp2_session_get_next_stream_id(s);
  buffer[IDX_SESSION_STATE_LOCAL_WINDOW_SIZE] =
      nghttp2_session_get_local_window_size(s);
  buffer[IDX_SESSION_STATE_LAST_PROC_STREAM_ID] =
      nghttp2_session_get_last_proc_stream_id(s);
  buffer[IDX_SESSION_STATE_REMOTE_WINDOW_SIZE] =
      nghttp2_session_get_remote_window_size(s);
  buffer[IDX_SESSION_STATE_OUTBOUND_QUEUE_SIZE] =
      static_cast<double>(nghttp2_session_get_outbound_queue_size(s));
  buffer[IDX_SESSION_STATE_HD_DEFLATE_DYNAMIC_TABLE_SIZE] =
      static_cast<double>(nghttp2_session_get_hd_deflate_dynamic_table_size(s));
  buffer[IDX_SESSION_STATE_HD_INFLATE_DYNAMIC_TABLE_SIZE] =
      static_cast<double>(nghttp2_session_get_hd_inflate_dynamic_table_size(s));
}


// Constructor for new Http2Session instances.
void Http2Session::New(const FunctionCallbackInfo<Value>& args) {
  Realm* realm = Realm::GetCurrent(args);
  Http2State* state = realm->GetBindingData<Http2State>();

  CHECK(args.IsConstructCall());
  SessionType type = static_cast<SessionType>(
      args[0]->Int32Value(realm->context()).ToChecked());
  Http2Session* session = new Http2Session(state, args.This(), type);
  Debug(session, "session created");
}


// Binds the Http2Session with a StreamBase used for i/o
void Http2Session::Consume(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  CHECK(args[0]->IsObject());
  session->Consume(args[0].As<Object>());
}

// Destroys the Http2Session instance and renders it unusable
void Http2Session::Destroy(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  Debug(session, "destroying session");
  Environment* env = Environment::GetCurrent(args);
  Local<Context> context = env->context();

  uint32_t code = args[0]->Uint32Value(context).ToChecked();
  session->Close(code, args[1]->IsTrue());
}

// Submits a new request on the Http2Session and returns either an error code
// or the Http2Stream object.
void Http2Session::Request(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  Environment* env = session->env();

  Local<Array> headers = args[0].As<Array>();
  int32_t options = args[1]->Int32Value(env->context()).ToChecked();

  Debug(session, "request submitted");

  int32_t ret = 0;
  Http2Stream* stream =
      session->Http2Session::SubmitRequest(
          Http2Priority(env, args[2], args[3], args[4]),
          Http2Headers(env, headers),
          &ret,
          static_cast<int>(options));

  if (ret <= 0 || stream == nullptr) {
    Debug(session, "could not submit request: %s", nghttp2_strerror(ret));
    return args.GetReturnValue().Set(ret);
  }

  Debug(session, "request submitted, new stream id %d", stream->id());
  args.GetReturnValue().Set(stream->object());
}

// Submits a GOAWAY frame to signal that the Http2Session is in the process
// of shutting down. Note that this function does not actually alter the
// state of the Http2Session, it's simply a notification.
void Http2Session::Goaway(uint32_t code,
                          int32_t lastStreamID,
                          const uint8_t* data,
                          size_t len) {
  if (is_destroyed())
    return;

  Http2Scope h2scope(this);
  // the last proc stream id is the most recently created Http2Stream.
  if (lastStreamID <= 0)
    lastStreamID = nghttp2_session_get_last_proc_stream_id(session_.get());
  Debug(this, "submitting goaway");
  nghttp2_submit_goaway(session_.get(), NGHTTP2_FLAG_NONE,
                        lastStreamID, code, data, len);
}

// Submits a GOAWAY frame to signal that the Http2Session is in the process
// of shutting down. The opaque data argument is an optional TypedArray that
// can be used to send debugging data to the connected peer.
void Http2Session::Goaway(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Context> context = env->context();
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

  uint32_t code = args[0]->Uint32Value(context).ToChecked();
  int32_t lastStreamID = args[1]->Int32Value(context).ToChecked();
  ArrayBufferViewContents<uint8_t> opaque_data;

  if (args[2]->IsArrayBufferView()) {
    opaque_data.Read(args[2].As<ArrayBufferView>());
  }

  session->Goaway(code, lastStreamID, opaque_data.data(), opaque_data.length());
}

// Update accounting of data chunks. This is used primarily to manage timeout
// logic when using the FD Provider.
void Http2Session::UpdateChunksSent(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Isolate* isolate = env->isolate();
  HandleScope scope(isolate);
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

  uint32_t length = session->chunks_sent_since_last_write_;

  session->object()->Set(env->context(),
                         env->chunks_sent_since_last_write_string(),
                         Integer::NewFromUnsigned(isolate, length)).Check();

  args.GetReturnValue().Set(length);
}

// Submits an RST_STREAM frame effectively closing the Http2Stream. Note that
// this *WILL* alter the state of the stream, causing the OnStreamClose
// callback to the triggered.
void Http2Stream::RstStream(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Context> context = env->context();
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
  uint32_t code = args[0]->Uint32Value(context).ToChecked();
  Debug(stream, "sending rst_stream with code %d", code);
  stream->SubmitRstStream(code);
}

// Initiates a response on the Http2Stream using the StreamBase API to provide
// outbound DATA frames.
void Http2Stream::Respond(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());

  Local<Array> headers = args[0].As<Array>();
  int32_t options = args[1]->Int32Value(env->context()).ToChecked();

  args.GetReturnValue().Set(
      stream->SubmitResponse(
          Http2Headers(env, headers),
          static_cast<int>(options)));
  Debug(stream, "response submitted");
}


// Submits informational headers on the Http2Stream
void Http2Stream::Info(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());

  Local<Array> headers = args[0].As<Array>();

  args.GetReturnValue().Set(stream->SubmitInfo(Http2Headers(env, headers)));
}

// Submits trailing headers on the Http2Stream
void Http2Stream::Trailers(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());

  Local<Array> headers = args[0].As<Array>();

  args.GetReturnValue().Set(
      stream->SubmitTrailers(Http2Headers(env, headers)));
}

// Grab the numeric id of the Http2Stream
void Http2Stream::GetID(const FunctionCallbackInfo<Value>& args) {
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
  args.GetReturnValue().Set(stream->id());
}

// Destroy the Http2Stream, rendering it no longer usable
void Http2Stream::Destroy(const FunctionCallbackInfo<Value>& args) {
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());
  Debug(stream, "destroying stream");
  stream->Destroy();
}

// Initiate a Push Promise and create the associated Http2Stream
void Http2Stream::PushPromise(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Stream* parent;
  ASSIGN_OR_RETURN_UNWRAP(&parent, args.This());

  Local<Array> headers = args[0].As<Array>();
  int32_t options = args[1]->Int32Value(env->context()).ToChecked();

  Debug(parent, "creating push promise");

  int32_t ret = 0;
  Http2Stream* stream =
      parent->SubmitPushPromise(
          Http2Headers(env, headers),
          &ret,
          static_cast<int>(options));

  if (ret <= 0 || stream == nullptr) {
    Debug(parent, "failed to create push stream: %d", ret);
    return args.GetReturnValue().Set(ret);
  }
  Debug(parent, "push stream %d created", stream->id());
  args.GetReturnValue().Set(stream->object());
}

// Send a PRIORITY frame
void Http2Stream::Priority(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());

  CHECK_EQ(stream->SubmitPriority(
      Http2Priority(env, args[0], args[1], args[2]),
      args[3]->IsTrue()), 0);
  Debug(stream, "priority submitted");
}

// A TypedArray shared by C++ and JS land is used to communicate state
// information about the Http2Stream. This updates the values in that
// TypedArray so that the state can be read by JS.
void Http2Stream::RefreshState(const FunctionCallbackInfo<Value>& args) {
  Http2Stream* stream;
  ASSIGN_OR_RETURN_UNWRAP(&stream, args.This());

  Debug(stream, "refreshing state");

  CHECK_NOT_NULL(stream->session());
  AliasedFloat64Array& buffer =
      stream->session()->http2_state()->stream_state_buffer;

  nghttp2_stream* str = stream->stream();
  nghttp2_session* s = stream->session()->session();

  if (str == nullptr) {
    buffer[IDX_STREAM_STATE] = NGHTTP2_STREAM_STATE_IDLE;
    buffer[IDX_STREAM_STATE_WEIGHT] =
        buffer[IDX_STREAM_STATE_SUM_DEPENDENCY_WEIGHT] =
        buffer[IDX_STREAM_STATE_LOCAL_CLOSE] =
        buffer[IDX_STREAM_STATE_REMOTE_CLOSE] =
        buffer[IDX_STREAM_STATE_LOCAL_WINDOW_SIZE] = 0;
  } else {
    buffer[IDX_STREAM_STATE] =
        nghttp2_stream_get_state(str);
    buffer[IDX_STREAM_STATE_WEIGHT] =
        nghttp2_stream_get_weight(str);
    buffer[IDX_STREAM_STATE_SUM_DEPENDENCY_WEIGHT] =
        nghttp2_stream_get_sum_dependency_weight(str);
    buffer[IDX_STREAM_STATE_LOCAL_CLOSE] =
        nghttp2_session_get_stream_local_close(s, stream->id());
    buffer[IDX_STREAM_STATE_REMOTE_CLOSE] =
        nghttp2_session_get_stream_remote_close(s, stream->id());
    buffer[IDX_STREAM_STATE_LOCAL_WINDOW_SIZE] =
        nghttp2_session_get_stream_local_window_size(s, stream->id());
  }
}

void Http2Session::AltSvc(int32_t id,
                          uint8_t* origin,
                          size_t origin_len,
                          uint8_t* value,
                          size_t value_len) {
  Http2Scope h2scope(this);
  CHECK_EQ(nghttp2_submit_altsvc(session_.get(), NGHTTP2_FLAG_NONE, id,
                                 origin, origin_len, value, value_len), 0);
}

void Http2Session::Origin(const Origins& origins) {
  Http2Scope h2scope(this);
  CHECK_EQ(nghttp2_submit_origin(
      session_.get(),
      NGHTTP2_FLAG_NONE,
      *origins,
      origins.length()), 0);
}

// Submits an AltSvc frame to be sent to the connected peer.
void Http2Session::AltSvc(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

  int32_t id = args[0]->Int32Value(env->context()).ToChecked();

  // origin and value are both required to be ASCII, handle them as such.
  Local<String> origin_str = args[1]->ToString(env->context()).ToLocalChecked();
  Local<String> value_str = args[2]->ToString(env->context()).ToLocalChecked();

  if (origin_str.IsEmpty() || value_str.IsEmpty())
    return;

  size_t origin_len = origin_str->Length();
  size_t value_len = value_str->Length();

  CHECK_LE(origin_len + value_len, 16382);  // Max permitted for ALTSVC
  // Verify that origin len != 0 if stream id == 0, or
  // that origin len == 0 if stream id != 0
  CHECK((origin_len != 0 && id == 0) || (origin_len == 0 && id != 0));

  MaybeStackBuffer<uint8_t> origin(origin_len);
  MaybeStackBuffer<uint8_t> value(value_len);
  origin_str->WriteOneByte(env->isolate(), *origin);
  value_str->WriteOneByte(env->isolate(), *value);

  session->AltSvc(id, *origin, origin_len, *value, value_len);
}

void Http2Session::Origin(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  Local<Context> context = env->context();
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

  Local<String> origin_string = args[0].As<String>();
  size_t count = args[1]->Int32Value(context).ToChecked();

  session->Origin(Origins(env, origin_string, count));
}

// Submits a PING frame to be sent to the connected peer.
void Http2Session::Ping(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());

  // A PING frame may have exactly 8 bytes of payload data. If not provided,
  // then the current hrtime will be used as the payload.
  ArrayBufferViewContents<uint8_t, 8> payload;
  if (args[0]->IsArrayBufferView()) {
    payload.Read(args[0].As<ArrayBufferView>());
    CHECK_EQ(payload.length(), 8);
  }

  CHECK(args[1]->IsFunction());
  args.GetReturnValue().Set(
      session->AddPing(payload.data(), args[1].As<Function>()));
}

// Submits a SETTINGS frame for the Http2Session
void Http2Session::Settings(const FunctionCallbackInfo<Value>& args) {
  Http2Session* session;
  ASSIGN_OR_RETURN_UNWRAP(&session, args.This());
  CHECK(args[0]->IsFunction());
  args.GetReturnValue().Set(session->AddSettings(args[0].As<Function>()));
}

BaseObjectPtr<Http2Ping> Http2Session::PopPing() {
  BaseObjectPtr<Http2Ping> ping;
  if (!outstanding_pings_.empty()) {
    ping = std::move(outstanding_pings_.front());
    outstanding_pings_.pop();
    DecrementCurrentSessionMemory(sizeof(*ping));
  }
  return ping;
}

bool Http2Session::AddPing(const uint8_t* payload, Local<Function> callback) {
  Local<Object> obj;
  if (!env()->http2ping_constructor_template()
          ->NewInstance(env()->context())
              .ToLocal(&obj)) {
    return false;
  }

  BaseObjectPtr<Http2Ping> ping =
      MakeDetachedBaseObject<Http2Ping>(this, obj, callback);
  if (!ping)
    return false;

  if (outstanding_pings_.size() == max_outstanding_pings_) {
    ping->Done(false);
    return false;
  }

  IncrementCurrentSessionMemory(sizeof(*ping));
  // The Ping itself is an Async resource. When the acknowledgement is received,
  // the callback will be invoked and a notification sent out to JS land. The
  // notification will include the duration of the ping, allowing the round
  // trip to be measured.
  ping->Send(payload);

  outstanding_pings_.emplace(std::move(ping));
  return true;
}

BaseObjectPtr<Http2Settings> Http2Session::PopSettings() {
  BaseObjectPtr<Http2Settings> settings;
  if (!outstanding_settings_.empty()) {
    settings = std::move(outstanding_settings_.front());
    outstanding_settings_.pop();
    DecrementCurrentSessionMemory(sizeof(*settings));
  }
  return settings;
}

bool Http2Session::AddSettings(Local<Function> callback) {
  Local<Object> obj;
  if (!env()->http2settings_constructor_template()
          ->NewInstance(env()->context())
              .ToLocal(&obj)) {
    return false;
  }

  BaseObjectPtr<Http2Settings> settings =
      MakeDetachedBaseObject<Http2Settings>(this, obj, callback, 0);
  if (!settings)
    return false;

  if (outstanding_settings_.size() == max_outstanding_settings_) {
    settings->Done(false);
    return false;
  }

  IncrementCurrentSessionMemory(sizeof(*settings));
  settings->Send();
  outstanding_settings_.emplace(std::move(settings));
  return true;
}

Http2Ping::Http2Ping(
    Http2Session* session,
    Local<Object> obj,
    Local<Function> callback)
    : AsyncWrap(session->env(), obj, AsyncWrap::PROVIDER_HTTP2PING),
      session_(session),
      startTime_(uv_hrtime()) {
  callback_.Reset(env()->isolate(), callback);
}

void Http2Ping::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("callback", callback_);
}

Local<Function> Http2Ping::callback() const {
  return callback_.Get(env()->isolate());
}

void Http2Ping::Send(const uint8_t* payload) {
  CHECK(session_);
  uint8_t data[8];
  if (payload == nullptr) {
    memcpy(&data, &startTime_, arraysize(data));
    payload = data;
  }
  Http2Scope h2scope(session_.get());
  CHECK_EQ(nghttp2_submit_ping(
      session_->session(),
      NGHTTP2_FLAG_NONE,
      payload), 0);
}

void Http2Ping::Done(bool ack, const uint8_t* payload) {
  uint64_t duration_ns = uv_hrtime() - startTime_;
  double duration_ms = duration_ns / 1e6;
  if (session_) session_->statistics_.ping_rtt = duration_ns;

  Isolate* isolate = env()->isolate();
  HandleScope handle_scope(isolate);
  Context::Scope context_scope(env()->context());

  Local<Value> buf = Undefined(isolate);
  if (payload != nullptr) {
    buf = Buffer::Copy(isolate,
                       reinterpret_cast<const char*>(payload),
                       8).ToLocalChecked();
  }

  Local<Value> argv[] = {
      Boolean::New(isolate, ack), Number::New(isolate, duration_ms), buf};
  MakeCallback(callback(), arraysize(argv), argv);
}

void Http2Ping::DetachFromSession() {
  session_.reset();
}

void NgHttp2StreamWrite::MemoryInfo(MemoryTracker* tracker) const {
  if (req_wrap)
    tracker->TrackField("req_wrap", req_wrap);
  tracker->TrackField("buf", buf);
}

void SetCallbackFunctions(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_EQ(args.Length(), 11);

#define SET_FUNCTION(arg, name)                                               \
  CHECK(args[arg]->IsFunction());                                             \
  env->set_http2session_on_ ## name ## _function(args[arg].As<Function>());

  SET_FUNCTION(0, error)
  SET_FUNCTION(1, priority)
  SET_FUNCTION(2, settings)
  SET_FUNCTION(3, ping)
  SET_FUNCTION(4, headers)
  SET_FUNCTION(5, frame_error)
  SET_FUNCTION(6, goaway_data)
  SET_FUNCTION(7, altsvc)
  SET_FUNCTION(8, origin)
  SET_FUNCTION(9, stream_trailers)
  SET_FUNCTION(10, stream_close)

#undef SET_FUNCTION
}

#ifdef NODE_DEBUG_NGHTTP2
void NgHttp2Debug(const char* format, va_list args) {
  vfprintf(stderr, format, args);
}
#endif

void Http2State::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("root_buffer", root_buffer);
}

// Set up the process.binding('http2') binding.
void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Realm* realm = Realm::GetCurrent(context);
  Environment* env = realm->env();
  Isolate* isolate = env->isolate();
  HandleScope handle_scope(isolate);

  Http2State* const state = realm->AddBindingData<Http2State>(target);
  if (state == nullptr) return;

#define SET_STATE_TYPEDARRAY(name, field)             \
  target->Set(context,                                \
              FIXED_ONE_BYTE_STRING(isolate, (name)), \
              (field)).FromJust()

  // Initialize the buffer used to store the session state
  SET_STATE_TYPEDARRAY(
    "sessionState", state->session_state_buffer.GetJSArray());
  // Initialize the buffer used to store the stream state
  SET_STATE_TYPEDARRAY(
    "streamState", state->stream_state_buffer.GetJSArray());
  SET_STATE_TYPEDARRAY(
    "settingsBuffer", state->settings_buffer.GetJSArray());
  SET_STATE_TYPEDARRAY(
    "optionsBuffer", state->options_buffer.GetJSArray());
  SET_STATE_TYPEDARRAY(
    "streamStats", state->stream_stats_buffer.GetJSArray());
  SET_STATE_TYPEDARRAY(
    "sessionStats", state->session_stats_buffer.GetJSArray());
#undef SET_STATE_TYPEDARRAY

  NODE_DEFINE_CONSTANT(target, kBitfield);
  NODE_DEFINE_CONSTANT(target, kSessionPriorityListenerCount);
  NODE_DEFINE_CONSTANT(target, kSessionFrameErrorListenerCount);
  NODE_DEFINE_CONSTANT(target, kSessionMaxInvalidFrames);
  NODE_DEFINE_CONSTANT(target, kSessionMaxRejectedStreams);
  NODE_DEFINE_CONSTANT(target, kSessionUint8FieldCount);

  NODE_DEFINE_CONSTANT(target, kSessionHasRemoteSettingsListeners);
  NODE_DEFINE_CONSTANT(target, kSessionRemoteSettingsIsUpToDate);
  NODE_DEFINE_CONSTANT(target, kSessionHasPingListeners);
  NODE_DEFINE_CONSTANT(target, kSessionHasAltsvcListeners);

  // Method to fetch the nghttp2 string description of an nghttp2 error code
  SetMethod(context, target, "nghttp2ErrorString", HttpErrorString);
  SetMethod(context, target, "refreshDefaultSettings", RefreshDefaultSettings);
  SetMethod(context, target, "packSettings", PackSettings);
  SetMethod(context, target, "setCallbackFunctions", SetCallbackFunctions);

  Local<FunctionTemplate> ping = FunctionTemplate::New(env->isolate());
  ping->SetClassName(FIXED_ONE_BYTE_STRING(env->isolate(), "Http2Ping"));
  ping->Inherit(AsyncWrap::GetConstructorTemplate(env));
  Local<ObjectTemplate> pingt = ping->InstanceTemplate();
  pingt->SetInternalFieldCount(Http2Ping::kInternalFieldCount);
  env->set_http2ping_constructor_template(pingt);

  Local<FunctionTemplate> setting = FunctionTemplate::New(env->isolate());
  setting->Inherit(AsyncWrap::GetConstructorTemplate(env));
  Local<ObjectTemplate> settingt = setting->InstanceTemplate();
  settingt->SetInternalFieldCount(AsyncWrap::kInternalFieldCount);
  env->set_http2settings_constructor_template(settingt);

  Local<FunctionTemplate> stream = FunctionTemplate::New(env->isolate());
  SetProtoMethod(isolate, stream, "id", Http2Stream::GetID);
  SetProtoMethod(isolate, stream, "destroy", Http2Stream::Destroy);
  SetProtoMethod(isolate, stream, "priority", Http2Stream::Priority);
  SetProtoMethod(isolate, stream, "pushPromise", Http2Stream::PushPromise);
  SetProtoMethod(isolate, stream, "info", Http2Stream::Info);
  SetProtoMethod(isolate, stream, "trailers", Http2Stream::Trailers);
  SetProtoMethod(isolate, stream, "respond", Http2Stream::Respond);
  SetProtoMethod(isolate, stream, "rstStream", Http2Stream::RstStream);
  SetProtoMethod(isolate, stream, "refreshState", Http2Stream::RefreshState);
  stream->Inherit(AsyncWrap::GetConstructorTemplate(env));
  StreamBase::AddMethods(env, stream);
  Local<ObjectTemplate> streamt = stream->InstanceTemplate();
  streamt->SetInternalFieldCount(StreamBase::kInternalFieldCount);
  env->set_http2stream_constructor_template(streamt);
  SetConstructorFunction(context, target, "Http2Stream", stream);

  Local<FunctionTemplate> session =
      NewFunctionTemplate(isolate, Http2Session::New);
  session->InstanceTemplate()->SetInternalFieldCount(
      Http2Session::kInternalFieldCount);
  session->Inherit(AsyncWrap::GetConstructorTemplate(env));
  SetProtoMethod(isolate, session, "origin", Http2Session::Origin);
  SetProtoMethod(isolate, session, "altsvc", Http2Session::AltSvc);
  SetProtoMethod(isolate, session, "ping", Http2Session::Ping);
  SetProtoMethod(isolate, session, "consume", Http2Session::Consume);
  SetProtoMethod(isolate, session, "receive", Http2Session::Receive);
  SetProtoMethod(isolate, session, "destroy", Http2Session::Destroy);
  SetProtoMethod(isolate, session, "goaway", Http2Session::Goaway);
  SetProtoMethod(isolate, session, "settings", Http2Session::Settings);
  SetProtoMethod(isolate, session, "request", Http2Session::Request);
  SetProtoMethod(
      isolate, session, "setNextStreamID", Http2Session::SetNextStreamID);
  SetProtoMethod(
      isolate, session, "setLocalWindowSize", Http2Session::SetLocalWindowSize);
  SetProtoMethod(
      isolate, session, "updateChunksSent", Http2Session::UpdateChunksSent);
  SetProtoMethod(isolate, session, "refreshState", Http2Session::RefreshState);
  SetProtoMethod(
      isolate,
      session,
      "localSettings",
      Http2Session::RefreshSettings<nghttp2_session_get_local_settings, true>);
  SetProtoMethod(
      isolate,
      session,
      "remoteSettings",
      Http2Session::RefreshSettings<nghttp2_session_get_remote_settings,
                                    false>);
  SetConstructorFunction(context, target, "Http2Session", session);

  Local<Object> constants = Object::New(isolate);

  // This does allocate one more slot than needed but it's not used.
#define V(name) FIXED_ONE_BYTE_STRING(isolate, #name),
  Local<Value> error_code_names[] = {
    HTTP2_ERROR_CODES(V)
  };
#undef V

  Local<Array> name_for_error_code =
      Array::New(
          isolate,
          error_code_names,
          arraysize(error_code_names));

  target->Set(context,
              FIXED_ONE_BYTE_STRING(isolate, "nameForErrorCode"),
              name_for_error_code).Check();

#define V(constant) NODE_DEFINE_HIDDEN_CONSTANT(constants, constant);
  HTTP2_HIDDEN_CONSTANTS(V)
#undef V

#define V(constant) NODE_DEFINE_CONSTANT(constants, constant);
  HTTP2_CONSTANTS(V)
#undef V

  // NGHTTP2_DEFAULT_WEIGHT is a macro and not a regular define
  // it won't be set properly on the constants object if included
  // in the HTTP2_CONSTANTS macro.
  NODE_DEFINE_CONSTANT(constants, NGHTTP2_DEFAULT_WEIGHT);

#define V(NAME, VALUE)                                          \
  NODE_DEFINE_STRING_CONSTANT(constants, "HTTP2_HEADER_" # NAME, VALUE);
  HTTP_KNOWN_HEADERS(V)
#undef V

#define V(NAME, VALUE)                                          \
  NODE_DEFINE_STRING_CONSTANT(constants, "HTTP2_METHOD_" # NAME, VALUE);
  HTTP_KNOWN_METHODS(V)
#undef V

#define V(name, _) NODE_DEFINE_CONSTANT(constants, HTTP_STATUS_##name);
  HTTP_STATUS_CODES(V)
#undef V

  target->Set(context, env->constants_string(), constants).Check();

#ifdef NODE_DEBUG_NGHTTP2
  nghttp2_set_debug_vprintf_callback(NgHttp2Debug);
#endif
}
}  // namespace http2
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(http2, node::http2::Initialize)
