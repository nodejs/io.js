#ifndef SRC_HISTOGRAM_H_
#define SRC_HISTOGRAM_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <hdr/hdr_histogram.h>
#include "base_object.h"
#include "memory_tracker.h"
#include "node_messaging.h"
#include "util.h"
#include "uv.h"
#include "v8.h"

#include <functional>
#include <limits>
#include <map>
#include <string>

namespace node {

class ExternalReferenceRegistry;

constexpr int kDefaultHistogramFigures = 3;

class Histogram : public MemoryRetainer {
 public:
  struct Options {
    int64_t lowest = 1;
    int64_t highest = std::numeric_limits<int64_t>::max();
    int figures = kDefaultHistogramFigures;
  };

  explicit Histogram(const Options& options);
  virtual ~Histogram() = default;

  inline bool Record(int64_t value);
  inline void Reset();
  inline int64_t Min() const;
  inline int64_t Max() const;
  inline double Mean() const;
  inline double Stddev() const;
  inline int64_t Percentile(double percentile) const;
  inline size_t Exceeds() const { return exceeds_; }
  inline size_t Count() const;

  inline uint64_t RecordDelta();

  inline double Add(const Histogram& other);

  // Iterator is a function type that takes two doubles as argument, one for
  // percentile and one for the value at that percentile.
  template <typename Iterator>
  inline void Percentiles(Iterator&& fn);

  inline size_t GetMemorySize() const;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(Histogram)
  SET_SELF_SIZE(Histogram)

 private:
  using HistogramPointer = DeleteFnPtr<hdr_histogram, hdr_close>;
  HistogramPointer histogram_;
  uint64_t prev_ = 0;
  size_t exceeds_ = 0;
  size_t count_ = 0;
  Mutex mutex_;
};

class HistogramImpl {
 public:
  enum InternalFields {
    kSlot = BaseObject::kSlot,
    kImplField = BaseObject::kInternalFieldCount,
    kInternalFieldCount
  };

  explicit HistogramImpl(
      const Histogram::Options& options = Histogram::Options {});
  explicit HistogramImpl(std::shared_ptr<Histogram> histogram);

  Histogram* operator->() { return histogram_.get(); }

  const std::shared_ptr<Histogram>& histogram() const { return histogram_; }

  static void DoReset(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetCountBigInt(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetMinBigInt(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetMaxBigInt(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetExceedsBigInt(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetCount(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetMin(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetMax(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetMean(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetExceeds(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetStddev(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPercentile(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPercentileBigInt(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPercentiles(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void GetPercentilesBigInt(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  static void FastReset(v8::Local<v8::Value> receiver);
  static double FastGetCount(v8::Local<v8::Value> receiver);
  static double FastGetMin(v8::Local<v8::Value> receiver);
  static double FastGetMax(v8::Local<v8::Value> receiver);
  static double FastGetMean(v8::Local<v8::Value> receiver);
  static double FastGetExceeds(v8::Local<v8::Value> receiver);
  static double FastGetStddev(v8::Local<v8::Value> receiver);
  static double FastGetPercentile(v8::Local<v8::Value> receiver,
                                  const double percentile);

  static void AddMethods(v8::Isolate* isolate,
                         v8::Local<v8::FunctionTemplate> tmpl);

  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static HistogramImpl* FromJSObject(v8::Local<v8::Value> value);

 private:
  std::shared_ptr<Histogram> histogram_;

  static v8::CFunction fast_reset_;
  static v8::CFunction fast_get_count_;
  static v8::CFunction fast_get_min_;
  static v8::CFunction fast_get_max_;
  static v8::CFunction fast_get_mean_;
  static v8::CFunction fast_get_exceeds_;
  static v8::CFunction fast_get_stddev_;
  static v8::CFunction fast_get_percentile_;
};

class HistogramBase final : public BaseObject, public HistogramImpl {
 public:
  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      IsolateData* isolate_data);
  static void Initialize(IsolateData* isolate_data,
                         v8::Local<v8::ObjectTemplate> target);
  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static BaseObjectPtr<HistogramBase> Create(
      Environment* env,
      const Histogram::Options& options = Histogram::Options {});

  static BaseObjectPtr<HistogramBase> Create(
      Environment* env,
      std::shared_ptr<Histogram> histogram);

  static void New(const v8::FunctionCallbackInfo<v8::Value>& args);

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(HistogramBase)
  SET_SELF_SIZE(HistogramBase)

  static void Record(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void RecordDelta(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Add(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void FastRecord(
      v8::Local<v8::Value> receiver,
      const int64_t value,
      v8::FastApiCallbackOptions& options);  // NOLINT(runtime/references)
  static void FastRecordDelta(v8::Local<v8::Value> receiver);

  HistogramBase(
      Environment* env,
      v8::Local<v8::Object> wrap,
      const Histogram::Options& options = Histogram::Options {});

  HistogramBase(
      Environment* env,
      v8::Local<v8::Object> wrap,
      std::shared_ptr<Histogram> histogram);

  BaseObject::TransferMode GetTransferMode() const override {
    return TransferMode::kCloneable;
  }
  std::unique_ptr<worker::TransferData> CloneForMessaging() const override;

  class HistogramTransferData : public worker::TransferData {
   public:
    explicit HistogramTransferData(const HistogramBase* histogram)
        : histogram_(histogram->histogram()) {}

    explicit HistogramTransferData(std::shared_ptr<Histogram> histogram)
        : histogram_(std::move(histogram)) {}

    BaseObjectPtr<BaseObject> Deserialize(
        Environment* env,
        v8::Local<v8::Context> context,
        std::unique_ptr<worker::TransferData> self) override;

    void MemoryInfo(MemoryTracker* tracker) const override;
    SET_MEMORY_INFO_NAME(HistogramTransferData)
    SET_SELF_SIZE(HistogramTransferData)

   private:
    std::shared_ptr<Histogram> histogram_;
  };

 private:
  static v8::CFunction fast_record_;
  static v8::CFunction fast_record_delta_;
};

class IntervalHistogram final : public HandleWrap, public HistogramImpl {
 public:
  enum class StartFlags {
    NONE,
    RESET
  };

  static void RegisterExternalReferences(ExternalReferenceRegistry* registry);

  static v8::Local<v8::FunctionTemplate> GetConstructorTemplate(
      Environment* env);

  static BaseObjectPtr<IntervalHistogram> Create(
      Environment* env,
      int32_t interval,
      std::function<void(Histogram&)> on_interval,
      const Histogram::Options& options);

  IntervalHistogram(
      Environment* env,
      v8::Local<v8::Object> wrap,
      AsyncWrap::ProviderType type,
      int32_t interval,
      std::function<void(Histogram&)> on_interval,
      const Histogram::Options& options = Histogram::Options {});

  static void Start(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Stop(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void FastStart(v8::Local<v8::Value> receiver, bool reset);
  static void FastStop(v8::Local<v8::Value> receiver);

  BaseObject::TransferMode GetTransferMode() const override {
    return TransferMode::kCloneable;
  }
  std::unique_ptr<worker::TransferData> CloneForMessaging() const override;

  void MemoryInfo(MemoryTracker* tracker) const override;
  SET_MEMORY_INFO_NAME(IntervalHistogram)
  SET_SELF_SIZE(IntervalHistogram)

 private:
  static void TimerCB(uv_timer_t* handle);
  void OnStart(StartFlags flags = StartFlags::RESET);
  void OnStop();

  bool enabled_ = false;
  int32_t interval_ = 0;
  std::function<void(Histogram&)> on_interval_;
  uv_timer_t timer_;

  static v8::CFunction fast_start_;
  static v8::CFunction fast_stop_;
};

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_HISTOGRAM_H_
