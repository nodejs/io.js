#ifndef SRC_NODE_HTTP2_STATE_H_
#define SRC_NODE_HTTP2_STATE_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "aliased_buffer.h"

struct nghttp2_rcbuf;

namespace node {
namespace http2 {

  enum Http2SettingsIndex {
    IDX_SETTINGS_HEADER_TABLE_SIZE,
    IDX_SETTINGS_ENABLE_PUSH,
    IDX_SETTINGS_INITIAL_WINDOW_SIZE,
    IDX_SETTINGS_MAX_FRAME_SIZE,
    IDX_SETTINGS_MAX_CONCURRENT_STREAMS,
    IDX_SETTINGS_MAX_HEADER_LIST_SIZE,
    IDX_SETTINGS_ENABLE_CONNECT_PROTOCOL,
    IDX_SETTINGS_COUNT
  };

  // number of max additional settings, thus settings not implemented by nghttp2
  const size_t MAX_ADDITIONAL_SETTINGS = 10;

  enum Http2SessionStateIndex {
    IDX_SESSION_STATE_EFFECTIVE_LOCAL_WINDOW_SIZE,
    IDX_SESSION_STATE_EFFECTIVE_RECV_DATA_LENGTH,
    IDX_SESSION_STATE_NEXT_STREAM_ID,
    IDX_SESSION_STATE_LOCAL_WINDOW_SIZE,
    IDX_SESSION_STATE_LAST_PROC_STREAM_ID,
    IDX_SESSION_STATE_REMOTE_WINDOW_SIZE,
    IDX_SESSION_STATE_OUTBOUND_QUEUE_SIZE,
    IDX_SESSION_STATE_HD_DEFLATE_DYNAMIC_TABLE_SIZE,
    IDX_SESSION_STATE_HD_INFLATE_DYNAMIC_TABLE_SIZE,
    IDX_SESSION_STATE_COUNT
  };

  enum Http2StreamStateIndex {
    IDX_STREAM_STATE,
    IDX_STREAM_STATE_WEIGHT,
    IDX_STREAM_STATE_SUM_DEPENDENCY_WEIGHT,
    IDX_STREAM_STATE_LOCAL_CLOSE,
    IDX_STREAM_STATE_REMOTE_CLOSE,
    IDX_STREAM_STATE_LOCAL_WINDOW_SIZE,
    IDX_STREAM_STATE_COUNT
  };

  enum Http2OptionsIndex {
    IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE,
    IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS,
    IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH,
    IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS,
    IDX_OPTIONS_PADDING_STRATEGY,
    IDX_OPTIONS_MAX_HEADER_LIST_PAIRS,
    IDX_OPTIONS_MAX_OUTSTANDING_PINGS,
    IDX_OPTIONS_MAX_OUTSTANDING_SETTINGS,
    IDX_OPTIONS_MAX_SESSION_MEMORY,
    IDX_OPTIONS_MAX_SETTINGS,
    IDX_OPTIONS_FLAGS
  };

  enum Http2StreamStatisticsIndex {
    IDX_STREAM_STATS_ID,
    IDX_STREAM_STATS_TIMETOFIRSTBYTE,
    IDX_STREAM_STATS_TIMETOFIRSTHEADER,
    IDX_STREAM_STATS_TIMETOFIRSTBYTESENT,
    IDX_STREAM_STATS_SENTBYTES,
    IDX_STREAM_STATS_RECEIVEDBYTES,
    IDX_STREAM_STATS_COUNT
  };

  enum Http2SessionStatisticsIndex {
    IDX_SESSION_STATS_TYPE,
    IDX_SESSION_STATS_PINGRTT,
    IDX_SESSION_STATS_FRAMESRECEIVED,
    IDX_SESSION_STATS_FRAMESSENT,
    IDX_SESSION_STATS_STREAMCOUNT,
    IDX_SESSION_STATS_STREAMAVERAGEDURATION,
    IDX_SESSION_STATS_DATA_SENT,
    IDX_SESSION_STATS_DATA_RECEIVED,
    IDX_SESSION_STATS_MAX_CONCURRENT_STREAMS,
    IDX_SESSION_STATS_COUNT
  };

class Http2State : public BaseObject {
 public:
  Http2State(Realm* realm, v8::Local<v8::Object> obj)
      : BaseObject(realm, obj),
        root_buffer(realm->isolate(), sizeof(http2_state_internal)),
        session_state_buffer(
            realm->isolate(),
            offsetof(http2_state_internal, session_state_buffer),
            IDX_SESSION_STATE_COUNT,
            root_buffer),
        stream_state_buffer(realm->isolate(),
                            offsetof(http2_state_internal, stream_state_buffer),
                            IDX_STREAM_STATE_COUNT,
                            root_buffer),
        stream_stats_buffer(realm->isolate(),
                            offsetof(http2_state_internal, stream_stats_buffer),
                            IDX_STREAM_STATS_COUNT,
                            root_buffer),
        session_stats_buffer(
            realm->isolate(),
            offsetof(http2_state_internal, session_stats_buffer),
            IDX_SESSION_STATS_COUNT,
            root_buffer),
        options_buffer(realm->isolate(),
                       offsetof(http2_state_internal, options_buffer),
                       IDX_OPTIONS_FLAGS + 1,
                       root_buffer),
        settings_buffer(
            realm->isolate(),
            offsetof(http2_state_internal, settings_buffer),
            IDX_SETTINGS_COUNT + 1 + 1 + 2 * MAX_ADDITIONAL_SETTINGS,
            root_buffer) {}

  AliasedUint8Array root_buffer;
  AliasedFloat64Array session_state_buffer;
  AliasedFloat64Array stream_state_buffer;
  AliasedFloat64Array stream_stats_buffer;
  AliasedFloat64Array session_stats_buffer;
  AliasedUint32Array options_buffer;
  AliasedUint32Array settings_buffer;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_SELF_SIZE(Http2State)
  SET_MEMORY_INFO_NAME(Http2State)

  SET_BINDING_ID(http2_binding_data)

 private:
  struct http2_state_internal {
    // doubles first so that they are always sizeof(double)-aligned
    double session_state_buffer[IDX_SESSION_STATE_COUNT];
    double stream_state_buffer[IDX_STREAM_STATE_COUNT];
    double stream_stats_buffer[IDX_STREAM_STATS_COUNT];
    double session_stats_buffer[IDX_SESSION_STATS_COUNT];
    uint32_t options_buffer[IDX_OPTIONS_FLAGS + 1];
    // first + 1: number of actual nghttp2 supported settings
    // second + 1: number of additional settings not supported by nghttp2
    // 2 * MAX_ADDITIONAL_SETTINGS: settings id and value for each
    // additional setting
    uint32_t settings_buffer[IDX_SETTINGS_COUNT + 1 + 1 +
                             2 * MAX_ADDITIONAL_SETTINGS];
  };
};

}  // namespace http2
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_HTTP2_STATE_H_
