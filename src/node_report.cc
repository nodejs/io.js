#include "node_report.h"
#include "debug_utils-inl.h"
#include "diagnosticfilename-inl.h"
#include "env-inl.h"
#include "json_utils.h"
#include "node_internals.h"
#include "node_metadata.h"
#include "node_mutex.h"
#include "node_worker.h"
#include "permission/permission.h"
#include "util.h"

#ifdef _WIN32
#include <Windows.h>
#else  // !_WIN32
#include <cxxabi.h>
#include <sys/resource.h>
#include <dlfcn.h>
#endif

#include <cstring>
#include <ctime>
#include <cwctype>
#include <fstream>

constexpr int NODE_REPORT_VERSION = 5;
constexpr int NANOS_PER_SEC = 1000 * 1000 * 1000;
constexpr double SEC_PER_MICROS = 1e-6;
constexpr int MAX_FRAME_COUNT = node::kMaxFrameCountForLogging;

namespace node {
using node::worker::Worker;
using v8::Array;
using v8::Context;
using v8::HandleScope;
using v8::HeapSpaceStatistics;
using v8::HeapStatistics;
using v8::Isolate;
using v8::Just;
using v8::Local;
using v8::Maybe;
using v8::MaybeLocal;
using v8::Nothing;
using v8::Object;
using v8::RegisterState;
using v8::SampleInfo;
using v8::StackFrame;
using v8::StackTrace;
using v8::String;
using v8::TryCatch;
using v8::V8;
using v8::Value;

namespace report {
// Internal/static function declarations
static void WriteNodeReport(Isolate* isolate,
                            Environment* env,
                            const char* message,
                            const char* trigger,
                            const std::string& filename,
                            std::ostream& out,
                            Local<Value> error,
                            bool compact,
                            bool exclude_network = false,
                            bool exclude_env = false);
static void PrintVersionInformation(JSONWriter* writer,
                                    bool exclude_network = false);
static void PrintJavaScriptErrorStack(JSONWriter* writer,
                                      Isolate* isolate,
                                      Local<Value> error,
                                      const char* trigger);
static void PrintEmptyJavaScriptStack(JSONWriter* writer);
static void PrintJavaScriptStack(JSONWriter* writer,
                                 Isolate* isolate,
                                 const char* trigger);
static void PrintJavaScriptErrorProperties(JSONWriter* writer,
                                           Isolate* isolate,
                                           Local<Value> error);
static void PrintNativeStack(JSONWriter* writer);
static void PrintResourceUsage(JSONWriter* writer);
static void PrintGCStatistics(JSONWriter* writer, Isolate* isolate);
static void PrintEnvironmentVariables(JSONWriter* writer);
static void PrintSystemInformation(JSONWriter* writer);
static void PrintLoadedLibraries(JSONWriter* writer);
static void PrintComponentVersions(JSONWriter* writer);
static void PrintRelease(JSONWriter* writer);
static void PrintCpuInfo(JSONWriter* writer);
static void PrintNetworkInterfaceInfo(JSONWriter* writer);

// Internal function to coordinate and write the various
// sections of the report to the supplied stream
static void WriteNodeReport(Isolate* isolate,
                            Environment* env,
                            const char* message,
                            const char* trigger,
                            const std::string& filename,
                            std::ostream& out,
                            Local<Value> error,
                            bool compact,
                            bool exclude_network,
                            bool exclude_env) {
  // Obtain the current time and the pid.
  TIME_TYPE tm_struct;
  DiagnosticFilename::LocalTime(&tm_struct);
  uv_pid_t pid = uv_os_getpid();

  // Save formatting for output stream.
  std::ios old_state(nullptr);
  old_state.copyfmt(out);

  // File stream opened OK, now start printing the report content:
  // the title and header information (event, filename, timestamp and pid)

  JSONWriter writer(out, compact);
  writer.json_start();
  writer.json_objectstart("header");
  writer.json_keyvalue("reportVersion", NODE_REPORT_VERSION);
  writer.json_keyvalue("event", message);
  writer.json_keyvalue("trigger", trigger);
  if (!filename.empty())
    writer.json_keyvalue("filename", filename);
  else
    writer.json_keyvalue("filename", JSONWriter::Null{});

  // Report dump event and module load date/time stamps
  char timebuf[64];
#ifdef _WIN32
  snprintf(timebuf,
           sizeof(timebuf),
           "%4d-%02d-%02dT%02d:%02d:%02dZ",
           tm_struct.wYear,
           tm_struct.wMonth,
           tm_struct.wDay,
           tm_struct.wHour,
           tm_struct.wMinute,
           tm_struct.wSecond);
  writer.json_keyvalue("dumpEventTime", timebuf);
#else  // UNIX, macOS
  snprintf(timebuf,
           sizeof(timebuf),
           "%4d-%02d-%02dT%02d:%02d:%02dZ",
           tm_struct.tm_year + 1900,
           tm_struct.tm_mon + 1,
           tm_struct.tm_mday,
           tm_struct.tm_hour,
           tm_struct.tm_min,
           tm_struct.tm_sec);
  writer.json_keyvalue("dumpEventTime", timebuf);
#endif

  uv_timeval64_t ts;
  if (uv_gettimeofday(&ts) == 0) {
    writer.json_keyvalue("dumpEventTimeStamp",
                         std::to_string(ts.tv_sec * 1000 + ts.tv_usec / 1000));
  }

  // Report native process ID
  writer.json_keyvalue("processId", pid);
  if (env != nullptr)
    writer.json_keyvalue("threadId", env->thread_id());
  else
    writer.json_keyvalue("threadId", JSONWriter::Null{});

  {
    // Report the process cwd.
    char buf[PATH_MAX_BYTES];
    size_t cwd_size = sizeof(buf);
    if (uv_cwd(buf, &cwd_size) == 0)
      writer.json_keyvalue("cwd", buf);
  }

  // Report out the command line.
  if (!per_process::cli_options->cmdline.empty()) {
    writer.json_arraystart("commandLine");
    for (const std::string& arg : per_process::cli_options->cmdline) {
      writer.json_element(arg);
    }
    writer.json_arrayend();
  }

  // Report Node.js and OS version information
  PrintVersionInformation(&writer, exclude_network);
  writer.json_objectend();

  if (isolate != nullptr) {
    writer.json_objectstart("javascriptStack");
    // Report summary JavaScript error stack backtrace
    PrintJavaScriptErrorStack(&writer, isolate, error, trigger);

    writer.json_objectend();  // the end of 'javascriptStack'

    // Report V8 Heap and Garbage Collector information
    PrintGCStatistics(&writer, isolate);
  } else {
    writer.json_objectstart("javascriptStack");
    PrintEmptyJavaScriptStack(&writer);
    writer.json_objectend();  // the end of 'javascriptStack'
  }

  // Report native stack backtrace
  PrintNativeStack(&writer);

  // Report OS and current thread resource usage
  PrintResourceUsage(&writer);

  writer.json_arraystart("libuv");
  if (env != nullptr) {
    uv_walk(env->event_loop(),
            exclude_network ? WalkHandleNoNetwork : WalkHandleNetwork,
            static_cast<void*>(&writer));

    writer.json_start();
    writer.json_keyvalue("type", "loop");
    writer.json_keyvalue("is_active",
        static_cast<bool>(uv_loop_alive(env->event_loop())));
    writer.json_keyvalue("address",
        ValueToHexString(reinterpret_cast<int64_t>(env->event_loop())));

    // Report Event loop idle time
    uint64_t idle_time = uv_metrics_idle_time(env->event_loop());
    writer.json_keyvalue("loopIdleTimeSeconds", 1.0 * idle_time / 1e9);
    writer.json_end();
  }

  writer.json_arrayend();

  writer.json_arraystart("workers");
  if (env != nullptr) {
    Mutex workers_mutex;
    ConditionVariable notify;
    std::vector<std::string> worker_infos;
    size_t expected_results = 0;

    env->ForEachWorker([&](Worker* w) {
      expected_results += w->RequestInterrupt([&](Environment* env) {
        std::ostringstream os;

        GetNodeReport(
            env, "Worker thread subreport", trigger, Local<Value>(), os);

        Mutex::ScopedLock lock(workers_mutex);
        worker_infos.emplace_back(os.str());
        notify.Signal(lock);
      });
    });

    Mutex::ScopedLock lock(workers_mutex);
    worker_infos.reserve(expected_results);
    while (worker_infos.size() < expected_results)
      notify.Wait(lock);
    for (const std::string& worker_info : worker_infos)
      writer.json_element(JSONWriter::ForeignJSON { worker_info });
  }
  writer.json_arrayend();

  // Report operating system information
  if (exclude_env == false) {
    PrintEnvironmentVariables(&writer);
  }
  PrintSystemInformation(&writer);

  writer.json_objectend();

  // Restore output stream formatting.
  out.copyfmt(old_state);
}

// Report Node.js version, OS version and machine information.
static void PrintVersionInformation(JSONWriter* writer, bool exclude_network) {
  std::ostringstream buf;
  // Report Node version
  buf << "v" << NODE_VERSION_STRING;
  writer->json_keyvalue("nodejsVersion", buf.str());
  buf.str("");

#ifndef _WIN32
  // Report compiler and runtime glibc versions where possible.
  const char* (*libc_version)();
  *(reinterpret_cast<void**>(&libc_version)) =
      dlsym(RTLD_DEFAULT, "gnu_get_libc_version");
  if (libc_version != nullptr)
    writer->json_keyvalue("glibcVersionRuntime", (*libc_version)());
#endif /* _WIN32 */

#ifdef __GLIBC__
  buf << __GLIBC__ << "." << __GLIBC_MINOR__;
  writer->json_keyvalue("glibcVersionCompiler", buf.str());
  buf.str("");
#endif

  // Report Process word size
  writer->json_keyvalue("wordSize", sizeof(void*) * 8);
  writer->json_keyvalue("arch", per_process::metadata.arch);
  writer->json_keyvalue("platform", per_process::metadata.platform);

  // Report deps component versions
  PrintComponentVersions(writer);

  // Report release metadata.
  PrintRelease(writer);

  // Report operating system and machine information
  uv_utsname_t os_info;

  if (uv_os_uname(&os_info) == 0) {
    writer->json_keyvalue("osName", os_info.sysname);
    writer->json_keyvalue("osRelease", os_info.release);
    writer->json_keyvalue("osVersion", os_info.version);
    writer->json_keyvalue("osMachine", os_info.machine);
  }

  PrintCpuInfo(writer);
  if (!exclude_network) PrintNetworkInterfaceInfo(writer);

  char host[UV_MAXHOSTNAMESIZE];
  size_t host_size = sizeof(host);

  if (uv_os_gethostname(host, &host_size) == 0)
    writer->json_keyvalue("host", host);
}

// Report CPU info
static void PrintCpuInfo(JSONWriter* writer) {
  uv_cpu_info_t* cpu_info;
  int count;
  if (uv_cpu_info(&cpu_info, &count) == 0) {
    writer->json_arraystart("cpus");
    for (int i = 0; i < count; i++) {
      writer->json_start();
      writer->json_keyvalue("model", cpu_info[i].model);
      writer->json_keyvalue("speed", cpu_info[i].speed);
      writer->json_keyvalue("user", cpu_info[i].cpu_times.user);
      writer->json_keyvalue("nice", cpu_info[i].cpu_times.nice);
      writer->json_keyvalue("sys", cpu_info[i].cpu_times.sys);
      writer->json_keyvalue("idle", cpu_info[i].cpu_times.idle);
      writer->json_keyvalue("irq", cpu_info[i].cpu_times.irq);
      writer->json_end();
    }
    writer->json_arrayend();
    uv_free_cpu_info(cpu_info, count);
  }
}

static void PrintNetworkInterfaceInfo(JSONWriter* writer) {
  uv_interface_address_t* interfaces;
  char ip[INET6_ADDRSTRLEN];
  char netmask[INET6_ADDRSTRLEN];
  char mac[18];
  int count;

  if (uv_interface_addresses(&interfaces, &count) == 0) {
    writer->json_arraystart("networkInterfaces");

    for (int i = 0; i < count; i++) {
      writer->json_start();
      writer->json_keyvalue("name", interfaces[i].name);
      writer->json_keyvalue("internal", !!interfaces[i].is_internal);
      snprintf(mac,
               sizeof(mac),
               "%02x:%02x:%02x:%02x:%02x:%02x",
               static_cast<unsigned char>(interfaces[i].phys_addr[0]),
               static_cast<unsigned char>(interfaces[i].phys_addr[1]),
               static_cast<unsigned char>(interfaces[i].phys_addr[2]),
               static_cast<unsigned char>(interfaces[i].phys_addr[3]),
               static_cast<unsigned char>(interfaces[i].phys_addr[4]),
               static_cast<unsigned char>(interfaces[i].phys_addr[5]));
      writer->json_keyvalue("mac", mac);

      if (interfaces[i].address.address4.sin_family == AF_INET) {
        uv_ip4_name(&interfaces[i].address.address4, ip, sizeof(ip));
        uv_ip4_name(&interfaces[i].netmask.netmask4, netmask, sizeof(netmask));
        writer->json_keyvalue("address", ip);
        writer->json_keyvalue("netmask", netmask);
        writer->json_keyvalue("family", "IPv4");
      } else if (interfaces[i].address.address4.sin_family == AF_INET6) {
        uv_ip6_name(&interfaces[i].address.address6, ip, sizeof(ip));
        uv_ip6_name(&interfaces[i].netmask.netmask6, netmask, sizeof(netmask));
        writer->json_keyvalue("address", ip);
        writer->json_keyvalue("netmask", netmask);
        writer->json_keyvalue("family", "IPv6");
        writer->json_keyvalue("scopeid",
                              interfaces[i].address.address6.sin6_scope_id);
      } else {
        writer->json_keyvalue("family", "unknown");
      }

      writer->json_end();
    }

    writer->json_arrayend();
    uv_free_interface_addresses(interfaces, count);
  }
}

static void PrintJavaScriptErrorProperties(JSONWriter* writer,
                                           Isolate* isolate,
                                           Local<Value> error) {
  writer->json_objectstart("errorProperties");
  if (!error.IsEmpty() && error->IsObject()) {
    TryCatch try_catch(isolate);
    Local<Object> error_obj = error.As<Object>();
    Local<Context> context = error_obj->GetIsolate()->GetCurrentContext();
    Local<Array> keys;
    if (!error_obj->GetOwnPropertyNames(context).ToLocal(&keys)) {
      return writer->json_objectend();  // the end of 'errorProperties'
    }
    uint32_t keys_length = keys->Length();
    for (uint32_t i = 0; i < keys_length; i++) {
      Local<Value> key;
      if (!keys->Get(context, i).ToLocal(&key) || !key->IsString()) {
        continue;
      }
      Local<Value> value;
      Local<String> value_string;
      if (!error_obj->Get(context, key).ToLocal(&value) ||
          !value->ToString(context).ToLocal(&value_string)) {
        continue;
      }
      node::Utf8Value k(isolate, key);
      if (k == "stack" || k == "message") continue;
      node::Utf8Value v(isolate, value_string);
      writer->json_keyvalue(k.ToStringView(), v.ToStringView());
    }
  }
  writer->json_objectend();  // the end of 'errorProperties'
}

static Maybe<std::string> ErrorToString(Isolate* isolate,
                                        Local<Context> context,
                                        Local<Value> error) {
  if (error.IsEmpty()) {
    return Nothing<std::string>();
  }

  MaybeLocal<String> maybe_str;
  // `ToString` is not available to Symbols.
  if (error->IsSymbol()) {
    maybe_str = error.As<v8::Symbol>()->ToDetailString(context);
  } else if (!error->IsObject()) {
    maybe_str = error->ToString(context);
  } else if (error->IsObject()) {
    MaybeLocal<Value> stack = error.As<Object>()->Get(
        context, FIXED_ONE_BYTE_STRING(isolate, "stack"));
    if (!stack.IsEmpty() && stack.ToLocalChecked()->IsString()) {
      maybe_str = stack.ToLocalChecked().As<String>();
    }
  }

  Local<String> js_str;
  if (!maybe_str.ToLocal(&js_str)) {
    return Nothing<std::string>();
  }
  String::Utf8Value sv(isolate, js_str);
  return Just<>(std::string(*sv, sv.length()));
}

static void PrintEmptyJavaScriptStack(JSONWriter* writer) {
  writer->json_keyvalue("message", "No stack.");
  writer->json_arraystart("stack");
  writer->json_element("Unavailable.");
  writer->json_arrayend();

  writer->json_objectstart("errorProperties");
  writer->json_objectend();
}

// Do our best to report the JavaScript stack without calling into JavaScript.
static void PrintJavaScriptStack(JSONWriter* writer,
                                 Isolate* isolate,
                                 const char* trigger) {
  HandleScope scope(isolate);
  Local<v8::StackTrace> stack;
  if (!GetCurrentStackTrace(isolate, MAX_FRAME_COUNT).ToLocal(&stack)) {
    PrintEmptyJavaScriptStack(writer);
    return;
  }

  RegisterState state;
  state.pc = nullptr;
  state.fp = &state;
  state.sp = &state;

  // in-out params
  SampleInfo info;
  void* samples[MAX_FRAME_COUNT];
  isolate->GetStackSample(state, samples, MAX_FRAME_COUNT, &info);

  writer->json_keyvalue("message", trigger);
  writer->json_arraystart("stack");
  for (int i = 0; i < stack->GetFrameCount(); i++) {
    Local<StackFrame> frame = stack->GetFrame(isolate, i);

    Utf8Value function_name(isolate, frame->GetFunctionName());
    Utf8Value script_name(isolate, frame->GetScriptName());
    const int line_number = frame->GetLineNumber();
    const int column = frame->GetColumn();

    std::string stack_line = SPrintF(
        "at %s (%s:%d:%d)", *function_name, *script_name, line_number, column);
    writer->json_element(stack_line);
  }
  writer->json_arrayend();
  writer->json_objectstart("errorProperties");
  writer->json_objectend();
}

// Report the JavaScript stack.
static void PrintJavaScriptErrorStack(JSONWriter* writer,
                                      Isolate* isolate,
                                      Local<Value> error,
                                      const char* trigger) {
  if (error.IsEmpty()) {
    return PrintJavaScriptStack(writer, isolate, trigger);
  }

  TryCatch try_catch(isolate);
  HandleScope scope(isolate);
  Local<Context> context = isolate->GetCurrentContext();
  std::string ss = "";
  if (!ErrorToString(isolate, context, error).To(&ss)) {
    PrintEmptyJavaScriptStack(writer);
    return;
  }

  int line = ss.find('\n');
  if (line == -1) {
    writer->json_keyvalue("message", ss);
  } else {
    std::string l = ss.substr(0, line);
    writer->json_keyvalue("message", l);
    writer->json_arraystart("stack");
    ss = ss.substr(line + 1);
    line = ss.find('\n');
    while (line != -1) {
      l = ss.substr(0, line);
      l.erase(l.begin(), std::find_if(l.begin(), l.end(), [](int ch) {
                return !std::iswspace(ch);
              }));
      writer->json_element(l);
      ss = ss.substr(line + 1);
      line = ss.find('\n');
    }
    writer->json_arrayend();
  }

  // Report summary JavaScript error properties backtrace
  PrintJavaScriptErrorProperties(writer, isolate, error);
}

// Report a native stack backtrace
static void PrintNativeStack(JSONWriter* writer) {
  auto sym_ctx = NativeSymbolDebuggingContext::New();
  void* frames[256];
  const int size = sym_ctx->GetStackTrace(frames, arraysize(frames));
  writer->json_arraystart("nativeStack");
  int i;
  for (i = 1; i < size; i++) {
    void* frame = frames[i];
    writer->json_start();
    writer->json_keyvalue("pc",
                          ValueToHexString(reinterpret_cast<uintptr_t>(frame)));
    writer->json_keyvalue("symbol", sym_ctx->LookupSymbol(frame).Display());
    writer->json_end();
  }
  writer->json_arrayend();
}

// Report V8 JavaScript heap information.
// This uses the existing V8 HeapStatistics and HeapSpaceStatistics APIs.
// The isolate->GetGCStatistics(&heap_stats) internal V8 API could potentially
// provide some more useful information - the GC history and the handle counts
static void PrintGCStatistics(JSONWriter* writer, Isolate* isolate) {
  HeapStatistics v8_heap_stats;
  isolate->GetHeapStatistics(&v8_heap_stats);
  HeapSpaceStatistics v8_heap_space_stats;

  writer->json_objectstart("javascriptHeap");
  writer->json_keyvalue("totalMemory", v8_heap_stats.total_heap_size());
  writer->json_keyvalue("executableMemory",
                        v8_heap_stats.total_heap_size_executable());
  writer->json_keyvalue("totalCommittedMemory",
                        v8_heap_stats.total_physical_size());
  writer->json_keyvalue("availableMemory",
                        v8_heap_stats.total_available_size());
  writer->json_keyvalue("totalGlobalHandlesMemory",
                        v8_heap_stats.total_global_handles_size());
  writer->json_keyvalue("usedGlobalHandlesMemory",
                        v8_heap_stats.used_global_handles_size());
  writer->json_keyvalue("usedMemory", v8_heap_stats.used_heap_size());
  writer->json_keyvalue("memoryLimit", v8_heap_stats.heap_size_limit());
  writer->json_keyvalue("mallocedMemory", v8_heap_stats.malloced_memory());
  writer->json_keyvalue("externalMemory", v8_heap_stats.external_memory());
  writer->json_keyvalue("peakMallocedMemory",
                        v8_heap_stats.peak_malloced_memory());
  writer->json_keyvalue("nativeContextCount",
                        v8_heap_stats.number_of_native_contexts());
  writer->json_keyvalue("detachedContextCount",
                        v8_heap_stats.number_of_detached_contexts());
  writer->json_keyvalue("doesZapGarbage", v8_heap_stats.does_zap_garbage());

  writer->json_objectstart("heapSpaces");
  // Loop through heap spaces
  for (size_t i = 0; i < isolate->NumberOfHeapSpaces(); i++) {
    isolate->GetHeapSpaceStatistics(&v8_heap_space_stats, i);
    writer->json_objectstart(v8_heap_space_stats.space_name());
    writer->json_keyvalue("memorySize", v8_heap_space_stats.space_size());
    writer->json_keyvalue(
        "committedMemory",
        v8_heap_space_stats.physical_space_size());
    writer->json_keyvalue(
        "capacity",
        v8_heap_space_stats.space_used_size() +
            v8_heap_space_stats.space_available_size());
    writer->json_keyvalue("used", v8_heap_space_stats.space_used_size());
    writer->json_keyvalue(
        "available", v8_heap_space_stats.space_available_size());
    writer->json_objectend();
  }

  writer->json_objectend();
  writer->json_objectend();
}

static void PrintResourceUsage(JSONWriter* writer) {
  // Get process uptime in seconds
  uint64_t uptime =
      (uv_hrtime() - per_process::node_start_time) / (NANOS_PER_SEC);
  if (uptime == 0) uptime = 1;  // avoid division by zero.

  // Process and current thread usage statistics
  uv_rusage_t rusage;
  writer->json_objectstart("resourceUsage");

  uint64_t free_memory = uv_get_free_memory();
  uint64_t total_memory = uv_get_total_memory();

  writer->json_keyvalue("free_memory", free_memory);
  writer->json_keyvalue("total_memory", total_memory);

  size_t rss;
  int err = uv_resident_set_memory(&rss);
  if (!err) {
    writer->json_keyvalue("rss", rss);
  }

  uint64_t constrained_memory = uv_get_constrained_memory();
  if (constrained_memory) {
    writer->json_keyvalue("constrained_memory", constrained_memory);
  }

  uint64_t available_memory = uv_get_available_memory();
  writer->json_keyvalue("available_memory", available_memory);

  if (uv_getrusage(&rusage) == 0) {
    double user_cpu =
        rusage.ru_utime.tv_sec + SEC_PER_MICROS * rusage.ru_utime.tv_usec;
    double kernel_cpu =
        rusage.ru_stime.tv_sec + SEC_PER_MICROS * rusage.ru_stime.tv_usec;
    writer->json_keyvalue("userCpuSeconds", user_cpu);
    writer->json_keyvalue("kernelCpuSeconds", kernel_cpu);
    double cpu_abs = user_cpu + kernel_cpu;
    double cpu_percentage = (cpu_abs / uptime) * 100.0;
    double user_cpu_percentage = (user_cpu / uptime) * 100.0;
    double kernel_cpu_percentage = (kernel_cpu / uptime) * 100.0;
    writer->json_keyvalue("cpuConsumptionPercent", cpu_percentage);
    writer->json_keyvalue("userCpuConsumptionPercent", user_cpu_percentage);
    writer->json_keyvalue("kernelCpuConsumptionPercent", kernel_cpu_percentage);
    writer->json_keyvalue("maxRss", rusage.ru_maxrss * 1024);
    writer->json_objectstart("pageFaults");
    writer->json_keyvalue("IORequired", rusage.ru_majflt);
    writer->json_keyvalue("IONotRequired", rusage.ru_minflt);
    writer->json_objectend();
    writer->json_objectstart("fsActivity");
    writer->json_keyvalue("reads", rusage.ru_inblock);
    writer->json_keyvalue("writes", rusage.ru_oublock);
    writer->json_objectend();
  }
  writer->json_objectend();
#ifdef RUSAGE_THREAD
  struct rusage stats;
  if (getrusage(RUSAGE_THREAD, &stats) == 0) {
    writer->json_objectstart("uvthreadResourceUsage");
    double user_cpu =
        stats.ru_utime.tv_sec + SEC_PER_MICROS * stats.ru_utime.tv_usec;
    double kernel_cpu =
        stats.ru_stime.tv_sec + SEC_PER_MICROS * stats.ru_stime.tv_usec;
    writer->json_keyvalue("userCpuSeconds", user_cpu);
    writer->json_keyvalue("kernelCpuSeconds", kernel_cpu);
    double cpu_abs = user_cpu + kernel_cpu;
    double cpu_percentage = (cpu_abs / uptime) * 100.0;
    double user_cpu_percentage = (user_cpu / uptime) * 100.0;
    double kernel_cpu_percentage = (kernel_cpu / uptime) * 100.0;
    writer->json_keyvalue("cpuConsumptionPercent", cpu_percentage);
    writer->json_keyvalue("userCpuConsumptionPercent", user_cpu_percentage);
    writer->json_keyvalue("kernelCpuConsumptionPercent", kernel_cpu_percentage);
    writer->json_objectstart("fsActivity");
    writer->json_keyvalue("reads", stats.ru_inblock);
    writer->json_keyvalue("writes", stats.ru_oublock);
    writer->json_objectend();
    writer->json_objectend();
  }
#endif  // RUSAGE_THREAD
}

static void PrintEnvironmentVariables(JSONWriter* writer) {
  uv_env_item_t* envitems;
  int envcount;
  int r;

  writer->json_objectstart("environmentVariables");

  {
    Mutex::ScopedLock lock(per_process::env_var_mutex);
    r = uv_os_environ(&envitems, &envcount);
  }

  if (r == 0) {
    for (int i = 0; i < envcount; i++)
      writer->json_keyvalue(envitems[i].name, envitems[i].value);

    uv_os_free_environ(envitems, envcount);
  }

  writer->json_objectend();
}

// Report operating system information.
static void PrintSystemInformation(JSONWriter* writer) {
#ifndef _WIN32
  static struct {
    const char* description;
    int id;
  } rlimit_strings[] = {
    {"core_file_size_blocks", RLIMIT_CORE},
    {"data_seg_size_bytes", RLIMIT_DATA},
    {"file_size_blocks", RLIMIT_FSIZE},
#if !(defined(_AIX) || defined(__sun))
    {"max_locked_memory_bytes", RLIMIT_MEMLOCK},
#endif
#ifndef __sun
    {"max_memory_size_bytes", RLIMIT_RSS},
#endif
    {"open_files", RLIMIT_NOFILE},
    {"stack_size_bytes", RLIMIT_STACK},
    {"cpu_time_seconds", RLIMIT_CPU},
#ifndef __sun
    {"max_user_processes", RLIMIT_NPROC},
#endif
#ifndef __OpenBSD__
    {"virtual_memory_bytes", RLIMIT_AS}
#endif
  };

  writer->json_objectstart("userLimits");
  struct rlimit limit;
  std::string soft, hard;

  for (size_t i = 0; i < arraysize(rlimit_strings); i++) {
    if (getrlimit(rlimit_strings[i].id, &limit) == 0) {
      writer->json_objectstart(rlimit_strings[i].description);

      if (limit.rlim_cur == RLIM_INFINITY)
        writer->json_keyvalue("soft", "unlimited");
      else
        writer->json_keyvalue("soft", limit.rlim_cur);

      if (limit.rlim_max == RLIM_INFINITY)
        writer->json_keyvalue("hard", "unlimited");
      else
        writer->json_keyvalue("hard", limit.rlim_max);

      writer->json_objectend();
    }
  }
  writer->json_objectend();
#endif  // _WIN32

  PrintLoadedLibraries(writer);
}

// Report a list of loaded native libraries.
static void PrintLoadedLibraries(JSONWriter* writer) {
  writer->json_arraystart("sharedObjects");
  std::vector<std::string> modules =
      NativeSymbolDebuggingContext::GetLoadedLibraries();
  for (auto const& module_name : modules) writer->json_element(module_name);
  writer->json_arrayend();
}

// Obtain and report the node and subcomponent version strings.
static void PrintComponentVersions(JSONWriter* writer) {
  std::stringstream buf;

  writer->json_objectstart("componentVersions");

#define V(key) +1
  std::pair<std::string_view, std::string_view>
      versions_array[NODE_VERSIONS_KEYS(V)];
#undef V
  auto* slot = &versions_array[0];

#define V(key)                                                                 \
  do {                                                                         \
    *slot++ = std::pair<std::string_view, std::string_view>(                   \
        #key, per_process::metadata.versions.key);                             \
  } while (0);
  NODE_VERSIONS_KEYS(V)
#undef V

  std::sort(&versions_array[0],
            &versions_array[arraysize(versions_array)],
            [](auto& a, auto& b) { return a.first < b.first; });

  for (const auto& version : versions_array) {
    writer->json_keyvalue(version.first, version.second);
  }

  writer->json_objectend();
}

// Report runtime release information.
static void PrintRelease(JSONWriter* writer) {
  writer->json_objectstart("release");
  writer->json_keyvalue("name", per_process::metadata.release.name);
#if NODE_VERSION_IS_LTS
  writer->json_keyvalue("lts", per_process::metadata.release.lts);
#endif

#ifdef NODE_HAS_RELEASE_URLS
  writer->json_keyvalue("headersUrl",
                        per_process::metadata.release.headers_url);
  writer->json_keyvalue("sourceUrl", per_process::metadata.release.source_url);
#ifdef _WIN32
  writer->json_keyvalue("libUrl", per_process::metadata.release.lib_url);
#endif  // _WIN32
#endif  // NODE_HAS_RELEASE_URLS

  writer->json_objectend();
}

}  // namespace report

std::string TriggerNodeReport(Isolate* isolate,
                              Environment* env,
                              const char* message,
                              const char* trigger,
                              const std::string& name,
                              Local<Value> error) {
  std::string filename;

  // Determine the required report filename. In order of priority:
  //   1) supplied on API 2) configured on startup 3) default generated
  if (!name.empty()) {
    // we may not always be in a great state when generating a node report
    // allow for the case where we don't have an env
    if (env != nullptr) {
      THROW_IF_INSUFFICIENT_PERMISSIONS(
          env, permission::PermissionScope::kFileSystemWrite, name, name);
      // Filename was specified as API parameter.
    }
    filename = name;
  } else {
    std::string report_filename;
    {
      Mutex::ScopedLock lock(per_process::cli_options_mutex);
      report_filename = per_process::cli_options->report_filename;
    }
    if (report_filename.length() > 0) {
      // File name was supplied via start-up option.
      filename = report_filename;
    } else {
      filename = *DiagnosticFilename(
          env != nullptr ? env->thread_id() : 0, "report", "json");
    }
    if (env != nullptr) {
      THROW_IF_INSUFFICIENT_PERMISSIONS(
          env,
          permission::PermissionScope::kFileSystemWrite,
          std::string_view(Environment::GetCwd(env->exec_path())),
          filename);
    }
  }

  // Open the report file stream for writing. Supports stdout/err,
  // user-specified or (default) generated name
  std::ofstream outfile;
  std::ostream* outstream;
  if (filename == "stdout") {
    outstream = &std::cout;
  } else if (filename == "stderr") {
    outstream = &std::cerr;
  } else {
    std::string report_directory;
    {
      Mutex::ScopedLock lock(per_process::cli_options_mutex);
      report_directory = per_process::cli_options->report_directory;
    }
    // Regular file. Append filename to directory path if one was specified
    if (report_directory.length() > 0) {
      std::string pathname = report_directory + kPathSeparator + filename;
      outfile.open(pathname, std::ios::out | std::ios::binary);
    } else {
      outfile.open(filename, std::ios::out | std::ios::binary);
    }
    // Check for errors on the file open
    if (!outfile.is_open()) {
      std::cerr << "\nFailed to open Node.js report file: " << filename;

      if (report_directory.length() > 0)
        std::cerr << " directory: " << report_directory;

      std::cerr << " (errno: " << errno << ")" << std::endl;
      return "";
    }
    outstream = &outfile;
    std::cerr << "\nWriting Node.js report to file: " << filename;
  }

  bool compact;
  {
    Mutex::ScopedLock lock(per_process::cli_options_mutex);
    compact = per_process::cli_options->report_compact;
  }

  bool exclude_network = env != nullptr ? env->options()->report_exclude_network
                                        : per_process::cli_options->per_isolate
                                              ->per_env->report_exclude_network;
  bool exclude_env =
      env != nullptr
          ? env->report_exclude_env()
          : per_process::cli_options->per_isolate->per_env->report_exclude_env;

  report::WriteNodeReport(isolate,
                          env,
                          message,
                          trigger,
                          filename,
                          *outstream,
                          error,
                          compact,
                          exclude_network,
                          exclude_env);

  // Do not close stdout/stderr, only close files we opened.
  if (outfile.is_open()) {
    outfile.close();
  }

  // Do not mix JSON and free-form text on stderr.
  if (filename != "stderr") {
    std::cerr << "\nNode.js report completed" << std::endl;
  }
  return filename;
}

// External function to trigger a report, writing to file.
std::string TriggerNodeReport(Isolate* isolate,
                              const char* message,
                              const char* trigger,
                              const std::string& name,
                              Local<Value> error) {
  Environment* env = nullptr;
  if (isolate != nullptr) {
    env = Environment::GetCurrent(isolate);
  }
  return TriggerNodeReport(isolate, env, message, trigger, name, error);
}

// External function to trigger a report, writing to file.
std::string TriggerNodeReport(Environment* env,
                              const char* message,
                              const char* trigger,
                              const std::string& name,
                              Local<Value> error) {
  return TriggerNodeReport(env != nullptr ? env->isolate() : nullptr,
                           env,
                           message,
                           trigger,
                           name,
                           error);
}

// External function to trigger a report, writing to a supplied stream.
void GetNodeReport(Isolate* isolate,
                   const char* message,
                   const char* trigger,
                   Local<Value> error,
                   std::ostream& out) {
  Environment* env = nullptr;
  if (isolate != nullptr) {
    env = Environment::GetCurrent(isolate);
  }
  bool exclude_network = env != nullptr ? env->options()->report_exclude_network
                                        : per_process::cli_options->per_isolate
                                              ->per_env->report_exclude_network;
  bool exclude_env =
      env != nullptr
          ? env->report_exclude_env()
          : per_process::cli_options->per_isolate->per_env->report_exclude_env;
  report::WriteNodeReport(isolate,
                          env,
                          message,
                          trigger,
                          "",
                          out,
                          error,
                          false,
                          exclude_network,
                          exclude_env);
}

// External function to trigger a report, writing to a supplied stream.
void GetNodeReport(Environment* env,
                   const char* message,
                   const char* trigger,
                   Local<Value> error,
                   std::ostream& out) {
  Isolate* isolate = nullptr;
  if (env != nullptr) {
    isolate = env->isolate();
  }
  bool exclude_network = env != nullptr ? env->options()->report_exclude_network
                                        : per_process::cli_options->per_isolate
                                              ->per_env->report_exclude_network;
  bool exclude_env =
      env != nullptr
          ? env->report_exclude_env()
          : per_process::cli_options->per_isolate->per_env->report_exclude_env;
  report::WriteNodeReport(isolate,
                          env,
                          message,
                          trigger,
                          "",
                          out,
                          error,
                          false,
                          exclude_network,
                          exclude_env);
}

}  // namespace node
