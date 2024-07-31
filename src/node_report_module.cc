#include "env.h"
#include "node_errors.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "node_options.h"
#include "node_report.h"
#include "util-inl.h"

#include "handle_wrap.h"
#include "node_buffer.h"
#include "stream_base-inl.h"
#include "stream_wrap.h"

#include <v8.h>
#include <atomic>
#include <sstream>

namespace node {
namespace report {
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

void WriteReport(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  Isolate* isolate = env->isolate();
  HandleScope scope(isolate);
  std::string filename;
  Local<Value> error;

  CHECK_EQ(info.Length(), 4);
  String::Utf8Value message(isolate, info[0].As<String>());
  String::Utf8Value trigger(isolate, info[1].As<String>());

  if (info[2]->IsString())
    filename = *String::Utf8Value(isolate, info[2]);
  if (!info[3].IsEmpty())
    error = info[3];
  else
    error = Local<Value>();

  filename = TriggerNodeReport(env, *message, *trigger, filename, error);
  // Return value is the report filename
  info.GetReturnValue().Set(
      String::NewFromUtf8(isolate, filename.c_str()).ToLocalChecked());
}

// External JavaScript API for returning a report
void GetReport(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  Isolate* isolate = env->isolate();
  HandleScope scope(isolate);
  Local<Object> error;
  std::ostringstream out;

  CHECK_EQ(info.Length(), 1);
  if (!info[0].IsEmpty() && info[0]->IsObject())
    error = info[0].As<Object>();
  else
    error = Local<Object>();

  GetNodeReport(env, "JavaScript API", __func__, error, out);

  // Return value is the contents of a report as a string.
  info.GetReturnValue().Set(
      String::NewFromUtf8(isolate, out.str().c_str()).ToLocalChecked());
}

static void GetCompact(const FunctionCallbackInfo<Value>& info) {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  info.GetReturnValue().Set(per_process::cli_options->report_compact);
}

static void SetCompact(const FunctionCallbackInfo<Value>& info) {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  Environment* env = Environment::GetCurrent(info);
  Isolate* isolate = env->isolate();
  bool compact = info[0]->ToBoolean(isolate)->Value();
  per_process::cli_options->report_compact = compact;
}

static void GetExcludeNetwork(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  info.GetReturnValue().Set(env->options()->report_exclude_network);
}

static void SetExcludeNetwork(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(info[0]->IsBoolean());
  env->options()->report_exclude_network = info[0]->IsTrue();
}

static void GetDirectory(const FunctionCallbackInfo<Value>& info) {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  Environment* env = Environment::GetCurrent(info);
  std::string directory = per_process::cli_options->report_directory;
  auto result = String::NewFromUtf8(env->isolate(), directory.c_str());
  info.GetReturnValue().Set(result.ToLocalChecked());
}

static void SetDirectory(const FunctionCallbackInfo<Value>& info) {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  Environment* env = Environment::GetCurrent(info);
  CHECK(info[0]->IsString());
  Utf8Value dir(env->isolate(), info[0].As<String>());
  per_process::cli_options->report_directory = *dir;
}

static void GetFilename(const FunctionCallbackInfo<Value>& info) {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  Environment* env = Environment::GetCurrent(info);
  std::string filename = per_process::cli_options->report_filename;
  auto result = String::NewFromUtf8(env->isolate(), filename.c_str());
  info.GetReturnValue().Set(result.ToLocalChecked());
}

static void SetFilename(const FunctionCallbackInfo<Value>& info) {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  Environment* env = Environment::GetCurrent(info);
  CHECK(info[0]->IsString());
  Utf8Value name(env->isolate(), info[0].As<String>());
  per_process::cli_options->report_filename = *name;
}

static void GetSignal(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  std::string signal = env->isolate_data()->options()->report_signal;
  auto result = String::NewFromUtf8(env->isolate(), signal.c_str());
  info.GetReturnValue().Set(result.ToLocalChecked());
}

static void SetSignal(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(info[0]->IsString());
  Utf8Value signal(env->isolate(), info[0].As<String>());
  env->isolate_data()->options()->report_signal = *signal;
}

static void ShouldReportOnFatalError(const FunctionCallbackInfo<Value>& info) {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  info.GetReturnValue().Set(per_process::cli_options->report_on_fatalerror);
}

static void SetReportOnFatalError(const FunctionCallbackInfo<Value>& info) {
  CHECK(info[0]->IsBoolean());
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  per_process::cli_options->report_on_fatalerror = info[0]->IsTrue();
}

static void ShouldReportOnSignal(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  info.GetReturnValue().Set(env->isolate_data()->options()->report_on_signal);
}

static void SetReportOnSignal(const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(info[0]->IsBoolean());
  env->isolate_data()->options()->report_on_signal = info[0]->IsTrue();
}

static void ShouldReportOnUncaughtException(
    const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  info.GetReturnValue().Set(
      env->isolate_data()->options()->report_uncaught_exception);
}

static void SetReportOnUncaughtException(
    const FunctionCallbackInfo<Value>& info) {
  Environment* env = Environment::GetCurrent(info);
  CHECK(info[0]->IsBoolean());
  env->isolate_data()->options()->report_uncaught_exception = info[0]->IsTrue();
}

static void Initialize(Local<Object> exports,
                       Local<Value> unused,
                       Local<Context> context,
                       void* priv) {
  SetMethod(context, exports, "writeReport", WriteReport);
  SetMethod(context, exports, "getReport", GetReport);
  SetMethod(context, exports, "getCompact", GetCompact);
  SetMethod(context, exports, "setCompact", SetCompact);
  SetMethod(context, exports, "getExcludeNetwork", GetExcludeNetwork);
  SetMethod(context, exports, "setExcludeNetwork", SetExcludeNetwork);
  SetMethod(context, exports, "getDirectory", GetDirectory);
  SetMethod(context, exports, "setDirectory", SetDirectory);
  SetMethod(context, exports, "getFilename", GetFilename);
  SetMethod(context, exports, "setFilename", SetFilename);
  SetMethod(context, exports, "getSignal", GetSignal);
  SetMethod(context, exports, "setSignal", SetSignal);
  SetMethod(
      context, exports, "shouldReportOnFatalError", ShouldReportOnFatalError);
  SetMethod(context, exports, "setReportOnFatalError", SetReportOnFatalError);
  SetMethod(context, exports, "shouldReportOnSignal", ShouldReportOnSignal);
  SetMethod(context, exports, "setReportOnSignal", SetReportOnSignal);
  SetMethod(context,
            exports,
            "shouldReportOnUncaughtException",
            ShouldReportOnUncaughtException);
  SetMethod(context,
            exports,
            "setReportOnUncaughtException",
            SetReportOnUncaughtException);
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(WriteReport);
  registry->Register(GetReport);
  registry->Register(GetCompact);
  registry->Register(SetCompact);
  registry->Register(GetExcludeNetwork);
  registry->Register(SetExcludeNetwork);
  registry->Register(GetDirectory);
  registry->Register(SetDirectory);
  registry->Register(GetFilename);
  registry->Register(SetFilename);
  registry->Register(GetSignal);
  registry->Register(SetSignal);
  registry->Register(ShouldReportOnFatalError);
  registry->Register(SetReportOnFatalError);
  registry->Register(ShouldReportOnSignal);
  registry->Register(SetReportOnSignal);
  registry->Register(ShouldReportOnUncaughtException);
  registry->Register(SetReportOnUncaughtException);
}

}  // namespace report
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(report, node::report::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(report,
                                node::report::RegisterExternalReferences)
