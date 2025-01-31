#pragma once

#include <aliased_struct.h>
#include <env.h>
#include <nghttp3/nghttp3.h>
#include <ngtcp2/ngtcp2.h>
#include <node_errors.h>
#include <uv.h>
#include <v8.h>
#include <limits>

namespace node::quic {

#define NGTCP2_SUCCESS 0
#define NGTCP2_ERR(V) (V != NGTCP2_SUCCESS)
#define NGTCP2_OK(V) (V == NGTCP2_SUCCESS)

#define IF_QUIC_DEBUG(env)                                                     \
  if (env->enabled_debug_list()->enabled(DebugCategory::QUIC)) [[unlikely]]

#define DISALLOW_COPY(Name)                                                    \
  Name(const Name&) = delete;                                                  \
  Name& operator=(const Name&) = delete;

#define DISALLOW_MOVE(Name)                                                    \
  Name(Name&&) = delete;                                                       \
  Name& operator=(Name&&) = delete;

#define DISALLOW_COPY_AND_MOVE(Name)                                           \
  DISALLOW_COPY(Name)                                                          \
  DISALLOW_MOVE(Name)

template <typename Opt, std::string Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const v8::Local<v8::Object>& object,
               const v8::Local<v8::String>& name) {
  v8::Local<v8::Value> value;
  if (!object->Get(env->context(), name).ToLocal(&value)) return false;
  if (!value->IsUndefined()) {
    Utf8Value utf8(env->isolate(), value);
    options->*member = *utf8;
  }
  return true;
}

template <typename Opt, bool Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const v8::Local<v8::Object>& object,
               const v8::Local<v8::String>& name) {
  v8::Local<v8::Value> value;
  if (!object->Get(env->context(), name).ToLocal(&value)) return false;
  if (!value->IsUndefined()) {
    options->*member = value->BooleanValue(env->isolate());
  }
  return true;
}

template <typename Opt, uint32_t Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const v8::Local<v8::Object>& object,
               const v8::Local<v8::String>& name) {
  v8::Local<v8::Value> value;
  if (!object->Get(env->context(), name).ToLocal(&value)) return false;
  if (!value->IsUndefined()) {
    if (!value->IsUint32()) {
      Utf8Value nameStr(env->isolate(), name);
      THROW_ERR_INVALID_ARG_VALUE(
          env, "The %s option must be an uint32", *nameStr);
      return false;
    }
    v8::Local<v8::Uint32> num;
    if (!value->ToUint32(env->context()).ToLocal(&num)) {
      Utf8Value nameStr(env->isolate(), name);
      THROW_ERR_INVALID_ARG_VALUE(
          env, "The %s option must be an uint32", *nameStr);
      return false;
    }
    options->*member = num->Value();
  }
  return true;
}

template <typename Opt, uint64_t Opt::*member>
bool SetOption(Environment* env,
               Opt* options,
               const v8::Local<v8::Object>& object,
               const v8::Local<v8::String>& name) {
  v8::Local<v8::Value> value;
  if (!object->Get(env->context(), name).ToLocal(&value)) return false;

  if (!value->IsUndefined()) {
    if (!value->IsBigInt() && !value->IsNumber()) {
      Utf8Value nameStr(env->isolate(), name);
      THROW_ERR_INVALID_ARG_VALUE(
          env, "option %s must be a bigint or number", *nameStr);
      return false;
    }
    DCHECK_IMPLIES(!value->IsBigInt(), value->IsNumber());

    uint64_t val = 0;
    if (value->IsBigInt()) {
      bool lossless = true;
      val = value.As<v8::BigInt>()->Uint64Value(&lossless);
      if (!lossless) {
        Utf8Value label(env->isolate(), name);
        THROW_ERR_INVALID_ARG_VALUE(env, "option %s is out of range", *label);
        return false;
      }
    } else {
      double dbl = value.As<v8::Number>()->Value();
      if (dbl < 0) {
        Utf8Value label(env->isolate(), name);
        THROW_ERR_INVALID_ARG_VALUE(env, "option %s is out of range", *label);
        return false;
      }
      val = static_cast<uint64_t>(dbl);
    }
    options->*member = val;
  }
  return true;
}

// Utilities used to update the stats for Endpoint, Session, and Stream
// objects. The stats themselves are maintained in an AliasedStruct within
// each of the relevant classes.

template <typename Stats, uint64_t Stats::*member>
void IncrementStat(Stats* stats, uint64_t amt = 1) {
  stats->*member += amt;
}

template <typename Stats, uint64_t Stats::*member>
void RecordTimestampStat(Stats* stats) {
  stats->*member = uv_hrtime();
}

template <typename Stats, uint64_t Stats::*member>
void SetStat(Stats* stats, uint64_t val) {
  stats->*member = val;
}

template <typename Stats, uint64_t Stats::*member>
uint64_t GetStat(Stats* stats) {
  return stats->*member;
}

#define STAT_INCREMENT(Type, name)                                             \
  IncrementStat<Type, &Type::name>(stats_.Data());
#define STAT_INCREMENT_N(Type, name, amt)                                      \
  IncrementStat<Type, &Type::name>(stats_.Data(), amt);
#define STAT_RECORD_TIMESTAMP(Type, name)                                      \
  RecordTimestampStat<Type, &Type::name>(stats_.Data());
#define STAT_SET(Type, name, val) SetStat<Type, &Type::name>(stats_.Data(), val)
#define STAT_GET(Type, name) GetStat<Type, &Type::name>(stats_.Data())
#define STAT_FIELD(_, name) uint64_t name;
#define STAT_STRUCT(klass, name)                                               \
  struct klass::Stats final {                                                  \
    name##_STATS(STAT_FIELD)                                                   \
  };

#define JS_METHOD(name)                                                        \
  static void name(const v8::FunctionCallbackInfo<v8::Value>& args)

enum class Side : uint8_t {
  CLIENT,
  SERVER,
};

enum class EndpointLabel : uint8_t {
  LOCAL,
  REMOTE,
};

enum class Direction : uint8_t {
  BIDIRECTIONAL,
  UNIDIRECTIONAL,
};

enum class HeadersKind : uint8_t {
  HINTS,
  INITIAL,
  TRAILING,
};

enum class HeadersFlags : uint8_t {
  NONE,
  TERMINAL,
};

enum class StreamPriority : uint8_t {
  DEFAULT = NGHTTP3_DEFAULT_URGENCY,
  LOW = NGHTTP3_URGENCY_LOW,
  HIGH = NGHTTP3_URGENCY_HIGH,
};

enum class StreamPriorityFlags : uint8_t {
  NONE,
  NON_INCREMENTAL,
};

enum class PathValidationResult : uint8_t {
  SUCCESS = NGTCP2_PATH_VALIDATION_RESULT_SUCCESS,
  FAILURE = NGTCP2_PATH_VALIDATION_RESULT_FAILURE,
  ABORTED = NGTCP2_PATH_VALIDATION_RESULT_ABORTED,
};

enum class DatagramStatus : uint8_t {
  ACKNOWLEDGED,
  LOST,
};

#define CC_ALGOS(V)                                                            \
  V(RENO, reno)                                                                \
  V(CUBIC, cubic)                                                              \
  V(BBR, bbr)

#define V(name, _) static constexpr auto CC_ALGO_##name = NGTCP2_CC_ALGO_##name;
CC_ALGOS(V)
#undef V

constexpr uint64_t NGTCP2_APP_NOERROR = 65280;
constexpr size_t kDefaultMaxPacketLength = NGTCP2_MAX_UDP_PAYLOAD_SIZE;
constexpr size_t kMaxSizeT = std::numeric_limits<size_t>::max();
constexpr uint64_t kMaxSafeJsInteger = 9007199254740991;
constexpr auto kSocketAddressInfoTimeout = 60 * NGTCP2_SECONDS;
constexpr size_t kMaxVectorCount = 16;

using error_code = uint64_t;

class DebugIndentScope {
 public:
  inline DebugIndentScope() { ++indent_; }
  DISALLOW_COPY_AND_MOVE(DebugIndentScope)
  inline ~DebugIndentScope() { --indent_; }
  inline std::string Prefix() const {
    std::string res("\n");
    res.append(indent_, '\t');
    return res;
  }
  inline std::string Close() const {
    std::string res("\n");
    res.append(indent_ - 1, '\t');
    res += "}";
    return res;
  }

 private:
  static int indent_;
};

}  // namespace node::quic
