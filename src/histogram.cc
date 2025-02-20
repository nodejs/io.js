#include "histogram.h"  // NOLINT(build/include_inline)
#include "base_object-inl.h"
#include "histogram-inl.h"
#include "memory_tracker-inl.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "util.h"

namespace node {

using v8::BigInt;
using v8::CFunction;
using v8::Context;
using v8::FastApiCallbackOptions;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Map;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Uint32;
using v8::Value;

Histogram::Histogram(const Options& options) {
  hdr_histogram* histogram;
  CHECK_EQ(0, hdr_init(options.lowest,
                       options.highest,
                       options.figures,
                       &histogram));
  histogram_.reset(histogram);
}

void Histogram::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackFieldWithSize("histogram", GetMemorySize());
}

HistogramImpl::HistogramImpl(const Histogram::Options& options)
    : histogram_(new Histogram(options)) {}

HistogramImpl::HistogramImpl(std::shared_ptr<Histogram> histogram)
    : histogram_(std::move(histogram)) {}

CFunction HistogramImpl::fast_reset_(
    CFunction::Make(&HistogramImpl::FastReset));
CFunction HistogramImpl::fast_get_count_(
    CFunction::Make(&HistogramImpl::FastGetCount));
CFunction HistogramImpl::fast_get_min_(
    CFunction::Make(&HistogramImpl::FastGetMin));
CFunction HistogramImpl::fast_get_max_(
    CFunction::Make(&HistogramImpl::FastGetMax));
CFunction HistogramImpl::fast_get_mean_(
    CFunction::Make(&HistogramImpl::FastGetMean));
CFunction HistogramImpl::fast_get_exceeds_(
    CFunction::Make(&HistogramImpl::FastGetExceeds));
CFunction HistogramImpl::fast_get_stddev_(
    CFunction::Make(&HistogramImpl::FastGetStddev));
CFunction HistogramImpl::fast_get_percentile_(
    CFunction::Make(&HistogramImpl::FastGetPercentile));
CFunction HistogramBase::fast_record_(
    CFunction::Make(&HistogramBase::FastRecord));
CFunction HistogramBase::fast_record_delta_(
    CFunction::Make(&HistogramBase::FastRecordDelta));
CFunction IntervalHistogram::fast_start_(
    CFunction::Make(&IntervalHistogram::FastStart));
CFunction IntervalHistogram::fast_stop_(
    CFunction::Make(&IntervalHistogram::FastStop));

void HistogramImpl::AddMethods(Isolate* isolate, Local<FunctionTemplate> tmpl) {
  // TODO(@jasnell): The bigint API variations do not yet support fast
  // variations since v8 will not return a bigint value from a fast method.
  SetProtoMethodNoSideEffect(isolate, tmpl, "countBigInt", GetCountBigInt);
  SetProtoMethodNoSideEffect(isolate, tmpl, "exceedsBigInt", GetExceedsBigInt);
  SetProtoMethodNoSideEffect(isolate, tmpl, "minBigInt", GetMinBigInt);
  SetProtoMethodNoSideEffect(isolate, tmpl, "maxBigInt", GetMaxBigInt);
  SetProtoMethodNoSideEffect(
      isolate, tmpl, "percentileBigInt", GetPercentileBigInt);
  SetProtoMethodNoSideEffect(isolate, tmpl, "percentiles", GetPercentiles);
  SetProtoMethodNoSideEffect(
      isolate, tmpl, "percentilesBigInt", GetPercentilesBigInt);
  auto instance = tmpl->InstanceTemplate();
  SetFastMethodNoSideEffect(
      isolate, instance, "count", GetCount, &fast_get_count_);
  SetFastMethodNoSideEffect(
      isolate, instance, "exceeds", GetExceeds, &fast_get_exceeds_);
  SetFastMethodNoSideEffect(isolate, instance, "min", GetMin, &fast_get_min_);
  SetFastMethodNoSideEffect(isolate, instance, "max", GetMax, &fast_get_max_);
  SetFastMethodNoSideEffect(
      isolate, instance, "mean", GetMean, &fast_get_mean_);
  SetFastMethodNoSideEffect(
      isolate, instance, "stddev", GetStddev, &fast_get_stddev_);
  SetFastMethodNoSideEffect(
      isolate, instance, "percentile", GetPercentile, &fast_get_percentile_);
  SetFastMethod(isolate, instance, "reset", DoReset, &fast_reset_);
}

void HistogramImpl::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  static bool is_registered = false;
  if (is_registered) return;
  registry->Register(GetCount);
  registry->Register(GetCountBigInt);
  registry->Register(GetExceeds);
  registry->Register(GetExceedsBigInt);
  registry->Register(GetMin);
  registry->Register(GetMinBigInt);
  registry->Register(GetMax);
  registry->Register(GetMaxBigInt);
  registry->Register(GetMean);
  registry->Register(GetStddev);
  registry->Register(GetPercentile);
  registry->Register(GetPercentileBigInt);
  registry->Register(GetPercentiles);
  registry->Register(GetPercentilesBigInt);
  registry->Register(DoReset);
  registry->Register(fast_reset_.GetTypeInfo());
  registry->Register(fast_get_count_.GetTypeInfo());
  registry->Register(fast_get_min_.GetTypeInfo());
  registry->Register(fast_get_max_.GetTypeInfo());
  registry->Register(fast_get_mean_.GetTypeInfo());
  registry->Register(fast_get_exceeds_.GetTypeInfo());
  registry->Register(fast_get_stddev_.GetTypeInfo());
  registry->Register(fast_get_percentile_.GetTypeInfo());
  registry->Register(FastReset);
  registry->Register(FastGetCount);
  registry->Register(FastGetMin);
  registry->Register(FastGetMax);
  registry->Register(FastGetMean);
  registry->Register(FastGetExceeds);
  registry->Register(FastGetStddev);
  registry->Register(FastGetPercentile);
  is_registered = true;
}

HistogramBase::HistogramBase(
    Environment* env,
    Local<Object> wrap,
    const Histogram::Options& options)
    : BaseObject(env, wrap),
      HistogramImpl(options) {
  MakeWeak();
  wrap->SetAlignedPointerInInternalField(
      HistogramImpl::InternalFields::kImplField,
      static_cast<HistogramImpl*>(this));
}

HistogramBase::HistogramBase(
    Environment* env,
    Local<Object> wrap,
    std::shared_ptr<Histogram> histogram)
    : BaseObject(env, wrap),
      HistogramImpl(std::move(histogram)) {
  MakeWeak();
  wrap->SetAlignedPointerInInternalField(
      HistogramImpl::InternalFields::kImplField,
      static_cast<HistogramImpl*>(this));
}

void HistogramBase::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("histogram", histogram());
}

void HistogramBase::RecordDelta(const FunctionCallbackInfo<Value>& args) {
  HistogramBase* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, args.This());
  (*histogram)->RecordDelta();
}

void HistogramBase::FastRecordDelta(Local<Value> unused,
                                    Local<Value> receiver) {
  HistogramBase* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, receiver);
  (*histogram)->RecordDelta();
}

void HistogramBase::Record(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  CHECK_IMPLIES(!args[0]->IsNumber(), args[0]->IsBigInt());
  bool lossless = true;
  int64_t value = args[0]->IsBigInt()
      ? args[0].As<BigInt>()->Int64Value(&lossless)
      : static_cast<int64_t>(args[0].As<Number>()->Value());
  if (!lossless || value < 1)
    return THROW_ERR_OUT_OF_RANGE(env, "value is out of range");
  HistogramBase* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, args.This());
  (*histogram)->Record(value);
}

void HistogramBase::FastRecord(Local<Value> unused,
                               Local<Value> receiver,
                               const int64_t value,
                               FastApiCallbackOptions& options) {
  if (value < 1) {
    HandleScope scope(options.isolate);
    THROW_ERR_OUT_OF_RANGE(options.isolate, "value is out of range");
    return;
  }
  HistogramBase* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, receiver);
  (*histogram)->Record(value);
}

void HistogramBase::Add(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramBase* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, args.This());

  CHECK(GetConstructorTemplate(env->isolate_data())->HasInstance(args[0]));
  HistogramBase* other;
  ASSIGN_OR_RETURN_UNWRAP(&other, args[0]);

  double count = (*histogram)->Add(*(other->histogram()));
  args.GetReturnValue().Set(count);
}

BaseObjectPtr<HistogramBase> HistogramBase::Create(
    Environment* env,
    const Histogram::Options& options) {
  Local<Object> obj;
  if (!GetConstructorTemplate(env->isolate_data())
           ->InstanceTemplate()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return nullptr;
  }

  return MakeBaseObject<HistogramBase>(env, obj, options);
}

BaseObjectPtr<HistogramBase> HistogramBase::Create(
    Environment* env,
    std::shared_ptr<Histogram> histogram) {
  Local<Object> obj;
  if (!GetConstructorTemplate(env->isolate_data())
           ->InstanceTemplate()
           ->NewInstance(env->context())
           .ToLocal(&obj)) {
    return nullptr;
  }
  return MakeBaseObject<HistogramBase>(env, obj, std::move(histogram));
}

void HistogramBase::New(const FunctionCallbackInfo<Value>& args) {
  CHECK(args.IsConstructCall());
  Environment* env = Environment::GetCurrent(args);

  CHECK_IMPLIES(!args[0]->IsNumber(), args[0]->IsBigInt());
  CHECK_IMPLIES(!args[1]->IsNumber(), args[1]->IsBigInt());
  CHECK(args[2]->IsUint32());

  int64_t lowest = 1;
  int64_t highest = std::numeric_limits<int64_t>::max();

  bool lossless_ignored;

  if (args[0]->IsNumber()) {
    lowest = args[0].As<Integer>()->Value();
  } else if (args[0]->IsBigInt()) {
    lowest = args[0].As<BigInt>()->Int64Value(&lossless_ignored);
  }

  if (args[1]->IsNumber()) {
    highest = args[1].As<Integer>()->Value();
  } else if (args[1]->IsBigInt()) {
    highest = args[1].As<BigInt>()->Int64Value(&lossless_ignored);
  }

  int32_t figures = args[2].As<Uint32>()->Value();
  new HistogramBase(env, args.This(), Histogram::Options {
    lowest, highest, figures
  });
}

Local<FunctionTemplate> HistogramBase::GetConstructorTemplate(
    IsolateData* isolate_data) {
  Local<FunctionTemplate> tmpl = isolate_data->histogram_ctor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = isolate_data->isolate();
    tmpl = NewFunctionTemplate(isolate, New);
    Local<String> classname = FIXED_ONE_BYTE_STRING(isolate, "Histogram");
    tmpl->SetClassName(classname);
    auto instance = tmpl->InstanceTemplate();
    instance->SetInternalFieldCount(HistogramImpl::kInternalFieldCount);
    SetFastMethod(isolate, instance, "record", Record, &fast_record_);
    SetFastMethod(
        isolate, instance, "recordDelta", RecordDelta, &fast_record_delta_);
    SetProtoMethod(isolate, tmpl, "add", Add);
    HistogramImpl::AddMethods(isolate, tmpl);
    isolate_data->set_histogram_ctor_template(tmpl);
  }
  return tmpl;
}

void HistogramBase::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(New);
  registry->Register(Add);
  registry->Register(Record);
  registry->Register(RecordDelta);
  registry->Register(fast_record_.GetTypeInfo());
  registry->Register(fast_record_delta_.GetTypeInfo());
  registry->Register(FastRecord);
  registry->Register(FastRecordDelta);
  HistogramImpl::RegisterExternalReferences(registry);
}

void HistogramBase::Initialize(IsolateData* isolate_data,
                               Local<ObjectTemplate> target) {
  SetConstructorFunction(isolate_data->isolate(),
                         target,
                         "Histogram",
                         GetConstructorTemplate(isolate_data),
                         SetConstructorFunctionFlag::NONE);
}

BaseObjectPtr<BaseObject> HistogramBase::HistogramTransferData::Deserialize(
    Environment* env,
    Local<Context> context,
    std::unique_ptr<worker::TransferData> self) {
  return Create(env, std::move(histogram_));
}

std::unique_ptr<worker::TransferData> HistogramBase::CloneForMessaging() const {
  return std::make_unique<HistogramTransferData>(this);
}

void HistogramBase::HistogramTransferData::MemoryInfo(
    MemoryTracker* tracker) const {
  tracker->TrackField("histogram", histogram_);
}

Local<FunctionTemplate> IntervalHistogram::GetConstructorTemplate(
    Environment* env) {
  Local<FunctionTemplate> tmpl = env->intervalhistogram_constructor_template();
  if (tmpl.IsEmpty()) {
    Isolate* isolate = env->isolate();
    tmpl = NewFunctionTemplate(isolate, nullptr);
    tmpl->Inherit(HandleWrap::GetConstructorTemplate(env));
    tmpl->SetClassName(FIXED_ONE_BYTE_STRING(isolate, "Histogram"));
    auto instance = tmpl->InstanceTemplate();
    instance->SetInternalFieldCount(HistogramImpl::kInternalFieldCount);
    HistogramImpl::AddMethods(isolate, tmpl);
    SetFastMethod(isolate, instance, "start", Start, &fast_start_);
    SetFastMethod(isolate, instance, "stop", Stop, &fast_stop_);
    env->set_intervalhistogram_constructor_template(tmpl);
  }
  return tmpl;
}

void IntervalHistogram::RegisterExternalReferences(
    ExternalReferenceRegistry* registry) {
  registry->Register(Start);
  registry->Register(Stop);
  registry->Register(fast_start_.GetTypeInfo());
  registry->Register(fast_stop_.GetTypeInfo());
  registry->Register(FastStart);
  registry->Register(FastStop);
  HistogramImpl::RegisterExternalReferences(registry);
}

IntervalHistogram::IntervalHistogram(
    Environment* env,
    Local<Object> wrap,
    AsyncWrap::ProviderType type,
    int32_t interval,
    std::function<void(Histogram&)> on_interval,
    const Histogram::Options& options)
    : HandleWrap(
          env,
          wrap,
          reinterpret_cast<uv_handle_t*>(&timer_),
          type),
      HistogramImpl(options),
      interval_(interval),
      on_interval_(std::move(on_interval)) {
  MakeWeak();
  wrap->SetAlignedPointerInInternalField(
      HistogramImpl::InternalFields::kImplField,
      static_cast<HistogramImpl*>(this));
  uv_timer_init(env->event_loop(), &timer_);
}

BaseObjectPtr<IntervalHistogram> IntervalHistogram::Create(
    Environment* env,
    int32_t interval,
    std::function<void(Histogram&)> on_interval,
    const Histogram::Options& options) {
  Local<Object> obj;
  if (!GetConstructorTemplate(env)
          ->InstanceTemplate()
          ->NewInstance(env->context()).ToLocal(&obj)) {
    return nullptr;
  }

  return MakeBaseObject<IntervalHistogram>(
      env,
      obj,
      AsyncWrap::PROVIDER_ELDHISTOGRAM,
      interval,
      std::move(on_interval),
      options);
}

void IntervalHistogram::TimerCB(uv_timer_t* handle) {
  IntervalHistogram* histogram =
      ContainerOf(&IntervalHistogram::timer_, handle);

  Histogram* h = histogram->histogram().get();

  histogram->on_interval_(*h);
}

void IntervalHistogram::MemoryInfo(MemoryTracker* tracker) const {
  tracker->TrackField("histogram", histogram());
}

void IntervalHistogram::OnStart(StartFlags flags) {
  if (enabled_ || IsHandleClosing()) return;
  enabled_ = true;
  if (flags == StartFlags::RESET)
    histogram()->Reset();
  uv_timer_start(&timer_, TimerCB, interval_, interval_);
  uv_unref(reinterpret_cast<uv_handle_t*>(&timer_));
}

void IntervalHistogram::OnStop() {
  if (!enabled_ || IsHandleClosing()) return;
  enabled_ = false;
  uv_timer_stop(&timer_);
}

void IntervalHistogram::Start(const FunctionCallbackInfo<Value>& args) {
  IntervalHistogram* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, args.This());
  histogram->OnStart(args[0]->IsTrue() ? StartFlags::RESET : StartFlags::NONE);
}

void IntervalHistogram::FastStart(Local<Value> unused,
                                  Local<Value> receiver,
                                  bool reset) {
  IntervalHistogram* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, receiver);
  histogram->OnStart(reset ? StartFlags::RESET : StartFlags::NONE);
}

void IntervalHistogram::Stop(const FunctionCallbackInfo<Value>& args) {
  IntervalHistogram* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, args.This());
  histogram->OnStop();
}

void IntervalHistogram::FastStop(Local<Value> unused, Local<Value> receiver) {
  IntervalHistogram* histogram;
  ASSIGN_OR_RETURN_UNWRAP(&histogram, receiver);
  histogram->OnStop();
}

void HistogramImpl::GetCount(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  double value = static_cast<double>((*histogram)->Count());
  args.GetReturnValue().Set(value);
}

void HistogramImpl::GetCountBigInt(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  args.GetReturnValue().Set(
      BigInt::NewFromUnsigned(env->isolate(), (*histogram)->Count()));
}

void HistogramImpl::GetMin(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  double value = static_cast<double>((*histogram)->Min());
  args.GetReturnValue().Set(value);
}

void HistogramImpl::GetMinBigInt(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  args.GetReturnValue().Set(BigInt::New(env->isolate(), (*histogram)->Min()));
}

void HistogramImpl::GetMax(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  double value = static_cast<double>((*histogram)->Max());
  args.GetReturnValue().Set(value);
}

void HistogramImpl::GetMaxBigInt(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  args.GetReturnValue().Set(BigInt::New(env->isolate(), (*histogram)->Max()));
}

void HistogramImpl::GetMean(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  args.GetReturnValue().Set((*histogram)->Mean());
}

void HistogramImpl::GetExceeds(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  double value = static_cast<double>((*histogram)->Exceeds());
  args.GetReturnValue().Set(value);
}

void HistogramImpl::GetExceedsBigInt(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  args.GetReturnValue().Set(
      BigInt::New(env->isolate(), (*histogram)->Exceeds()));
}

void HistogramImpl::GetStddev(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  args.GetReturnValue().Set((*histogram)->Stddev());
}

void HistogramImpl::GetPercentile(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  CHECK(args[0]->IsNumber());
  double percentile = args[0].As<Number>()->Value();
  double value = static_cast<double>((*histogram)->Percentile(percentile));
  args.GetReturnValue().Set(value);
}

void HistogramImpl::GetPercentileBigInt(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  CHECK(args[0]->IsNumber());
  double percentile = args[0].As<Number>()->Value();
  int64_t value = (*histogram)->Percentile(percentile);
  args.GetReturnValue().Set(BigInt::New(env->isolate(), value));
}

void HistogramImpl::GetPercentiles(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  CHECK(args[0]->IsMap());
  Local<Map> map = args[0].As<Map>();
  (*histogram)->Percentiles([map, env](double key, int64_t value) {
    USE(map->Set(
          env->context(),
          Number::New(env->isolate(), key),
          Number::New(env->isolate(), static_cast<double>(value))));
  });
}

void HistogramImpl::GetPercentilesBigInt(
    const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  CHECK(args[0]->IsMap());
  Local<Map> map = args[0].As<Map>();
  (*histogram)->Percentiles([map, env](double key, int64_t value) {
    USE(map->Set(
          env->context(),
          Number::New(env->isolate(), key),
          BigInt::New(env->isolate(), value)));
  });
}

void HistogramImpl::DoReset(const FunctionCallbackInfo<Value>& args) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(args.This());
  (*histogram)->Reset();
}

void HistogramImpl::FastReset(Local<Value> unused, Local<Value> receiver) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  (*histogram)->Reset();
}

double HistogramImpl::FastGetCount(Local<Value> unused, Local<Value> receiver) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  return static_cast<double>((*histogram)->Count());
}

double HistogramImpl::FastGetMin(Local<Value> unused, Local<Value> receiver) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  return static_cast<double>((*histogram)->Min());
}

double HistogramImpl::FastGetMax(Local<Value> unused, Local<Value> receiver) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  return static_cast<double>((*histogram)->Max());
}

double HistogramImpl::FastGetMean(Local<Value> unused, Local<Value> receiver) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  return (*histogram)->Mean();
}

double HistogramImpl::FastGetExceeds(Local<Value> unused,
                                     Local<Value> receiver) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  return static_cast<double>((*histogram)->Exceeds());
}

double HistogramImpl::FastGetStddev(Local<Value> unused,
                                    Local<Value> receiver) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  return (*histogram)->Stddev();
}

double HistogramImpl::FastGetPercentile(Local<Value> unused,
                                        Local<Value> receiver,
                                        const double percentile) {
  HistogramImpl* histogram = HistogramImpl::FromJSObject(receiver);
  return static_cast<double>((*histogram)->Percentile(percentile));
}

HistogramImpl* HistogramImpl::FromJSObject(Local<Value> value) {
  auto obj = value.As<Object>();
  DCHECK_GE(obj->InternalFieldCount(), HistogramImpl::kInternalFieldCount);
  return static_cast<HistogramImpl*>(
      obj->GetAlignedPointerFromInternalField(HistogramImpl::kImplField));
}

std::unique_ptr<worker::TransferData>
IntervalHistogram::CloneForMessaging() const {
  return std::make_unique<HistogramBase::HistogramTransferData>(histogram());
}

}  // namespace node
