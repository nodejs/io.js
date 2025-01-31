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

#include "node.h"
#include "node_dotenv.h"
#include "node_task_runner.h"

// ========== local headers ==========

#include "debug_utils-inl.h"
#include "env-inl.h"
#include "histogram-inl.h"
#include "memory_tracker-inl.h"
#include "node_binding.h"
#include "node_builtins.h"
#include "node_errors.h"
#include "node_internals.h"
#include "node_main_instance.h"
#include "node_metadata.h"
#include "node_options-inl.h"
#include "node_perf.h"
#include "node_process-inl.h"
#include "node_realm-inl.h"
#include "node_report.h"
#include "node_revert.h"
#include "node_sea.h"
#include "node_snapshot_builder.h"
#include "node_v8_platform-inl.h"
#include "node_version.h"

#if HAVE_OPENSSL
#include "ncrypto.h"
#include "node_crypto.h"
#endif

#if defined(NODE_HAVE_I18N_SUPPORT)
#include "node_i18n.h"
#endif

#if HAVE_INSPECTOR
#include "inspector_agent.h"
#include "inspector_io.h"
#endif

#if NODE_USE_V8_PLATFORM
#include "libplatform/libplatform.h"
#endif  // NODE_USE_V8_PLATFORM
#include "v8-profiler.h"

#include "cppgc/platform.h"

#if HAVE_INSPECTOR
#include "inspector/worker_inspector.h"  // ParentInspectorHandle
#endif

#ifdef NODE_ENABLE_VTUNE_PROFILING
#include "../deps/v8/src/third_party/vtune/v8-vtune.h"
#endif

#include "large_pages/node_large_page.h"

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#define NODE_USE_V8_WASM_TRAP_HANDLER 1
#else
#define NODE_USE_V8_WASM_TRAP_HANDLER 0
#endif

#if NODE_USE_V8_WASM_TRAP_HANDLER
#if defined(_WIN32)
#include "v8-wasm-trap-handler-win.h"
#else
#include <atomic>
#include "v8-wasm-trap-handler-posix.h"
#endif
#endif  // NODE_USE_V8_WASM_TRAP_HANDLER

// ========== global C headers ==========

#include <fcntl.h>  // _O_RDWR
#include <sys/types.h>

#if defined(NODE_HAVE_I18N_SUPPORT)
#include <unicode/uvernum.h>
#include <unicode/utypes.h>
#endif


#if defined(LEAK_SANITIZER)
#include <sanitizer/lsan_interface.h>
#endif

#if defined(_MSC_VER)
#include <direct.h>
#include <io.h>
#define STDIN_FILENO 0
#else
#include <pthread.h>
#include <sys/resource.h>  // getrlimit, setrlimit
#include <termios.h>       // tcgetattr, tcsetattr
#include <unistd.h>        // STDIN_FILENO, STDERR_FILENO
#endif

// ========== global C++ headers ==========

#include <cerrno>
#include <climits>  // PATH_MAX
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <tuple>
#include <vector>

namespace node {

using v8::Array;
using v8::Context;
using v8::EscapableHandleScope;
using v8::Function;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Object;
using v8::V8;
using v8::Value;

namespace per_process {

// node_dotenv.h
// Instance is used to store environment variables including NODE_OPTIONS.
node::Dotenv dotenv_file = Dotenv();

// node_revert.h
// Bit flag used to track security reverts.
unsigned int reverted_cve = 0;

// util.h
// Tells whether the per-process V8::Initialize() is called and
// if it is safe to call v8::Isolate::TryGetCurrent().
bool v8_initialized = false;

// node_internals.h
// process-relative uptime base in nanoseconds, initialized in node::Start()
uint64_t node_start_time;

#if NODE_USE_V8_WASM_TRAP_HANDLER && defined(_WIN32)
PVOID old_vectored_exception_handler;
#endif

// node_v8_platform-inl.h
struct V8Platform v8_platform;
}  // namespace per_process

// The section in the OpenSSL configuration file to be loaded.
const char* conf_section_name = STRINGIFY(NODE_OPENSSL_CONF_NAME);

#ifdef __POSIX__
void SignalExit(int signo, siginfo_t* info, void* ucontext) {
  ResetStdio();
  raise(signo);
}
#endif  // __POSIX__

#if HAVE_INSPECTOR
void Environment::InitializeInspector(
    std::unique_ptr<inspector::ParentInspectorHandle> parent_handle) {
  std::string inspector_path;
  bool is_main = !parent_handle;
  if (parent_handle) {
    inspector_path = parent_handle->url();
    inspector_agent_->SetParentHandle(std::move(parent_handle));
  } else {
    inspector_path = argv_.size() > 1 ? argv_[1].c_str() : "";
  }

  CHECK(!inspector_agent_->IsListening());
  // Inspector agent can't fail to start, but if it was configured to listen
  // right away on the websocket port and fails to bind/etc, this will return
  // false.
  inspector_agent_->Start(inspector_path,
                          options_->debug_options(),
                          inspector_host_port(),
                          is_main);
  if (options_->debug_options().inspector_enabled &&
      !inspector_agent_->IsListening()) {
    return;
  }

  if (should_wait_for_inspector_frontend()) {
    WaitForInspectorFrontendByOptions();
  }

  profiler::StartProfilers(this);
}

void Environment::WaitForInspectorFrontendByOptions() {
  if (!inspector_agent_->WaitForConnectByOptions()) {
    return;
  }

  if (inspector_agent_->options().break_node_first_line) {
    inspector_agent_->PauseOnNextJavascriptStatement("Break at bootstrap");
  }

  return;
}
#endif  // HAVE_INSPECTOR

void Environment::InitializeDiagnostics() {
  isolate_->GetHeapProfiler()->AddBuildEmbedderGraphCallback(
      Environment::BuildEmbedderGraph, this);
  if (heap_snapshot_near_heap_limit_ > 0) {
    AddHeapSnapshotNearHeapLimitCallback();
  }
  if (options_->trace_uncaught)
    isolate_->SetCaptureStackTraceForUncaughtExceptions(true);
  if (options_->trace_promises) {
    isolate_->SetPromiseHook(TracePromises);
  }
}

static
MaybeLocal<Value> StartExecution(Environment* env, const char* main_script_id) {
  EscapableHandleScope scope(env->isolate());
  CHECK_NOT_NULL(main_script_id);
  Realm* realm = env->principal_realm();

  return scope.EscapeMaybe(realm->ExecuteBootstrapper(main_script_id));
}

// Convert the result returned by an intermediate main script into
// StartExecutionCallbackInfo. Currently the result is an array containing
// [process, requireFunction, cjsRunner]
std::optional<StartExecutionCallbackInfo> CallbackInfoFromArray(
    Local<Context> context, Local<Value> result) {
  CHECK(result->IsArray());
  Local<Array> args = result.As<Array>();
  CHECK_EQ(args->Length(), 3);
  Local<Value> process_obj, require_fn, runcjs_fn;
  if (!args->Get(context, 0).ToLocal(&process_obj) ||
      !args->Get(context, 1).ToLocal(&require_fn) ||
      !args->Get(context, 2).ToLocal(&runcjs_fn)) {
    return std::nullopt;
  }
  CHECK(process_obj->IsObject());
  CHECK(require_fn->IsFunction());
  CHECK(runcjs_fn->IsFunction());
  // TODO(joyeecheung): some support for running ESM as an entrypoint
  // is needed. The simplest API would be to add a run_esm to
  // StartExecutionCallbackInfo which compiles, links (to builtins)
  // and evaluates a SourceTextModule.
  // TODO(joyeecheung): the env pointer should be part of
  // StartExecutionCallbackInfo, otherwise embedders are forced to use
  // lambdas to pass it into the callback, which can make the code
  // difficult to read.
  node::StartExecutionCallbackInfo info{process_obj.As<Object>(),
                                        require_fn.As<Function>(),
                                        runcjs_fn.As<Function>()};
  return info;
}

MaybeLocal<Value> StartExecution(Environment* env, StartExecutionCallback cb) {
  InternalCallbackScope callback_scope(
      env,
      Object::New(env->isolate()),
      { 1, 0 },
      InternalCallbackScope::kSkipAsyncHooks);

  // Only snapshot builder or embedder applications set the
  // callback.
  if (cb != nullptr) {
    EscapableHandleScope scope(env->isolate());

    Local<Value> result;
    if (env->isolate_data()->is_building_snapshot()) {
      if (!StartExecution(env, "internal/main/mksnapshot").ToLocal(&result)) {
        return MaybeLocal<Value>();
      }
    } else {
      if (!StartExecution(env, "internal/main/embedding").ToLocal(&result)) {
        return MaybeLocal<Value>();
      }
    }

    auto info = CallbackInfoFromArray(env->context(), result);
    if (!info.has_value()) {
      MaybeLocal<Value>();
    }
#if HAVE_INSPECTOR
    if (env->options()->debug_options().break_first_line) {
      env->inspector_agent()->PauseOnNextJavascriptStatement("Break on start");
    }
#endif

    env->performance_state()->Mark(
        performance::NODE_PERFORMANCE_MILESTONE_BOOTSTRAP_COMPLETE);
    return scope.EscapeMaybe(cb(info.value()));
  }

  CHECK(!env->isolate_data()->is_building_snapshot());

#ifndef DISABLE_SINGLE_EXECUTABLE_APPLICATION
  // Snapshot in SEA is only loaded for the main thread.
  if (sea::IsSingleExecutable() && env->is_main_thread()) {
    sea::SeaResource sea = sea::FindSingleExecutableResource();
    // The SEA preparation blob building process should already enforce this,
    // this check is just here to guard against the unlikely case where
    // the SEA preparation blob has been manually modified by someone.
    CHECK_IMPLIES(sea.use_snapshot(),
                  !env->snapshot_deserialize_main().IsEmpty());
  }
#endif

  // Ignore env file if we're in watch mode.
  // Without it env is not updated when restarting child process.
  // Child process has --watch flag removed, so it will load the file.
  if (env->options()->has_env_file_string && !env->options()->watch_mode) {
    per_process::dotenv_file.SetEnvironment(env);
  }

  // TODO(joyeecheung): move these conditions into JS land and let the
  // deserialize main function take precedence. For workers, we need to
  // move the pre-execution part into a different file that can be
  // reused when dealing with user-defined main functions.
  if (!env->snapshot_deserialize_main().IsEmpty()) {
    // Custom worker snapshot is not supported yet,
    // so workers can't have deserialize main functions.
    CHECK(env->is_main_thread());
    return env->RunSnapshotDeserializeMain();
  }

  if (env->worker_context() != nullptr) {
    return StartExecution(env, "internal/main/worker_thread");
  }

  std::string first_argv;
  if (env->argv().size() > 1) {
    first_argv = env->argv()[1];
  }

  if (first_argv == "inspect") {
    return StartExecution(env, "internal/main/inspect");
  }

  if (per_process::cli_options->print_help) {
    return StartExecution(env, "internal/main/print_help");
  }

  if (env->options()->prof_process) {
    return StartExecution(env, "internal/main/prof_process");
  }

  // -e/--eval without -i/--interactive
  if (env->options()->has_eval_string && !env->options()->force_repl) {
    return StartExecution(env, "internal/main/eval_string");
  }

  if (env->options()->syntax_check_only) {
    return StartExecution(env, "internal/main/check_syntax");
  }

  if (env->options()->test_runner) {
    return StartExecution(env, "internal/main/test_runner");
  }

  if (env->options()->watch_mode) {
    return StartExecution(env, "internal/main/watch_mode");
  }

  if (!first_argv.empty() && first_argv != "-") {
    return StartExecution(env, "internal/main/run_main_module");
  }

  if (env->options()->force_repl || uv_guess_handle(STDIN_FILENO) == UV_TTY) {
    return StartExecution(env, "internal/main/repl");
  }

  return StartExecution(env, "internal/main/eval_stdin");
}

#ifdef __POSIX__
typedef void (*sigaction_cb)(int signo, siginfo_t* info, void* ucontext);
#endif
#if NODE_USE_V8_WASM_TRAP_HANDLER
static std::atomic<bool> is_wasm_trap_handler_configured{false};
#if defined(_WIN32)
static LONG WINAPI TrapWebAssemblyOrContinue(EXCEPTION_POINTERS* exception) {
  if (v8::TryHandleWebAssemblyTrapWindows(exception)) {
    return EXCEPTION_CONTINUE_EXECUTION;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#else
static std::atomic<sigaction_cb> previous_sigsegv_action;
// TODO(align behavior between macos and other in next major version)
#if defined(__APPLE__)
static std::atomic<sigaction_cb> previous_sigbus_action;
#endif  // __APPLE__

void TrapWebAssemblyOrContinue(int signo, siginfo_t* info, void* ucontext) {
  if (!v8::TryHandleWebAssemblyTrapPosix(signo, info, ucontext)) {
#if defined(__APPLE__)
    sigaction_cb prev = signo == SIGBUS ? previous_sigbus_action.load()
                                        : previous_sigsegv_action.load();
#else
    sigaction_cb prev = previous_sigsegv_action.load();
#endif  // __APPLE__
    if (prev != nullptr) {
      prev(signo, info, ucontext);
    } else {
      // Reset to the default signal handler, i.e. cause a hard crash.
      struct sigaction sa;
      memset(&sa, 0, sizeof(sa));
      sa.sa_handler = SIG_DFL;
      CHECK_EQ(sigaction(signo, &sa, nullptr), 0);

      ResetStdio();
      raise(signo);
    }
  }
}
#endif  // defined(_WIN32)
#endif  // NODE_USE_V8_WASM_TRAP_HANDLER

#ifdef __POSIX__
void RegisterSignalHandler(int signal,
                           sigaction_cb handler,
                           bool reset_handler) {
  CHECK_NOT_NULL(handler);
#if NODE_USE_V8_WASM_TRAP_HANDLER
  // Stash the user-registered handlers for TrapWebAssemblyOrContinue
  // to call out to when the signal is not coming from a WASM OOM.
  if (signal == SIGSEGV && is_wasm_trap_handler_configured.load()) {
    CHECK(previous_sigsegv_action.is_lock_free());
    CHECK(!reset_handler);
    previous_sigsegv_action.store(handler);
    return;
  }
  // TODO(align behavior between macos and other in next major version)
#if defined(__APPLE__)
  if (signal == SIGBUS && is_wasm_trap_handler_configured.load()) {
    CHECK(previous_sigbus_action.is_lock_free());
    CHECK(!reset_handler);
    previous_sigbus_action.store(handler);
    return;
  }
#endif  // __APPLE__
#endif  // NODE_USE_V8_WASM_TRAP_HANDLER
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = handler;
  sa.sa_flags = reset_handler ? SA_RESETHAND : 0;
  sigfillset(&sa.sa_mask);
  CHECK_EQ(sigaction(signal, &sa, nullptr), 0);
}
#endif  // __POSIX__

#ifdef __POSIX__
static struct {
  int flags;
  bool isatty;
  struct stat stat;
  struct termios termios;
} stdio[1 + STDERR_FILENO];
#endif  // __POSIX__

void ResetSignalHandlers() {
#ifdef __POSIX__
  // Restore signal dispositions, the parent process may have changed them.
  struct sigaction act;
  memset(&act, 0, sizeof(act));

  // The hard-coded upper limit is because NSIG is not very reliable; on Linux,
  // it evaluates to 32, 34 or 64, depending on whether RT signals are enabled.
  // Counting up to SIGRTMIN doesn't work for the same reason.
  for (unsigned nr = 1; nr < kMaxSignal; nr += 1) {
    if (nr == SIGKILL || nr == SIGSTOP)
      continue;
    act.sa_handler = (nr == SIGPIPE || nr == SIGXFSZ) ? SIG_IGN : SIG_DFL;
    if (act.sa_handler == SIG_DFL) {
      // The only bad handler value we can inherit from before exec is SIG_IGN
      // (any actual function pointer is reset to SIG_DFL during exec).
      // If that's the case, we want to reset it back to SIG_DFL.
      // However, it's also possible that an embeder (or an LD_PRELOAD-ed
      // library) has set up own signal handler for own purposes
      // (e.g. profiling). If that's the case, we want to keep it intact.
      struct sigaction old;
      CHECK_EQ(0, sigaction(nr, nullptr, &old));
      if ((old.sa_flags & SA_SIGINFO) || old.sa_handler != SIG_IGN) continue;
    }
    CHECK_EQ(0, sigaction(nr, &act, nullptr));
  }
#endif  // __POSIX__
}

// We use uint32_t since that can be accessed as a lock-free atomic
// variable on all platforms that we support, which we require in
// order for its value to be usable inside signal handlers.
static std::atomic<uint32_t> init_process_flags = 0;
static_assert(
    std::is_same_v<std::underlying_type_t<ProcessInitializationFlags::Flags>,
                   uint32_t>);

static void PlatformInit(ProcessInitializationFlags::Flags flags) {
  // init_process_flags is accessed in ResetStdio(),
  // which can be called from signal handlers.
  CHECK(init_process_flags.is_lock_free());
  init_process_flags.store(flags);

  if (!(flags & ProcessInitializationFlags::kNoStdioInitialization)) {
    atexit(ResetStdio);
  }

#ifdef __POSIX__
  if (!(flags & ProcessInitializationFlags::kNoStdioInitialization)) {
    // Disable stdio buffering, it interacts poorly with printf()
    // calls elsewhere in the program (e.g., any logging from V8.)
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    // Make sure file descriptors 0-2 are valid before we start logging
    // anything.
    for (auto& s : stdio) {
      const int fd = &s - stdio;
      if (fstat(fd, &s.stat) == 0) continue;

      // Anything but EBADF means something is seriously wrong.  We don't
      // have to special-case EINTR, fstat() is not interruptible.
      if (errno != EBADF) ABORT();

      // If EBADF (file descriptor doesn't exist), open /dev/null and duplicate
      // its file descriptor to the invalid file descriptor.  Make sure *that*
      // file descriptor is valid.  POSIX doesn't guarantee the next file
      // descriptor open(2) gives us is the lowest available number anymore in
      // POSIX.1-2017, which is why dup2(2) is needed.
      int null_fd;

      do {
        null_fd = open("/dev/null", O_RDWR);
      } while (null_fd < 0 && errno == EINTR);

      if (null_fd != fd) {
        int err;

        do {
          err = dup2(null_fd, fd);
        } while (err < 0 && errno == EINTR);
        CHECK_EQ(err, 0);
      }

      if (fstat(fd, &s.stat) < 0) ABORT();
    }
  }

  if (!(flags & ProcessInitializationFlags::kNoDefaultSignalHandling)) {
#if HAVE_INSPECTOR
    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGUSR1);
    const int err = pthread_sigmask(SIG_SETMASK, &sigmask, nullptr);
    CHECK_EQ(err, 0);
#endif  // HAVE_INSPECTOR

    ResetSignalHandlers();
  }

  if (!(flags & ProcessInitializationFlags::kNoStdioInitialization)) {
    // Record the state of the stdio file descriptors so we can restore it
    // on exit.  Needs to happen before installing signal handlers because
    // they make use of that information.
    for (auto& s : stdio) {
      const int fd = &s - stdio;
      int err;

      do {
        s.flags = fcntl(fd, F_GETFL);
      } while (s.flags == -1 && errno == EINTR);  // NOLINT
      CHECK_NE(s.flags, -1);

      if (uv_guess_handle(fd) != UV_TTY) continue;
      s.isatty = true;

      do {
        err = tcgetattr(fd, &s.termios);
      } while (err == -1 && errno == EINTR);  // NOLINT
      CHECK_EQ(err, 0);
    }
  }

  if (!(flags & ProcessInitializationFlags::kNoDefaultSignalHandling)) {
    RegisterSignalHandler(SIGINT, SignalExit, true);
    RegisterSignalHandler(SIGTERM, SignalExit, true);
  }

  if (!(flags & ProcessInitializationFlags::kNoAdjustResourceLimits)) {
    // Raise the open file descriptor limit.
    struct rlimit lim;
    if (getrlimit(RLIMIT_NOFILE, &lim) == 0 && lim.rlim_cur != lim.rlim_max) {
      // Do a binary search for the limit.
      rlim_t min = lim.rlim_cur;
      rlim_t max = 1 << 20;
      // But if there's a defined upper bound, don't search, just set it.
      if (lim.rlim_max != RLIM_INFINITY) {
        min = lim.rlim_max;
        max = lim.rlim_max;
      }
      do {
        lim.rlim_cur = min + (max - min) / 2;
        if (setrlimit(RLIMIT_NOFILE, &lim)) {
          max = lim.rlim_cur;
        } else {
          min = lim.rlim_cur;
        }
      } while (min + 1 < max);
    }
  }
#endif  // __POSIX__
#ifdef _WIN32
  if (!(flags & ProcessInitializationFlags::kNoStdioInitialization)) {
    for (int fd = 0; fd <= 2; ++fd) {
      auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
      if (handle == INVALID_HANDLE_VALUE ||
          GetFileType(handle) == FILE_TYPE_UNKNOWN) {
        // Ignore _close result. If it fails or not depends on used Windows
        // version. We will just check _open result.
        _close(fd);
        if (fd != _open("nul", _O_RDWR)) ABORT();
      }
    }
  }
#endif  // _WIN32
}

// Safe to call more than once and from signal handlers.
void ResetStdio() {
  if (init_process_flags.load() &
      ProcessInitializationFlags::kNoStdioInitialization) {
    return;
  }

  uv_tty_reset_mode();
#ifdef __POSIX__
  for (auto& s : stdio) {
    const int fd = &s - stdio;

    struct stat tmp;
    if (-1 == fstat(fd, &tmp)) {
      CHECK_EQ(errno, EBADF);  // Program closed file descriptor.
      continue;
    }

    bool is_same_file =
        (s.stat.st_dev == tmp.st_dev && s.stat.st_ino == tmp.st_ino);
    if (!is_same_file) continue;  // Program reopened file descriptor.

    int flags;
    do
      flags = fcntl(fd, F_GETFL);
    while (flags == -1 && errno == EINTR);  // NOLINT
    CHECK_NE(flags, -1);

    // Restore the O_NONBLOCK flag if it changed.
    if (O_NONBLOCK & (flags ^ s.flags)) {
      flags &= ~O_NONBLOCK;
      flags |= s.flags & O_NONBLOCK;

      int err;
      do
        err = fcntl(fd, F_SETFL, flags);
      while (err == -1 && errno == EINTR);  // NOLINT
      CHECK_NE(err, -1);
    }

    if (s.isatty) {
      sigset_t sa;
      int err;

      // We might be a background job that doesn't own the TTY so block SIGTTOU
      // before making the tcsetattr() call, otherwise that signal suspends us.
      sigemptyset(&sa);
      sigaddset(&sa, SIGTTOU);

      CHECK_EQ(0, pthread_sigmask(SIG_BLOCK, &sa, nullptr));
      do
        err = tcsetattr(fd, TCSANOW, &s.termios);
      while (err == -1 && errno == EINTR);  // NOLINT
      CHECK_EQ(0, pthread_sigmask(SIG_UNBLOCK, &sa, nullptr));

      // Normally we expect err == 0. But if macOS App Sandbox is enabled,
      // tcsetattr will fail with err == -1 and errno == EPERM.
      CHECK_IMPLIES(err != 0, err == -1 && errno == EPERM);
    }
  }
#endif  // __POSIX__
}

static ExitCode ProcessGlobalArgsInternal(std::vector<std::string>* args,
                                          std::vector<std::string>* exec_args,
                                          std::vector<std::string>* errors,
                                          OptionEnvvarSettings settings) {
  // Parse a few arguments which are specific to Node.
  std::vector<std::string> v8_args;

  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  options_parser::Parse(
      args,
      exec_args,
      &v8_args,
      per_process::cli_options.get(),
      settings,
      errors);

  if (!errors->empty()) return ExitCode::kInvalidCommandLineArgument;

  std::string revert_error;
  for (const std::string& cve : per_process::cli_options->security_reverts) {
    Revert(cve.c_str(), &revert_error);
    if (!revert_error.empty()) {
      errors->emplace_back(std::move(revert_error));
      // TODO(joyeecheung): merge into kInvalidCommandLineArgument.
      return ExitCode::kInvalidCommandLineArgument2;
    }
  }

  if (per_process::cli_options->disable_proto != "delete" &&
      per_process::cli_options->disable_proto != "throw" &&
      per_process::cli_options->disable_proto != "") {
    errors->emplace_back("invalid mode passed to --disable-proto");
    // TODO(joyeecheung): merge into kInvalidCommandLineArgument.
    return ExitCode::kInvalidCommandLineArgument2;
  }

  // TODO(aduh95): remove this when the harmony-import-attributes flag
  // is removed in V8.
  if (std::find(v8_args.begin(),
                v8_args.end(),
                "--no-harmony-import-attributes") == v8_args.end()) {
    v8_args.emplace_back("--harmony-import-attributes");
  }

  auto env_opts = per_process::cli_options->per_isolate->per_env;
  if (std::find(v8_args.begin(), v8_args.end(),
                "--abort-on-uncaught-exception") != v8_args.end() ||
      std::find(v8_args.begin(), v8_args.end(),
                "--abort_on_uncaught_exception") != v8_args.end()) {
    env_opts->abort_on_uncaught_exception = true;
  }

#ifdef __POSIX__
  // Block SIGPROF signals when sleeping in epoll_wait/kevent/etc.  Avoids the
  // performance penalty of frequent EINTR wakeups when the profiler is running.
  // Only do this for v8.log profiling, as it breaks v8::CpuProfiler users.
  if (std::find(v8_args.begin(), v8_args.end(), "--prof") != v8_args.end()) {
    uv_loop_configure(uv_default_loop(), UV_LOOP_BLOCK_SIGNAL, SIGPROF);
  }
#endif

  std::vector<char*> v8_args_as_char_ptr(v8_args.size());
  if (v8_args.size() > 0) {
    for (size_t i = 0; i < v8_args.size(); ++i)
      v8_args_as_char_ptr[i] = v8_args[i].data();
    int argc = v8_args.size();
    V8::SetFlagsFromCommandLine(&argc, v8_args_as_char_ptr.data(), true);
    v8_args_as_char_ptr.resize(argc);
  }

  // Anything that's still in v8_argv is not a V8 or a node option.
  for (size_t i = 1; i < v8_args_as_char_ptr.size(); i++)
    errors->push_back("bad option: " + std::string(v8_args_as_char_ptr[i]));

  if (v8_args_as_char_ptr.size() > 1)
    return ExitCode::kInvalidCommandLineArgument;

  return ExitCode::kNoFailure;
}

int ProcessGlobalArgs(std::vector<std::string>* args,
                      std::vector<std::string>* exec_args,
                      std::vector<std::string>* errors,
                      OptionEnvvarSettings settings) {
  return static_cast<int>(
      ProcessGlobalArgsInternal(args, exec_args, errors, settings));
}

static std::atomic_bool init_called{false};

// TODO(addaleax): Turn this into a wrapper around InitializeOncePerProcess()
// (with the corresponding additional flags set), then eventually remove this.
static ExitCode InitializeNodeWithArgsInternal(
    std::vector<std::string>* argv,
    std::vector<std::string>* exec_argv,
    std::vector<std::string>* errors,
    ProcessInitializationFlags::Flags flags) {
  // Make sure InitializeNodeWithArgs() is called only once.
  CHECK(!init_called.exchange(true));

  // Initialize node_start_time to get relative uptime.
  per_process::node_start_time = uv_hrtime();

  // Register built-in bindings
  binding::RegisterBuiltinBindings();

  // Make inherited handles noninheritable.
  if (!(flags & ProcessInitializationFlags::kEnableStdioInheritance) &&
      !(flags & ProcessInitializationFlags::kNoStdioInitialization)) {
    uv_disable_stdio_inheritance();
  }

  // Cache the original command line to be
  // used in diagnostic reports.
  per_process::cli_options->cmdline = *argv;

  // Node provides a "v8.setFlagsFromString" method to dynamically change flags.
  // Hence do not freeze flags when initializing V8. In a browser setting, this
  // is security relevant, for Node it's less important.
  V8::SetFlagsFromString("--no-freeze-flags-after-init");

#if defined(NODE_V8_OPTIONS)
  // Should come before the call to V8::SetFlagsFromCommandLine()
  // so the user can disable a flag --foo at run-time by passing
  // --no_foo from the command line.
  V8::SetFlagsFromString(NODE_V8_OPTIONS, sizeof(NODE_V8_OPTIONS) - 1);
#endif

  if (!!(flags & ProcessInitializationFlags::kGeneratePredictableSnapshot) ||
      per_process::cli_options->per_isolate->build_snapshot) {
    v8::V8::SetFlagsFromString("--predictable");
    v8::V8::SetFlagsFromString("--random_seed=42");
  }

  // Specify this explicitly to avoid being affected by V8 changes to the
  // default value.
  V8::SetFlagsFromString("--rehash-snapshot");

  HandleEnvOptions(per_process::cli_options->per_isolate->per_env);

  std::string node_options;
  auto env_files = node::Dotenv::GetDataFromArgs(*argv);

  if (!env_files.empty()) {
    CHECK(!per_process::v8_initialized);

    for (const auto& file_data : env_files) {
      switch (per_process::dotenv_file.ParsePath(file_data.path)) {
        case Dotenv::ParseResult::Valid:
          break;
        case Dotenv::ParseResult::InvalidContent:
          errors->push_back(file_data.path + ": invalid format");
          break;
        case Dotenv::ParseResult::FileError:
          if (file_data.is_optional) {
            fprintf(stderr,
                    "%s not found. Continuing without it.\n",
                    file_data.path.c_str());
            continue;
          }
          errors->push_back(file_data.path + ": not found");
          break;
        default:
          UNREACHABLE();
      }
    }

    per_process::dotenv_file.AssignNodeOptionsIfAvailable(&node_options);
  }

#if !defined(NODE_WITHOUT_NODE_OPTIONS)
  if (!(flags & ProcessInitializationFlags::kDisableNodeOptionsEnv)) {
    // NODE_OPTIONS environment variable is preferred over the file one.
    if (credentials::SafeGetenv("NODE_OPTIONS", &node_options) ||
        !node_options.empty()) {
      std::vector<std::string> env_argv =
          ParseNodeOptionsEnvVar(node_options, errors);

      if (!errors->empty()) return ExitCode::kInvalidCommandLineArgument;

      // [0] is expected to be the program name, fill it in from the real argv.
      env_argv.insert(env_argv.begin(), argv->at(0));

      const ExitCode exit_code = ProcessGlobalArgsInternal(
          &env_argv, nullptr, errors, kAllowedInEnvvar);
      if (exit_code != ExitCode::kNoFailure) return exit_code;
    }
  } else {
    std::string node_repl_external_env = {};
    if (credentials::SafeGetenv("NODE_REPL_EXTERNAL_MODULE",
                                &node_repl_external_env) ||
        !node_repl_external_env.empty()) {
      errors->emplace_back("NODE_REPL_EXTERNAL_MODULE can't be used with "
                           "kDisableNodeOptionsEnv");
      return ExitCode::kInvalidCommandLineArgument;
    }
  }
#endif

  if (!(flags & ProcessInitializationFlags::kDisableCLIOptions)) {
    const ExitCode exit_code =
        ProcessGlobalArgsInternal(argv, exec_argv, errors, kDisallowedInEnvvar);
    if (exit_code != ExitCode::kNoFailure) return exit_code;
  }

  // Set the process.title immediately after processing argv if --title is set.
  if (!per_process::cli_options->title.empty())
    uv_set_process_title(per_process::cli_options->title.c_str());

#if defined(NODE_HAVE_I18N_SUPPORT)
  if (!(flags & ProcessInitializationFlags::kNoICU)) {
    // If the parameter isn't given, use the env variable.
    if (per_process::cli_options->icu_data_dir.empty())
      credentials::SafeGetenv("NODE_ICU_DATA",
                              &per_process::cli_options->icu_data_dir);

#ifdef NODE_ICU_DEFAULT_DATA_DIR
    // If neither the CLI option nor the environment variable was specified,
    // fall back to the configured default
    if (per_process::cli_options->icu_data_dir.empty()) {
      // Check whether the NODE_ICU_DEFAULT_DATA_DIR contains the right data
      // file and can be read.
      static const char full_path[] =
          NODE_ICU_DEFAULT_DATA_DIR "/" U_ICUDATA_NAME ".dat";

      FILE* f = fopen(full_path, "rb");

      if (f != nullptr) {
        fclose(f);
        per_process::cli_options->icu_data_dir = NODE_ICU_DEFAULT_DATA_DIR;
      }
    }
#endif  // NODE_ICU_DEFAULT_DATA_DIR

    // Initialize ICU.
    // If icu_data_dir is empty here, it will load the 'minimal' data.
    std::string icu_error;
    if (!i18n::InitializeICUDirectory(per_process::cli_options->icu_data_dir,
                                      &icu_error)) {
      errors->push_back(icu_error +
                        ": Could not initialize ICU. "
                        "Check the directory specified by NODE_ICU_DATA or "
                        "--icu-data-dir contains " U_ICUDATA_NAME ".dat and "
                        "it's readable\n");
      return ExitCode::kInvalidCommandLineArgument;
    }
    per_process::metadata.versions.InitializeIntlVersions();
  }

# ifndef __POSIX__
  std::string tz;
  if (credentials::SafeGetenv("TZ", &tz) && !tz.empty()) {
    i18n::SetDefaultTimeZone(tz.c_str());
  }
# endif

#endif  // defined(NODE_HAVE_I18N_SUPPORT)

  // We should set node_is_initialized here instead of in node::Start,
  // otherwise embedders using node::Init to initialize everything will not be
  // able to set it and native addons will not load for them.
  node_is_initialized = true;
  return ExitCode::kNoFailure;
}

int InitializeNodeWithArgs(std::vector<std::string>* argv,
                           std::vector<std::string>* exec_argv,
                           std::vector<std::string>* errors,
                           ProcessInitializationFlags::Flags flags) {
  return static_cast<int>(
      InitializeNodeWithArgsInternal(argv, exec_argv, errors, flags));
}

static std::shared_ptr<InitializationResultImpl>
InitializeOncePerProcessInternal(const std::vector<std::string>& args,
                                 ProcessInitializationFlags::Flags flags =
                                     ProcessInitializationFlags::kNoFlags) {
  auto result = std::make_shared<InitializationResultImpl>();
  result->args_ = args;

  if (!(flags & ProcessInitializationFlags::kNoParseGlobalDebugVariables)) {
    // Initialized the enabled list for Debug() calls with system
    // environment variables.
    per_process::enabled_debug_list.Parse(nullptr);
  }

  PlatformInit(flags);

  // This needs to run *before* V8::Initialize().
  {
    result->exit_code_ = InitializeNodeWithArgsInternal(
        &result->args_, &result->exec_args_, &result->errors_, flags);
    if (result->exit_code_enum() != ExitCode::kNoFailure) {
      result->early_return_ = true;
      return result;
    }
  }

  if (!(flags & ProcessInitializationFlags::kNoUseLargePages) &&
      (per_process::cli_options->use_largepages == "on" ||
       per_process::cli_options->use_largepages == "silent")) {
    int lp_result = node::MapStaticCodeToLargePages();
    if (per_process::cli_options->use_largepages == "on" && lp_result != 0) {
      result->errors_.emplace_back(node::LargePagesError(lp_result));
    }
  }

  if (!per_process::cli_options->run.empty()) {
    auto positional_args = task_runner::GetPositionalArgs(args);
    result->early_return_ = true;
    task_runner::RunTask(
        result, per_process::cli_options->run, positional_args);
    return result;
  }

  if (!(flags & ProcessInitializationFlags::kNoPrintHelpOrVersionOutput)) {
    if (per_process::cli_options->print_version) {
      printf("%s\n", NODE_VERSION);
      result->exit_code_ = ExitCode::kNoFailure;
      result->early_return_ = true;
      return result;
    }

    if (per_process::cli_options->print_bash_completion) {
      std::string completion = options_parser::GetBashCompletion();
      printf("%s\n", completion.c_str());
      result->exit_code_ = ExitCode::kNoFailure;
      result->early_return_ = true;
      return result;
    }

    if (per_process::cli_options->print_v8_help) {
      V8::SetFlagsFromString("--help", static_cast<size_t>(6));
      result->exit_code_ = ExitCode::kNoFailure;
      result->early_return_ = true;
      return result;
    }
  }

  if (!(flags & ProcessInitializationFlags::kNoInitOpenSSL)) {
#if HAVE_OPENSSL
#ifndef OPENSSL_IS_BORINGSSL
    auto GetOpenSSLErrorString = []() -> std::string {
      std::string ret;
      ERR_print_errors_cb(
          [](const char* str, size_t len, void* opaque) {
            std::string* ret = static_cast<std::string*>(opaque);
            ret->append(str, len);
            ret->append("\n");
            return 0;
          },
          static_cast<void*>(&ret));
      return ret;
    };

    // In the case of FIPS builds we should make sure
    // the random source is properly initialized first.
#if OPENSSL_VERSION_MAJOR >= 3
    // Call OPENSSL_init_crypto to initialize OPENSSL_INIT_LOAD_CONFIG to
    // avoid the default behavior where errors raised during the parsing of the
    // OpenSSL configuration file are not propagated and cannot be detected.
    //
    // If FIPS is configured the OpenSSL configuration file will have an
    // .include pointing to the fipsmodule.cnf file generated by the openssl
    // fipsinstall command. If the path to this file is incorrect no error
    // will be reported.
    //
    // For Node.js this will mean that CSPRNG() will be called by V8 as
    // part of its initialization process, and CSPRNG() will in turn call
    // call RAND_status which will now always return 0, leading to an endless
    // loop and the node process will appear to hang/freeze.

    // Passing NULL as the config file will allow the default openssl.cnf file
    // to be loaded, but the default section in that file will not be used,
    // instead only the section that matches the value of conf_section_name
    // will be read from the default configuration file.
    const char* conf_file = nullptr;
    // To allow for using the previous default where the 'openssl_conf' appname
    // was used, the command line option 'openssl-shared-config' can be used to
    // force the old behavior.
    if (per_process::cli_options->openssl_shared_config) {
      conf_section_name = "openssl_conf";
    }
    // Use OPENSSL_CONF environment variable is set.
    std::string env_openssl_conf;
    credentials::SafeGetenv("OPENSSL_CONF", &env_openssl_conf);
    if (!env_openssl_conf.empty()) {
      conf_file = env_openssl_conf.c_str();
    }
    // Use --openssl-conf command line option if specified.
    if (!per_process::cli_options->openssl_config.empty()) {
      conf_file = per_process::cli_options->openssl_config.c_str();
    }

    OPENSSL_INIT_SETTINGS* settings = OPENSSL_INIT_new();
    OPENSSL_INIT_set_config_filename(settings, conf_file);
    OPENSSL_INIT_set_config_appname(settings, conf_section_name);
    OPENSSL_INIT_set_config_file_flags(settings,
                                       CONF_MFLAGS_IGNORE_MISSING_FILE);

    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, settings);
    OPENSSL_INIT_free(settings);

    if (ERR_peek_error() != 0) {
      // XXX: ERR_GET_REASON does not return something that is
      // useful as an exit code at all.
      result->exit_code_ =
          static_cast<ExitCode>(ERR_GET_REASON(ERR_peek_error()));
      result->early_return_ = true;
      result->errors_.emplace_back("OpenSSL configuration error:\n" +
                                   GetOpenSSLErrorString());
      return result;
    }
#else  // OPENSSL_VERSION_MAJOR < 3
    if (FIPS_mode()) {
      OPENSSL_init();
    }
#endif
    if (!crypto::ProcessFipsOptions()) {
      // XXX: ERR_GET_REASON does not return something that is
      // useful as an exit code at all.
      result->exit_code_ =
          static_cast<ExitCode>(ERR_GET_REASON(ERR_peek_error()));
      result->early_return_ = true;
      result->errors_.emplace_back(
          "OpenSSL error when trying to enable FIPS:\n" +
          GetOpenSSLErrorString());
      return result;
    }

    // Ensure CSPRNG is properly seeded.
    CHECK(ncrypto::CSPRNG(nullptr, 0));

    V8::SetEntropySource([](unsigned char* buffer, size_t length) {
      // V8 falls back to very weak entropy when this function fails
      // and /dev/urandom isn't available. That wouldn't be so bad if
      // the entropy was only used for Math.random() but it's also used for
      // hash table and address space layout randomization. Better to abort.
      CHECK(ncrypto::CSPRNG(buffer, length));
      return true;
    });
#endif  // !defined(OPENSSL_IS_BORINGSSL)
    {
      std::string extra_ca_certs;
      if (credentials::SafeGetenv("NODE_EXTRA_CA_CERTS", &extra_ca_certs))
        crypto::UseExtraCaCerts(extra_ca_certs);
    }
#endif  // HAVE_OPENSSL
  }

  if (!(flags & ProcessInitializationFlags::kNoInitializeNodeV8Platform)) {
    per_process::v8_platform.Initialize(
        static_cast<int>(per_process::cli_options->v8_thread_pool_size));
    result->platform_ = per_process::v8_platform.Platform();
  }

  if (!(flags & ProcessInitializationFlags::kNoInitializeV8)) {
    V8::Initialize();
  }

  if (!(flags & ProcessInitializationFlags::kNoInitializeCppgc)) {
    v8::PageAllocator* allocator = nullptr;
    if (result->platform_ != nullptr) {
      allocator = result->platform_->GetPageAllocator();
    }
    cppgc::InitializeProcess(allocator);
  }

#if NODE_USE_V8_WASM_TRAP_HANDLER
  bool use_wasm_trap_handler =
      !per_process::cli_options->disable_wasm_trap_handler;
  if (!(flags & ProcessInitializationFlags::kNoDefaultSignalHandling) &&
      use_wasm_trap_handler) {
#if defined(_WIN32)
    constexpr ULONG first = TRUE;
    per_process::old_vectored_exception_handler =
        AddVectoredExceptionHandler(first, TrapWebAssemblyOrContinue);
#else
    // Tell V8 to disable emitting WebAssembly
    // memory bounds checks. This means that we have
    // to catch the SIGSEGV/SIGBUS in TrapWebAssemblyOrContinue
    // and pass the signal context to V8.
    {
      struct sigaction sa;
      memset(&sa, 0, sizeof(sa));
      sa.sa_sigaction = TrapWebAssemblyOrContinue;
      sa.sa_flags = SA_SIGINFO;
      CHECK_EQ(sigaction(SIGSEGV, &sa, nullptr), 0);
// TODO(align behavior between macos and other in next major version)
#if defined(__APPLE__)
      CHECK_EQ(sigaction(SIGBUS, &sa, nullptr), 0);
#endif
    }
#endif  // defined(_WIN32)
    is_wasm_trap_handler_configured.store(true);
    V8::EnableWebAssemblyTrapHandler(false);
  }
#endif  // NODE_USE_V8_WASM_TRAP_HANDLER

  performance::performance_v8_start = PERFORMANCE_NOW();
  per_process::v8_initialized = true;

  return result;
}

std::shared_ptr<InitializationResult> InitializeOncePerProcess(
    const std::vector<std::string>& args,
    ProcessInitializationFlags::Flags flags) {
  return InitializeOncePerProcessInternal(args, flags);
}

void TearDownOncePerProcess() {
  const uint32_t flags = init_process_flags.load();
  ResetStdio();
  if (!(flags & ProcessInitializationFlags::kNoDefaultSignalHandling)) {
    ResetSignalHandlers();
  }

  if (!(flags & ProcessInitializationFlags::kNoInitializeCppgc)) {
    cppgc::ShutdownProcess();
  }

  per_process::v8_initialized = false;
  if (!(flags & ProcessInitializationFlags::kNoInitializeV8)) {
    V8::Dispose();
  }

#if NODE_USE_V8_WASM_TRAP_HANDLER && defined(_WIN32)
  if (is_wasm_trap_handler_configured.load()) {
    RemoveVectoredExceptionHandler(per_process::old_vectored_exception_handler);
  }
#endif

  if (!(flags & ProcessInitializationFlags::kNoInitializeNodeV8Platform)) {
    V8::DisposePlatform();
    // uv_run cannot be called from the time before the beforeExit callback
    // runs until the program exits unless the event loop has any referenced
    // handles after beforeExit terminates. This prevents unrefed timers
    // that happen to terminate during shutdown from being run unsafely.
    // Since uv_run cannot be called, uv_async handles held by the platform
    // will never be fully cleaned up.
    per_process::v8_platform.Dispose();
  }
}

ExitCode GenerateAndWriteSnapshotData(const SnapshotData** snapshot_data_ptr,
                                      const InitializationResultImpl* result) {
  ExitCode exit_code = result->exit_code_enum();
  // nullptr indicates there's no snapshot data.
  DCHECK_NULL(*snapshot_data_ptr);

  SnapshotConfig snapshot_config;
  const std::string& config_path =
      per_process::cli_options->per_isolate->build_snapshot_config;
  // For snapshot config read from JSON, we fix up process.argv[1] using the
  // "builder" field.
  std::vector<std::string> args_maybe_patched;
  args_maybe_patched.reserve(result->args().size() + 1);
  if (!config_path.empty()) {
    std::optional<SnapshotConfig> optional_config =
        ReadSnapshotConfig(config_path.c_str());
    if (!optional_config.has_value()) {
      return ExitCode::kGenericUserError;
    }
    snapshot_config = std::move(optional_config.value());
    DCHECK(snapshot_config.builder_script_path.has_value());
    args_maybe_patched.emplace_back(result->args()[0]);
    args_maybe_patched.emplace_back(
        snapshot_config.builder_script_path.value());
    if (result->args().size() > 1) {
      args_maybe_patched.insert(args_maybe_patched.end(),
                                result->args().begin() + 1,
                                result->args().end());
    }
  } else {
    snapshot_config.builder_script_path = result->args()[1];
    args_maybe_patched = result->args();
  }
  DCHECK(snapshot_config.builder_script_path.has_value());
  const std::string& builder_script =
      snapshot_config.builder_script_path.value();
  // node:embedded_snapshot_main indicates that we are using the
  // embedded snapshot and we are not supposed to clean it up.
  if (builder_script == "node:embedded_snapshot_main") {
    *snapshot_data_ptr = SnapshotBuilder::GetEmbeddedSnapshotData();
    if (*snapshot_data_ptr == nullptr) {
      // The Node.js binary is built without embedded snapshot
      fprintf(stderr,
              "node:embedded_snapshot_main was specified as snapshot "
              "entry point but Node.js was built without embedded "
              "snapshot.\n");
      exit_code = ExitCode::kInvalidCommandLineArgument;
      return exit_code;
    }
  } else {
    std::optional<std::string> builder_script_content;
    // Otherwise, load and run the specified builder script.
    std::unique_ptr<SnapshotData> generated_data =
        std::make_unique<SnapshotData>();
    if (builder_script != "node:generate_default_snapshot") {
      builder_script_content = std::string();
      int r = ReadFileSync(&(builder_script_content.value()),
                           builder_script.c_str());
      if (r != 0) {
        FPrintF(stderr,
                "Cannot read builder script %s for building snapshot. %s: %s\n",
                builder_script,
                uv_err_name(r),
                uv_strerror(r));
        return ExitCode::kGenericUserError;
      }
    } else {
      snapshot_config.builder_script_path = std::nullopt;
    }

    exit_code = node::SnapshotBuilder::Generate(generated_data.get(),
                                                args_maybe_patched,
                                                result->exec_args(),
                                                builder_script_content,
                                                snapshot_config);
    if (exit_code == ExitCode::kNoFailure) {
      *snapshot_data_ptr = generated_data.release();
    } else {
      return exit_code;
    }
  }

  // Get the path to write the snapshot blob to.
  std::string snapshot_blob_path;
  if (!per_process::cli_options->snapshot_blob.empty()) {
    snapshot_blob_path = per_process::cli_options->snapshot_blob;
  } else {
    // Defaults to snapshot.blob in the current working directory.
    snapshot_blob_path = std::string("snapshot.blob");
  }

  FILE* fp = fopen(snapshot_blob_path.c_str(), "wb");
  if (fp != nullptr) {
    (*snapshot_data_ptr)->ToFile(fp);
    fclose(fp);
  } else {
    fprintf(stderr,
            "Cannot open %s for writing a snapshot.\n",
            snapshot_blob_path.c_str());
    exit_code = ExitCode::kStartupSnapshotFailure;
  }
  return exit_code;
}

bool LoadSnapshotData(const SnapshotData** snapshot_data_ptr) {
  // nullptr indicates there's no snapshot data.
  DCHECK_NULL(*snapshot_data_ptr);

  bool is_sea = false;
#ifndef DISABLE_SINGLE_EXECUTABLE_APPLICATION
  if (sea::IsSingleExecutable()) {
    is_sea = true;
    sea::SeaResource sea = sea::FindSingleExecutableResource();
    if (sea.use_snapshot()) {
      std::unique_ptr<SnapshotData> read_data =
          std::make_unique<SnapshotData>();
      std::string_view snapshot = sea.main_code_or_snapshot;
      if (SnapshotData::FromBlob(read_data.get(), snapshot)) {
        *snapshot_data_ptr = read_data.release();
        return true;
      } else {
        fprintf(stderr, "Invalid snapshot data in single executable binary\n");
        return false;
      }
    }
  }
#endif

  // --snapshot-blob indicates that we are reading a customized snapshot.
  // Ignore it when we are loading from SEA.
  if (!is_sea && !per_process::cli_options->snapshot_blob.empty()) {
    std::string filename = per_process::cli_options->snapshot_blob;
    FILE* fp = fopen(filename.c_str(), "rb");
    if (fp == nullptr) {
      fprintf(stderr, "Cannot open %s", filename.c_str());
      return false;
    }
    std::unique_ptr<SnapshotData> read_data = std::make_unique<SnapshotData>();
    bool ok = SnapshotData::FromFile(read_data.get(), fp);
    fclose(fp);
    if (!ok) {
      return false;
    }
    *snapshot_data_ptr = read_data.release();
    return true;
  }

  if (per_process::cli_options->node_snapshot) {
    // If --snapshot-blob is not specified or if the SEA contains no snapshot,
    // we are reading the embedded snapshot, but we will skip it if
    // --no-node-snapshot is specified.
    const node::SnapshotData* read_data =
        SnapshotBuilder::GetEmbeddedSnapshotData();
    if (read_data != nullptr) {
      if (!read_data->Check()) {
        return false;
      }
      // If we fail to read the embedded snapshot, treat it as if Node.js
      // was built without one.
      *snapshot_data_ptr = read_data;
    }
  }

  return true;
}

static ExitCode StartInternal(int argc, char** argv) {
  CHECK_GT(argc, 0);

  // Hack around with the argv pointer. Used for process.title = "blah".
  argv = uv_setup_args(argc, argv);

  std::shared_ptr<InitializationResultImpl> result =
      InitializeOncePerProcessInternal(
          std::vector<std::string>(argv, argv + argc));
  for (const std::string& error : result->errors()) {
    FPrintF(stderr, "%s: %s\n", result->args().at(0), error);
  }
  if (result->early_return()) {
    return result->exit_code_enum();
  }

  DCHECK_EQ(result->exit_code_enum(), ExitCode::kNoFailure);
  const SnapshotData* snapshot_data = nullptr;

  auto cleanup_process = OnScopeLeave([&]() {
    TearDownOncePerProcess();

    if (snapshot_data != nullptr &&
        snapshot_data->data_ownership == SnapshotData::DataOwnership::kOwned) {
      delete snapshot_data;
    }
  });

  uv_loop_configure(uv_default_loop(), UV_METRICS_IDLE_TIME);
  std::string sea_config = per_process::cli_options->experimental_sea_config;
  if (!sea_config.empty()) {
#if !defined(DISABLE_SINGLE_EXECUTABLE_APPLICATION)
    return sea::BuildSingleExecutableBlob(
        sea_config, result->args(), result->exec_args());
#else
    fprintf(stderr, "Single executable application is disabled.\n");
    return ExitCode::kGenericUserError;
#endif  // !defined(DISABLE_SINGLE_EXECUTABLE_APPLICATION)
  }
  // --build-snapshot indicates that we are in snapshot building mode.
  if (per_process::cli_options->per_isolate->build_snapshot) {
    if (per_process::cli_options->per_isolate->build_snapshot_config.empty() &&
        result->args().size() < 2) {
      fprintf(stderr,
              "--build-snapshot must be used with an entry point script.\n"
              "Usage: node --build-snapshot /path/to/entry.js\n");
      return ExitCode::kInvalidCommandLineArgument;
    }
    return GenerateAndWriteSnapshotData(&snapshot_data, result.get());
  }

  // Without --build-snapshot, we are in snapshot loading mode.
  if (!LoadSnapshotData(&snapshot_data)) {
    return ExitCode::kStartupSnapshotFailure;
  }
  NodeMainInstance main_instance(snapshot_data,
                                 uv_default_loop(),
                                 per_process::v8_platform.Platform(),
                                 result->args(),
                                 result->exec_args());
  return main_instance.Run();
}

int Start(int argc, char** argv) {
#ifndef DISABLE_SINGLE_EXECUTABLE_APPLICATION
  std::tie(argc, argv) = sea::FixupArgsForSEA(argc, argv);
#endif
  return static_cast<int>(StartInternal(argc, argv));
}

int Stop(Environment* env, StopFlags::Flags flags) {
  env->ExitEnv(flags);
  return 0;
}

}  // namespace node

#if !HAVE_INSPECTOR
void Initialize() {}

NODE_BINDING_CONTEXT_AWARE_INTERNAL(inspector, Initialize)
#endif  // !HAVE_INSPECTOR
