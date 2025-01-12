#include "node_options.h"  // NOLINT(build/include_inline)
#include "node_options-inl.h"

#include "env-inl.h"
#include "node_binding.h"
#include "node_external_reference.h"
#include "node_internals.h"
#include "node_sea.h"
#if HAVE_OPENSSL
#include "openssl/opensslv.h"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <sstream>
#include <string_view>

using v8::Boolean;
using v8::Context;
using v8::FunctionCallbackInfo;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::LocalVector;
using v8::Map;
using v8::Name;
using v8::Null;
using v8::Number;
using v8::Object;
using v8::Undefined;
using v8::Value;
namespace node {

namespace per_process {
Mutex cli_options_mutex;
std::shared_ptr<PerProcessOptions> cli_options{new PerProcessOptions()};
}  // namespace per_process

void DebugOptions::CheckOptions(std::vector<std::string>* errors,
                                std::vector<std::string>* argv) {
#if !NODE_USE_V8_PLATFORM && !HAVE_INSPECTOR
  if (inspector_enabled) {
    errors->push_back("Inspector is not available when Node is compiled "
                      "--without-v8-platform and --without-inspector.");
  }
#endif

  if (deprecated_debug) {
    errors->push_back("[DEP0062]: `node --debug` and `node --debug-brk` "
                      "are invalid. Please use `node --inspect` and "
                      "`node --inspect-brk` instead.");
  }

  using std::string_view_literals::operator""sv;
  const std::vector<std::string_view> destinations =
      SplitString(inspect_publish_uid_string, ","sv);
  inspect_publish_uid.console = false;
  inspect_publish_uid.http = false;
  for (const std::string_view destination : destinations) {
    if (destination == "stderr"sv) {
      inspect_publish_uid.console = true;
    } else if (destination == "http"sv) {
      inspect_publish_uid.http = true;
    } else {
      errors->push_back("--inspect-publish-uid destination can be "
                        "stderr or http");
    }
  }
}

void PerProcessOptions::CheckOptions(std::vector<std::string>* errors,
                                     std::vector<std::string>* argv) {
#if HAVE_OPENSSL
  if (use_openssl_ca && use_bundled_ca) {
    errors->push_back("either --use-openssl-ca or --use-bundled-ca can be "
                      "used, not both");
  }

  // Any value less than 2 disables use of the secure heap.
  if (secure_heap >= 2) {
    if ((secure_heap & (secure_heap - 1)) != 0)
      errors->push_back("--secure-heap must be a power of 2");
    secure_heap_min =
        std::min({
            secure_heap,
            secure_heap_min,
            static_cast<int64_t>(std::numeric_limits<int>::max())});
    secure_heap_min = std::max(static_cast<int64_t>(2), secure_heap_min);
    if ((secure_heap_min & (secure_heap_min - 1)) != 0)
      errors->push_back("--secure-heap-min must be a power of 2");
  }
#endif  // HAVE_OPENSSL

  if (use_largepages != "off" &&
      use_largepages != "on" &&
      use_largepages != "silent") {
    errors->push_back("invalid value for --use-largepages");
  }
  per_isolate->CheckOptions(errors, argv);
}

void PerIsolateOptions::CheckOptions(std::vector<std::string>* errors,
                                     std::vector<std::string>* argv) {
  per_env->CheckOptions(errors, argv);
}

void EnvironmentOptions::CheckOptions(std::vector<std::string>* errors,
                                      std::vector<std::string>* argv) {
  if (!input_type.empty()) {
    if (input_type != "commonjs" && input_type != "module" &&
        input_type != "commonjs-typescript" &&
        input_type != "module-typescript") {
      errors->push_back(
          "--input-type must be \"module\","
          "\"commonjs\", \"module-typescript\" or \"commonjs-typescript\"");
    }
  }

  if (syntax_check_only && has_eval_string) {
    errors->push_back("either --check or --eval can be used, not both");
  }

  if (!unhandled_rejections.empty() &&
      unhandled_rejections != "warn-with-error-code" &&
      unhandled_rejections != "throw" &&
      unhandled_rejections != "strict" &&
      unhandled_rejections != "warn" &&
      unhandled_rejections != "none") {
    errors->push_back("invalid value for --unhandled-rejections");
  }

  if (tls_min_v1_3 && tls_max_v1_2) {
    errors->push_back("either --tls-min-v1.3 or --tls-max-v1.2 can be "
                      "used, not both");
  }

  if (heap_snapshot_near_heap_limit < 0) {
    errors->push_back("--heapsnapshot-near-heap-limit must not be negative");
  }

  if (!trace_require_module.empty() && trace_require_module != "all" &&
      trace_require_module != "no-node-modules") {
    errors->push_back("invalid value for --trace-require-module");
  }

  if (test_runner) {
    if (test_isolation == "none") {
      debug_options_.allow_attaching_debugger = true;
    } else {
      if (test_isolation != "process") {
        errors->push_back("invalid value for --test-isolation");
      }

#ifndef ALLOW_ATTACHING_DEBUGGER_IN_TEST_RUNNER
      debug_options_.allow_attaching_debugger = false;
#endif
    }

    if (syntax_check_only) {
      errors->push_back("either --test or --check can be used, not both");
    }

    if (has_eval_string) {
      errors->push_back("either --test or --eval can be used, not both");
    }

    if (force_repl) {
      errors->push_back("either --test or --interactive can be used, not both");
    }

    if (watch_mode_paths.size() > 0) {
      errors->push_back(
          "--watch-path cannot be used in combination with --test");
    }
  }

  if (watch_mode) {
    if (syntax_check_only) {
      errors->push_back("either --watch or --check can be used, not both");
    } else if (has_eval_string) {
      errors->push_back("either --watch or --eval can be used, not both");
    } else if (force_repl) {
      errors->push_back("either --watch or --interactive "
                        "can be used, not both");
    } else if (test_runner_force_exit) {
      errors->push_back("either --watch or --test-force-exit "
                        "can be used, not both");
    } else if (!test_runner && (argv->size() < 1 || (*argv)[1].empty())) {
      errors->push_back("--watch requires specifying a file");
    }

#ifndef ALLOW_ATTACHING_DEBUGGER_IN_WATCH_MODE
    debug_options_.allow_attaching_debugger = false;
#endif
  }

#if HAVE_INSPECTOR
  if (!cpu_prof) {
    if (!cpu_prof_name.empty()) {
      errors->push_back("--cpu-prof-name must be used with --cpu-prof");
    }
    if (!cpu_prof_dir.empty()) {
      errors->push_back("--cpu-prof-dir must be used with --cpu-prof");
    }
    // We can't catch the case where the value passed is the default value,
    // then the option just becomes a noop which is fine.
    if (cpu_prof_interval != kDefaultCpuProfInterval) {
      errors->push_back("--cpu-prof-interval must be used with --cpu-prof");
    }
  }

  if (cpu_prof && cpu_prof_dir.empty() && !diagnostic_dir.empty()) {
      cpu_prof_dir = diagnostic_dir;
    }

  if (!heap_prof) {
    if (!heap_prof_name.empty()) {
      errors->push_back("--heap-prof-name must be used with --heap-prof");
    }
    if (!heap_prof_dir.empty()) {
      errors->push_back("--heap-prof-dir must be used with --heap-prof");
    }
    // We can't catch the case where the value passed is the default value,
    // then the option just becomes a noop which is fine.
    if (heap_prof_interval != kDefaultHeapProfInterval) {
      errors->push_back("--heap-prof-interval must be used with --heap-prof");
    }
  }

  if (heap_prof && heap_prof_dir.empty() && !diagnostic_dir.empty()) {
    heap_prof_dir = diagnostic_dir;
  }

  debug_options_.CheckOptions(errors, argv);
#endif  // HAVE_INSPECTOR
}

namespace options_parser {

class DebugOptionsParser : public OptionsParser<DebugOptions> {
 public:
  DebugOptionsParser();
};

class EnvironmentOptionsParser : public OptionsParser<EnvironmentOptions> {
 public:
  EnvironmentOptionsParser();
  explicit EnvironmentOptionsParser(const DebugOptionsParser& dop)
    : EnvironmentOptionsParser() {
    Insert(dop, &EnvironmentOptions::get_debug_options);
  }
};

class PerIsolateOptionsParser : public OptionsParser<PerIsolateOptions> {
 public:
  PerIsolateOptionsParser() = delete;
  explicit PerIsolateOptionsParser(const EnvironmentOptionsParser& eop);
};

class PerProcessOptionsParser : public OptionsParser<PerProcessOptions> {
 public:
  PerProcessOptionsParser() = delete;
  explicit PerProcessOptionsParser(const PerIsolateOptionsParser& iop);
};

#if HAVE_INSPECTOR
const DebugOptionsParser _dop_instance{};
const EnvironmentOptionsParser _eop_instance{_dop_instance};

// This Parse is not dead code. It is used by embedders (e.g., Electron).
template <>
void Parse(
  StringVector* const args, StringVector* const exec_args,
  StringVector* const v8_args,
  DebugOptions* const options,
  OptionEnvvarSettings required_env_settings, StringVector* const errors) {
  _dop_instance.Parse(
    args, exec_args, v8_args, options, required_env_settings, errors);
}
#else
const EnvironmentOptionsParser _eop_instance{};
#endif  // HAVE_INSPECTOR
const PerIsolateOptionsParser _piop_instance{_eop_instance};
const PerProcessOptionsParser _ppop_instance{_piop_instance};

template <>
void Parse(
  StringVector* const args, StringVector* const exec_args,
  StringVector* const v8_args,
  PerIsolateOptions* const options,
  OptionEnvvarSettings required_env_settings, StringVector* const errors) {
  _piop_instance.Parse(
    args, exec_args, v8_args, options, required_env_settings, errors);
}

template <>
void Parse(
  StringVector* const args, StringVector* const exec_args,
  StringVector* const v8_args,
  PerProcessOptions* const options,
  OptionEnvvarSettings required_env_settings, StringVector* const errors) {
  _ppop_instance.Parse(
    args, exec_args, v8_args, options, required_env_settings, errors);
}

// XXX: If you add an option here, please also add it to doc/node.1 and
// doc/api/cli.md
// TODO(addaleax): Make that unnecessary.

DebugOptionsParser::DebugOptionsParser() {
#ifndef DISABLE_SINGLE_EXECUTABLE_APPLICATION
  if (sea::IsSingleExecutable()) return;
#endif

  AddOption("--inspect-port",
            "set host:port for inspector",
            &DebugOptions::host_port,
            kAllowedInEnvvar);
  AddAlias("--debug-port", "--inspect-port");

  AddOption("--inspect",
            "activate inspector on host:port (default: 127.0.0.1:9229)",
            &DebugOptions::inspector_enabled,
            kAllowedInEnvvar);
  AddAlias("--inspect=", { "--inspect-port", "--inspect" });

  AddOption("--debug", "", &DebugOptions::deprecated_debug);
  AddAlias("--debug=", "--debug");
  AddOption("--debug-brk", "", &DebugOptions::deprecated_debug);
  AddAlias("--debug-brk=", "--debug-brk");

  AddOption("--inspect-brk",
            "activate inspector on host:port and break at start of user script",
            &DebugOptions::break_first_line,
            kAllowedInEnvvar);
  Implies("--inspect-brk", "--inspect");
  AddAlias("--inspect-brk=", { "--inspect-port", "--inspect-brk" });

  AddOption("--inspect-brk-node", "", &DebugOptions::break_node_first_line);
  Implies("--inspect-brk-node", "--inspect");
  AddAlias("--inspect-brk-node=", { "--inspect-port", "--inspect-brk-node" });

  AddOption(
      "--inspect-wait",
      "activate inspector on host:port and wait for debugger to be attached",
      &DebugOptions::inspect_wait,
      kAllowedInEnvvar);
  Implies("--inspect-wait", "--inspect");
  AddAlias("--inspect-wait=", {"--inspect-port", "--inspect-wait"});

  AddOption("--inspect-publish-uid",
            "comma separated list of destinations for inspector uid"
            "(default: stderr,http)",
            &DebugOptions::inspect_publish_uid_string,
            kAllowedInEnvvar);
}

EnvironmentOptionsParser::EnvironmentOptionsParser() {
  AddOption("--conditions",
            "additional user conditions for conditional exports and imports",
            &EnvironmentOptions::conditions,
            kAllowedInEnvvar);
  AddAlias("-C", "--conditions");
  AddOption("--experimental-detect-module",
            "when ambiguous modules fail to evaluate because they contain "
            "ES module syntax, try again to evaluate them as ES modules",
            &EnvironmentOptions::detect_module,
            kAllowedInEnvvar,
            true);
  AddOption("--experimental-print-required-tla",
            "Print pending top-level await. If --experimental-require-module "
            "is true, evaluate asynchronous graphs loaded by `require()` but "
            "do not run the microtasks, in order to to find and print "
            "top-level await in the graph",
            &EnvironmentOptions::print_required_tla,
            kAllowedInEnvvar);
  AddOption("--experimental-require-module",
            "Allow loading synchronous ES Modules in require().",
            &EnvironmentOptions::require_module,
            kAllowedInEnvvar,
            true);
  AddOption("--diagnostic-dir",
            "set dir for all output files"
            " (default: current working directory)",
            &EnvironmentOptions::diagnostic_dir,
            kAllowedInEnvvar);
  AddOption("--disable-sigusr1",
            "Disable inspector thread to be listening for SIGUSR1 signal",
            &EnvironmentOptions::disable_sigusr1,
            kAllowedInEnvvar,
            false);
  AddOption("--dns-result-order",
            "set default value of verbatim in dns.lookup. Options are "
            "'ipv4first' (IPv4 addresses are placed before IPv6 addresses) "
            "'ipv6first' (IPv6 addresses are placed before IPv4 addresses) "
            "'verbatim' (addresses are in the order the DNS resolver "
            "returned)",
            &EnvironmentOptions::dns_result_order,
            kAllowedInEnvvar);
  AddOption("--network-family-autoselection",
            "Disable network address family autodetection algorithm",
            &EnvironmentOptions::network_family_autoselection,
            kAllowedInEnvvar,
            true);
  AddOption("--network-family-autoselection-attempt-timeout",
            "Sets the default value for the network family autoselection "
            "attempt timeout.",
            &EnvironmentOptions::network_family_autoselection_attempt_timeout,
            kAllowedInEnvvar);
  AddAlias("--enable-network-family-autoselection",
           "--network-family-autoselection");
  AddOption("--enable-source-maps",
            "Source Map V3 support for stack traces",
            &EnvironmentOptions::enable_source_maps,
            kAllowedInEnvvar);
  AddOption("--entry-url",
            "Treat the entrypoint as a URL",
            &EnvironmentOptions::entry_is_url,
            kAllowedInEnvvar);
  AddOption("--experimental-addon-modules",
            "experimental import support for addons",
            &EnvironmentOptions::experimental_addon_modules,
            kAllowedInEnvvar);
  AddOption("--experimental-abortcontroller", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--experimental-eventsource",
            "experimental EventSource API",
            &EnvironmentOptions::experimental_eventsource,
            kAllowedInEnvvar,
            false);
  AddOption("--experimental-fetch", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--experimental-websocket",
            "experimental WebSocket API",
            &EnvironmentOptions::experimental_websocket,
            kAllowedInEnvvar,
            true);
  AddOption("--experimental-global-customevent", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--experimental-sqlite",
            "experimental node:sqlite module",
            &EnvironmentOptions::experimental_sqlite,
            kAllowedInEnvvar,
            true);
  AddOption("--experimental-quic",
            "experimental QUIC API",
            &EnvironmentOptions::experimental_quic,
            kAllowedInEnvvar);
  AddOption("--experimental-webstorage",
            "experimental Web Storage API",
            &EnvironmentOptions::experimental_webstorage,
            kAllowedInEnvvar);
  AddOption("--localstorage-file",
            "file used to persist localStorage data",
            &EnvironmentOptions::localstorage_file,
            kAllowedInEnvvar);
  AddOption("--experimental-global-navigator",
            "expose experimental Navigator API on the global scope",
            &EnvironmentOptions::experimental_global_navigator,
            kAllowedInEnvvar,
            true);
  AddOption("--experimental-global-webcrypto", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--experimental-json-modules", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--experimental-loader",
            "use the specified module as a custom loader",
            &EnvironmentOptions::userland_loaders,
            kAllowedInEnvvar);
  AddAlias("--loader", "--experimental-loader");
  AddOption("--experimental-modules", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--experimental-wasm-modules",
            "experimental ES Module support for webassembly modules",
            &EnvironmentOptions::experimental_wasm_modules,
            kAllowedInEnvvar);
  AddOption("--experimental-import-meta-resolve",
            "experimental ES Module import.meta.resolve() parentURL support",
            &EnvironmentOptions::experimental_import_meta_resolve,
            kAllowedInEnvvar);
  AddOption("--permission",
            "enable the permission system",
            &EnvironmentOptions::permission,
            kAllowedInEnvvar,
            false);
  AddOption("--allow-fs-read",
            "allow permissions to read the filesystem",
            &EnvironmentOptions::allow_fs_read,
            kAllowedInEnvvar);
  AddOption("--allow-fs-write",
            "allow permissions to write in the filesystem",
            &EnvironmentOptions::allow_fs_write,
            kAllowedInEnvvar);
  AddOption("--allow-addons",
            "allow use of addons when any permissions are set",
            &EnvironmentOptions::allow_addons,
            kAllowedInEnvvar);
  AddOption("--allow-child-process",
            "allow use of child process when any permissions are set",
            &EnvironmentOptions::allow_child_process,
            kAllowedInEnvvar);
  AddOption("--allow-wasi",
            "allow wasi when any permissions are set",
            &EnvironmentOptions::allow_wasi,
            kAllowedInEnvvar);
  AddOption("--allow-worker",
            "allow worker threads when any permissions are set",
            &EnvironmentOptions::allow_worker_threads,
            kAllowedInEnvvar);
  AddOption("--experimental-repl-await",
            "experimental await keyword support in REPL",
            &EnvironmentOptions::experimental_repl_await,
            kAllowedInEnvvar,
            true);
  AddOption("--experimental-vm-modules",
            "experimental ES Module support in vm module",
            &EnvironmentOptions::experimental_vm_modules,
            kAllowedInEnvvar);
  AddOption("--experimental-worker", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--experimental-report", "", NoOp{}, kAllowedInEnvvar);
  AddOption(
      "--experimental-wasi-unstable-preview1", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--expose-gc", "expose gc extension", V8Option{}, kAllowedInEnvvar);
  AddOption("--async-context-frame",
            "Improve AsyncLocalStorage performance with AsyncContextFrame",
            &EnvironmentOptions::async_context_frame,
            kAllowedInEnvvar,
            true);
  AddOption("--expose-internals", "", &EnvironmentOptions::expose_internals);
  AddOption("--frozen-intrinsics",
            "experimental frozen intrinsics support",
            &EnvironmentOptions::frozen_intrinsics,
            kAllowedInEnvvar);
  AddOption("--heapsnapshot-signal",
            "Generate heap snapshot on specified signal",
            &EnvironmentOptions::heap_snapshot_signal,
            kAllowedInEnvvar);
  AddOption("--heapsnapshot-near-heap-limit",
            "Generate heap snapshots whenever V8 is approaching "
            "the heap limit. No more than the specified number of "
            "heap snapshots will be generated.",
            &EnvironmentOptions::heap_snapshot_near_heap_limit,
            kAllowedInEnvvar);
  AddOption("--http-parser", "", NoOp{}, kAllowedInEnvvar);
  AddOption("--insecure-http-parser",
            "use an insecure HTTP parser that accepts invalid HTTP headers",
            &EnvironmentOptions::insecure_http_parser,
            kAllowedInEnvvar);
  AddOption("--input-type",
            "set module type for string input",
            &EnvironmentOptions::input_type,
            kAllowedInEnvvar);
  AddOption(
      "--experimental-specifier-resolution", "", NoOp{}, kAllowedInEnvvar);
  AddAlias("--es-module-specifier-resolution",
           "--experimental-specifier-resolution");
  AddOption("--deprecation",
            "silence deprecation warnings",
            &EnvironmentOptions::deprecation,
            kAllowedInEnvvar,
            true);
  AddOption("--force-async-hooks-checks",
            "disable checks for async_hooks",
            &EnvironmentOptions::force_async_hooks_checks,
            kAllowedInEnvvar,
            true);
  AddOption(
      "--force-node-api-uncaught-exceptions-policy",
      "enforces 'uncaughtException' event on Node API asynchronous callbacks",
      &EnvironmentOptions::force_node_api_uncaught_exceptions_policy,
      kAllowedInEnvvar,
      false);
  AddOption("--addons",
            "disable loading native addons",
            &EnvironmentOptions::allow_native_addons,
            kAllowedInEnvvar,
            true);
  AddOption("--global-search-paths",
            "disable global module search paths",
            &EnvironmentOptions::global_search_paths,
            kAllowedInEnvvar,
            true);
  AddOption("--warnings",
            "silence all process warnings",
            &EnvironmentOptions::warnings,
            kAllowedInEnvvar,
            true);
  AddOption("--disable-warning",
            "silence specific process warnings",
            &EnvironmentOptions::disable_warnings,
            kAllowedInEnvvar);
  AddOption("--force-context-aware",
            "disable loading non-context-aware addons",
            &EnvironmentOptions::force_context_aware,
            kAllowedInEnvvar);
  AddOption("--pending-deprecation",
            "emit pending deprecation warnings",
            &EnvironmentOptions::pending_deprecation,
            kAllowedInEnvvar);
  AddOption("--preserve-symlinks",
            "preserve symbolic links when resolving",
            &EnvironmentOptions::preserve_symlinks,
            kAllowedInEnvvar);
  AddOption("--preserve-symlinks-main",
            "preserve symbolic links when resolving the main module",
            &EnvironmentOptions::preserve_symlinks_main,
            kAllowedInEnvvar);
  AddOption("--prof",
            "Generate V8 profiler output.",
            V8Option{});
  AddOption("--prof-process",
            "process V8 profiler output generated using --prof",
            &EnvironmentOptions::prof_process);
  // Options after --prof-process are passed through to the prof processor.
  AddAlias("--prof-process", {"--prof-process", "--"});
#if HAVE_INSPECTOR
  AddOption("--cpu-prof",
            "Start the V8 CPU profiler on start up, and write the CPU profile "
            "to disk before exit. If --cpu-prof-dir is not specified, write "
            "the profile to the current working directory.",
            &EnvironmentOptions::cpu_prof);
  AddOption("--cpu-prof-name",
            "specified file name of the V8 CPU profile generated with "
            "--cpu-prof",
            &EnvironmentOptions::cpu_prof_name);
  AddOption("--cpu-prof-interval",
            "specified sampling interval in microseconds for the V8 CPU "
            "profile generated with --cpu-prof. (default: 1000)",
            &EnvironmentOptions::cpu_prof_interval);
  AddOption("--cpu-prof-dir",
            "Directory where the V8 profiles generated by --cpu-prof will be "
            "placed. Does not affect --prof.",
            &EnvironmentOptions::cpu_prof_dir);
  AddOption("--experimental-network-inspection",
            "experimental network inspection support",
            &EnvironmentOptions::experimental_network_inspection);
  AddOption(
      "--heap-prof",
      "Start the V8 heap profiler on start up, and write the heap profile "
      "to disk before exit. If --heap-prof-dir is not specified, write "
      "the profile to the current working directory.",
      &EnvironmentOptions::heap_prof,
      kAllowedInEnvvar);
  AddOption("--heap-prof-name",
            "specified file name of the V8 heap profile generated with "
            "--heap-prof",
            &EnvironmentOptions::heap_prof_name,
            kAllowedInEnvvar);
  AddOption("--heap-prof-dir",
            "Directory where the V8 heap profiles generated by --heap-prof "
            "will be placed.",
            &EnvironmentOptions::heap_prof_dir,
            kAllowedInEnvvar);
  AddOption("--heap-prof-interval",
            "specified sampling interval in bytes for the V8 heap "
            "profile generated with --heap-prof. (default: 512 * 1024)",
            &EnvironmentOptions::heap_prof_interval,
            kAllowedInEnvvar);
#endif  // HAVE_INSPECTOR
  AddOption("--max-http-header-size",
            "set the maximum size of HTTP headers (default: 16384 (16KB))",
            &EnvironmentOptions::max_http_header_size,
            kAllowedInEnvvar);
  AddOption("--redirect-warnings",
            "write warnings to file instead of stderr",
            &EnvironmentOptions::redirect_warnings,
            kAllowedInEnvvar);
  AddOption(
      "[has_env_file_string]", "", &EnvironmentOptions::has_env_file_string);
  AddOption("--env-file",
            "set environment variables from supplied file",
            &EnvironmentOptions::env_file);
  Implies("--env-file", "[has_env_file_string]");
  AddOption("--env-file-if-exists",
            "set environment variables from supplied file",
            &EnvironmentOptions::optional_env_file);
  Implies("--env-file-if-exists", "[has_env_file_string]");
  AddOption("--test",
            "launch test runner on startup",
            &EnvironmentOptions::test_runner);
  AddOption("--test-concurrency",
            "specify test runner concurrency",
            &EnvironmentOptions::test_runner_concurrency);
  AddOption("--test-force-exit",
            "force test runner to exit upon completion",
            &EnvironmentOptions::test_runner_force_exit);
  AddOption("--test-timeout",
            "specify test runner timeout",
            &EnvironmentOptions::test_runner_timeout);
  AddOption("--test-update-snapshots",
            "regenerate test snapshots",
            &EnvironmentOptions::test_runner_update_snapshots);
  AddOption("--experimental-test-coverage",
            "enable code coverage in the test runner",
            &EnvironmentOptions::test_runner_coverage);
  AddOption("--test-coverage-branches",
            "the branch coverage minimum threshold",
            &EnvironmentOptions::test_coverage_branches,
            kAllowedInEnvvar);
  AddOption("--test-coverage-functions",
            "the function coverage minimum threshold",
            &EnvironmentOptions::test_coverage_functions,
            kAllowedInEnvvar);
  AddOption("--test-coverage-lines",
            "the line coverage minimum threshold",
            &EnvironmentOptions::test_coverage_lines,
            kAllowedInEnvvar);
  AddOption("--test-isolation",
            "configures the type of test isolation used in the test runner",
            &EnvironmentOptions::test_isolation,
            kAllowedInEnvvar);
  // TODO(cjihrig): Remove this alias in a semver major.
  AddAlias("--experimental-test-isolation", "--test-isolation");
  AddOption("--experimental-test-module-mocks",
            "enable module mocking in the test runner",
            &EnvironmentOptions::test_runner_module_mocks);
  AddOption("--experimental-test-snapshots", "", NoOp{});
  AddOption("--test-name-pattern",
            "run tests whose name matches this regular expression",
            &EnvironmentOptions::test_name_pattern,
            kAllowedInEnvvar);
  AddOption("--test-reporter",
            "report test output using the given reporter",
            &EnvironmentOptions::test_reporter,
            kAllowedInEnvvar);
  AddOption("--test-reporter-destination",
            "report given reporter to the given destination",
            &EnvironmentOptions::test_reporter_destination,
            kAllowedInEnvvar);
  AddOption("--test-only",
            "run tests with 'only' option set",
            &EnvironmentOptions::test_only,
            kAllowedInEnvvar);
  AddOption("--test-shard",
            "run test at specific shard",
            &EnvironmentOptions::test_shard,
            kAllowedInEnvvar);
  AddOption("--test-skip-pattern",
            "run tests whose name do not match this regular expression",
            &EnvironmentOptions::test_skip_pattern,
            kAllowedInEnvvar);
  AddOption("--test-coverage-include",
            "include files in coverage report that match this glob pattern",
            &EnvironmentOptions::coverage_include_pattern,
            kAllowedInEnvvar);
  AddOption("--test-coverage-exclude",
            "exclude files from coverage report that match this glob pattern",
            &EnvironmentOptions::coverage_exclude_pattern,
            kAllowedInEnvvar);
  AddOption("--test-udp-no-try-send", "",  // For testing only.
            &EnvironmentOptions::test_udp_no_try_send);
  AddOption("--throw-deprecation",
            "throw an exception on deprecations",
            &EnvironmentOptions::throw_deprecation,
            kAllowedInEnvvar);
  AddOption("--trace-deprecation",
            "show stack traces on deprecations",
            &EnvironmentOptions::trace_deprecation,
            kAllowedInEnvvar);
  AddOption("--trace-exit",
            "show stack trace when an environment exits",
            &EnvironmentOptions::trace_exit,
            kAllowedInEnvvar);
  AddOption("--trace-sync-io",
            "show stack trace when use of sync IO is detected after the "
            "first tick",
            &EnvironmentOptions::trace_sync_io,
            kAllowedInEnvvar);
  AddOption("--trace-tls",
            "prints TLS packet trace information to stderr",
            &EnvironmentOptions::trace_tls,
            kAllowedInEnvvar);
  AddOption("--trace-uncaught",
            "show stack traces for the `throw` behind uncaught exceptions",
            &EnvironmentOptions::trace_uncaught,
            kAllowedInEnvvar);
  AddOption("--trace-warnings",
            "show stack traces on process warnings",
            &EnvironmentOptions::trace_warnings,
            kAllowedInEnvvar);
  AddOption("--trace-promises",
            "show stack traces on promise initialization and resolution",
            &EnvironmentOptions::trace_promises,
            kAllowedInEnvvar);

  AddOption("--trace-env",
            "Print accesses to the environment variables",
            &EnvironmentOptions::trace_env,
            kAllowedInEnvvar);
  Implies("--trace-env-js-stack", "--trace-env");
  Implies("--trace-env-native-stack", "--trace-env");
  AddOption("--trace-env-js-stack",
            "Print accesses to the environment variables and the JavaScript "
            "stack trace",
            &EnvironmentOptions::trace_env_js_stack,
            kAllowedInEnvvar);
  AddOption(
      "--trace-env-native-stack",
      "Print accesses to the environment variables and the native stack trace",
      &EnvironmentOptions::trace_env_native_stack,
      kAllowedInEnvvar);

  AddOption(
      "--trace-require-module",
      "Print access to require(esm). Options are 'all' (print all usage) and "
      "'no-node-modules' (excluding usage from the node_modules folder)",
      &EnvironmentOptions::trace_require_module,
      kAllowedInEnvvar);

  AddOption("--extra-info-on-fatal-exception",
            "hide extra information on fatal exception that causes exit",
            &EnvironmentOptions::extra_info_on_fatal_exception,
            kAllowedInEnvvar,
            true);
  AddOption("--unhandled-rejections",
            "define unhandled rejections behavior. Options are 'strict' "
            "(always raise an error), 'throw' (raise an error unless "
            "'unhandledRejection' hook is set), 'warn' (log a warning), 'none' "
            "(silence warnings), 'warn-with-error-code' (log a warning and set "
            "exit code 1 unless 'unhandledRejection' hook is set). (default: "
            "throw)",
            &EnvironmentOptions::unhandled_rejections,
            kAllowedInEnvvar);
  AddOption("--verify-base-objects",
            "", /* undocumented, only for debugging */
            &EnvironmentOptions::verify_base_objects,
            kAllowedInEnvvar);
  AddOption("--watch",
            "run in watch mode",
            &EnvironmentOptions::watch_mode,
            kAllowedInEnvvar);
  AddOption("--watch-path",
            "path to watch",
            &EnvironmentOptions::watch_mode_paths,
            kAllowedInEnvvar);
  AddOption("--watch-preserve-output",
            "preserve outputs on watch mode restart",
            &EnvironmentOptions::watch_mode_preserve_output,
            kAllowedInEnvvar);
  Implies("--watch-path", "--watch");
  AddOption("--check",
            "syntax check script without executing",
            &EnvironmentOptions::syntax_check_only);
  AddAlias("-c", "--check");
  // This option is only so that we can tell --eval with an empty string from
  // no eval at all. Having it not start with a dash makes it inaccessible
  // from the parser itself, but available for using Implies().
  // TODO(addaleax): When moving --help over to something generated from the
  // programmatic descriptions, this will need some special care.
  // (See also [ssl_openssl_cert_store] below.)
  AddOption("[has_eval_string]", "", &EnvironmentOptions::has_eval_string);
  AddOption("--eval", "evaluate script", &EnvironmentOptions::eval_string);
  Implies("--eval", "[has_eval_string]");
  AddOption("--print",
            "evaluate script and print result",
            &EnvironmentOptions::print_eval);
  AddAlias("-e", "--eval");
  AddAlias("--print <arg>", "-pe");
  AddAlias("-pe", { "--print", "--eval" });
  AddAlias("-p", "--print");
  AddOption("--require",
            "CommonJS module to preload (option can be repeated)",
            &EnvironmentOptions::preload_cjs_modules,
            kAllowedInEnvvar);
  AddAlias("-r", "--require");
  AddOption("--import",
            "ES module to preload (option can be repeated)",
            &EnvironmentOptions::preload_esm_modules,
            kAllowedInEnvvar);
  AddOption("--experimental-strip-types",
            "Experimental type-stripping for TypeScript files.",
            &EnvironmentOptions::experimental_strip_types,
            kAllowedInEnvvar,
            true);
  AddOption("--experimental-transform-types",
            "enable transformation of TypeScript-only"
            "syntax into JavaScript code",
            &EnvironmentOptions::experimental_transform_types,
            kAllowedInEnvvar);
  Implies("--experimental-transform-types", "--experimental-strip-types");
  Implies("--experimental-transform-types", "--enable-source-maps");
  AddOption("--interactive",
            "always enter the REPL even if stdin does not appear "
            "to be a terminal",
            &EnvironmentOptions::force_repl);
  AddAlias("-i", "--interactive");

  AddOption("--napi-modules", "", NoOp{}, kAllowedInEnvvar);

  AddOption("--tls-keylog",
            "log TLS decryption keys to named file for traffic analysis",
            &EnvironmentOptions::tls_keylog,
            kAllowedInEnvvar);

  AddOption("--tls-min-v1.0",
            "set default TLS minimum to TLSv1.0 (default: TLSv1.2)",
            &EnvironmentOptions::tls_min_v1_0,
            kAllowedInEnvvar);
  AddOption("--tls-min-v1.1",
            "set default TLS minimum to TLSv1.1 (default: TLSv1.2)",
            &EnvironmentOptions::tls_min_v1_1,
            kAllowedInEnvvar);
  AddOption("--tls-min-v1.2",
            "set default TLS minimum to TLSv1.2 (default: TLSv1.2)",
            &EnvironmentOptions::tls_min_v1_2,
            kAllowedInEnvvar);
  AddOption("--tls-min-v1.3",
            "set default TLS minimum to TLSv1.3 (default: TLSv1.2)",
            &EnvironmentOptions::tls_min_v1_3,
            kAllowedInEnvvar);
  AddOption("--tls-max-v1.2",
            "set default TLS maximum to TLSv1.2 (default: TLSv1.3)",
            &EnvironmentOptions::tls_max_v1_2,
            kAllowedInEnvvar);
  // Current plan is:
  // - 11.x and below: TLS1.3 is opt-in with --tls-max-v1.3
  // - 12.x: TLS1.3 is opt-out with --tls-max-v1.2
  // In either case, support both options they are uniformly available.
  AddOption("--tls-max-v1.3",
            "set default TLS maximum to TLSv1.3 (default: TLSv1.3)",
            &EnvironmentOptions::tls_max_v1_3,
            kAllowedInEnvvar);

  AddOption("--report-exclude-env",
            "Exclude environment variables when generating report"
            " (default: false)",
            &EnvironmentOptions::report_exclude_env,
            kAllowedInEnvvar);
  AddOption("--report-exclude-network",
            "exclude network interface diagnostics."
            " (default: false)",
            &EnvironmentOptions::report_exclude_network,
            kAllowedInEnvvar);
}

PerIsolateOptionsParser::PerIsolateOptionsParser(
  const EnvironmentOptionsParser& eop) {
  AddOption("--track-heap-objects",
            "track heap object allocations for heap snapshots",
            &PerIsolateOptions::track_heap_objects,
            kAllowedInEnvvar);

  // Explicitly add some V8 flags to mark them as allowed in NODE_OPTIONS.
  AddOption("--abort-on-uncaught-exception",
            "aborting instead of exiting causes a core file to be generated "
            "for analysis",
            V8Option{},
            kAllowedInEnvvar);
  AddOption("--interpreted-frames-native-stack",
            "help system profilers to translate JavaScript interpreted frames",
            V8Option{},
            kAllowedInEnvvar);
  AddOption("--max-old-space-size", "", V8Option{}, kAllowedInEnvvar);
  AddOption("--max-semi-space-size", "", V8Option{}, kAllowedInEnvvar);
  AddOption("--perf-basic-prof", "", V8Option{}, kAllowedInEnvvar);
  AddOption(
      "--perf-basic-prof-only-functions", "", V8Option{}, kAllowedInEnvvar);
  AddOption("--perf-prof", "", V8Option{}, kAllowedInEnvvar);
  AddOption("--perf-prof-unwinding-info", "", V8Option{}, kAllowedInEnvvar);
  AddOption("--stack-trace-limit",
            "",
            &PerIsolateOptions::stack_trace_limit,
            kAllowedInEnvvar);
  AddOption("--disallow-code-generation-from-strings",
            "disallow eval and friends",
            V8Option{},
            kAllowedInEnvvar);
  AddOption("--jitless",
            "disable runtime allocation of executable memory",
            V8Option{},
            kAllowedInEnvvar);
  AddOption("--report-uncaught-exception",
            "generate diagnostic report on uncaught exceptions",
            &PerIsolateOptions::report_uncaught_exception,
            kAllowedInEnvvar);
  AddOption("--report-on-signal",
            "generate diagnostic report upon receiving signals",
            &PerIsolateOptions::report_on_signal,
            kAllowedInEnvvar);
  AddOption("--report-signal",
            "causes diagnostic report to be produced on provided signal,"
            " unsupported in Windows. (default: SIGUSR2)",
            &PerIsolateOptions::report_signal,
            kAllowedInEnvvar);
  Implies("--report-signal", "--report-on-signal");
  AddOption("--enable-etw-stack-walking",
            "provides heap data to ETW Windows native tracing",
            V8Option{},
            kAllowedInEnvvar);

  AddOption("--experimental-top-level-await", "", NoOp{}, kAllowedInEnvvar);

  AddOption("--experimental-shadow-realm",
            "",
            &PerIsolateOptions::experimental_shadow_realm,
            kAllowedInEnvvar);
  AddOption("--harmony-shadow-realm", "", V8Option{});
  Implies("--experimental-shadow-realm", "--harmony-shadow-realm");
  Implies("--harmony-shadow-realm", "--experimental-shadow-realm");
  ImpliesNot("--no-harmony-shadow-realm", "--experimental-shadow-realm");
  AddOption("--build-snapshot",
            "Generate a snapshot blob when the process exits.",
            &PerIsolateOptions::build_snapshot,
            kDisallowedInEnvvar);
  AddOption("--build-snapshot-config",
            "Generate a snapshot blob when the process exits using a"
            "JSON configuration in the specified path.",
            &PerIsolateOptions::build_snapshot_config,
            kDisallowedInEnvvar);
  Implies("--build-snapshot-config", "--build-snapshot");

  Insert(eop, &PerIsolateOptions::get_per_env_options);
}

PerProcessOptionsParser::PerProcessOptionsParser(
  const PerIsolateOptionsParser& iop) {
  AddOption("--title",
            "the process title to use on startup",
            &PerProcessOptions::title,
            kAllowedInEnvvar);
  AddOption("--trace-event-categories",
            "comma separated list of trace event categories to record",
            &PerProcessOptions::trace_event_categories,
            kAllowedInEnvvar);
  AddOption("--trace-event-file-pattern",
            "Template string specifying the filepath for the trace-events "
            "data, it supports ${rotation} and ${pid}.",
            &PerProcessOptions::trace_event_file_pattern,
            kAllowedInEnvvar);
  AddAlias("--trace-events-enabled", {
    "--trace-event-categories", "v8,node,node.async_hooks" });
  AddOption("--v8-pool-size",
            "set V8's thread pool size",
            &PerProcessOptions::v8_thread_pool_size,
            kAllowedInEnvvar);
  AddOption("--zero-fill-buffers",
            "automatically zero-fill all newly allocated Buffer and "
            "SlowBuffer instances",
            &PerProcessOptions::zero_fill_all_buffers,
            kAllowedInEnvvar);
  AddOption("--debug-arraybuffer-allocations",
            "", /* undocumented, only for debugging */
            &PerProcessOptions::debug_arraybuffer_allocations,
            kAllowedInEnvvar);
  AddOption("--disable-proto",
            "disable Object.prototype.__proto__",
            &PerProcessOptions::disable_proto,
            kAllowedInEnvvar);
  AddOption("--node-snapshot",
            "",  // It's a debug-only option.
            &PerProcessOptions::node_snapshot,
            kAllowedInEnvvar);
  AddOption("--snapshot-blob",
            "Path to the snapshot blob that's either the result of snapshot"
            "building, or the blob that is used to restore the application "
            "state",
            &PerProcessOptions::snapshot_blob,
            kAllowedInEnvvar);

  // 12.x renamed this inadvertently, so alias it for consistency within the
  // release line, while using the original name for consistency with older
  // release lines.
  AddOption("--security-revert", "", &PerProcessOptions::security_reverts);
  AddAlias("--security-reverts", "--security-revert");
  AddOption("--completion-bash",
            "print source-able bash completion script",
            &PerProcessOptions::print_bash_completion);
  AddOption("--help",
            "print node command line options",
            &PerProcessOptions::print_help);
  AddAlias("-h", "--help");
  AddOption(
      "--version", "print Node.js version", &PerProcessOptions::print_version);
  AddAlias("-v", "--version");
  AddOption("--v8-options",
            "print V8 command line options",
            &PerProcessOptions::print_v8_help);
  AddOption("--report-compact",
            "output compact single-line JSON",
            &PerProcessOptions::report_compact,
            kAllowedInEnvvar);
  AddOption("--report-dir",
            "define custom report pathname."
            " (default: current working directory)",
            &PerProcessOptions::report_directory,
            kAllowedInEnvvar);
  AddAlias("--report-directory", "--report-dir");
  AddOption("--report-filename",
            "define custom report file name."
            " (default: YYYYMMDD.HHMMSS.PID.SEQUENCE#.txt)",
            &PerProcessOptions::report_filename,
            kAllowedInEnvvar);
  AddOption("--report-on-fatalerror",
            "generate diagnostic report on fatal (internal) errors",
            &PerProcessOptions::report_on_fatalerror,
            kAllowedInEnvvar);

#ifdef NODE_HAVE_I18N_SUPPORT
  AddOption("--icu-data-dir",
            "set ICU data load path to dir (overrides NODE_ICU_DATA)"
#ifndef NODE_HAVE_SMALL_ICU
            " (note: linked-in ICU data is present)"
#endif
            ,
            &PerProcessOptions::icu_data_dir,
            kAllowedInEnvvar);
#endif

#if HAVE_OPENSSL
  AddOption("--openssl-config",
            "load OpenSSL configuration from the specified file "
            "(overrides OPENSSL_CONF)",
            &PerProcessOptions::openssl_config,
            kAllowedInEnvvar);
  AddOption("--tls-cipher-list",
            "use an alternative default TLS cipher list",
            &PerProcessOptions::tls_cipher_list,
            kAllowedInEnvvar);
  AddOption("--use-openssl-ca",
            "use OpenSSL's default CA store"
#if defined(NODE_OPENSSL_CERT_STORE)
            " (default)"
#endif
            ,
            &PerProcessOptions::use_openssl_ca,
            kAllowedInEnvvar);
  AddOption("--use-bundled-ca",
            "use bundled CA store"
#if !defined(NODE_OPENSSL_CERT_STORE)
            " (default)"
#endif
            ,
            &PerProcessOptions::use_bundled_ca,
            kAllowedInEnvvar);
  // Similar to [has_eval_string] above, except that the separation between
  // this and use_openssl_ca only exists for option validation after parsing.
  // This is not ideal.
  AddOption("[ssl_openssl_cert_store]",
            "",
            &PerProcessOptions::ssl_openssl_cert_store);
  Implies("--use-openssl-ca", "[ssl_openssl_cert_store]");
  ImpliesNot("--use-bundled-ca", "[ssl_openssl_cert_store]");
  AddOption("--enable-fips",
            "enable FIPS crypto at startup",
            &PerProcessOptions::enable_fips_crypto,
            kAllowedInEnvvar);
  AddOption("--force-fips",
            "force FIPS crypto (cannot be disabled)",
            &PerProcessOptions::force_fips_crypto,
            kAllowedInEnvvar);
  AddOption("--secure-heap",
            "total size of the OpenSSL secure heap",
            &PerProcessOptions::secure_heap,
            kAllowedInEnvvar);
  AddOption("--secure-heap-min",
            "minimum allocation size from the OpenSSL secure heap",
            &PerProcessOptions::secure_heap_min,
            kAllowedInEnvvar);
#endif  // HAVE_OPENSSL
#if OPENSSL_VERSION_MAJOR >= 3
  AddOption("--openssl-legacy-provider",
            "enable OpenSSL 3.0 legacy provider",
            &PerProcessOptions::openssl_legacy_provider,
            kAllowedInEnvvar);
  AddOption("--openssl-shared-config",
            "enable OpenSSL shared configuration",
            &PerProcessOptions::openssl_shared_config,
            kAllowedInEnvvar);

#endif  // OPENSSL_VERSION_MAJOR
  AddOption("--use-largepages",
            "Map the Node.js static code to large pages. Options are "
            "'off' (the default value, meaning do not map), "
            "'on' (map and ignore failure, reporting it to stderr), "
            "or 'silent' (map and silently ignore failure)",
            &PerProcessOptions::use_largepages,
            kAllowedInEnvvar);

  AddOption("--trace-sigint",
            "enable printing JavaScript stacktrace on SIGINT",
            &PerProcessOptions::trace_sigint,
            kAllowedInEnvvar);

  Insert(iop, &PerProcessOptions::get_per_isolate_options);

  AddOption("--node-memory-debug",
            "Run with extra debug checks for memory leaks in Node.js itself",
            NoOp{},
            kAllowedInEnvvar);
  Implies("--node-memory-debug", "--debug-arraybuffer-allocations");
  Implies("--node-memory-debug", "--verify-base-objects");

  AddOption("--experimental-sea-config",
            "Generate a blob that can be embedded into the single executable "
            "application",
            &PerProcessOptions::experimental_sea_config);

  AddOption("--run",
            "Run a script specified in package.json",
            &PerProcessOptions::run);
  AddOption(
      "--disable-wasm-trap-handler",
      "Disable trap-handler-based WebAssembly bound checks. V8 will insert "
      "inline bound checks when compiling WebAssembly which may slow down "
      "performance.",
      &PerProcessOptions::disable_wasm_trap_handler,
      kAllowedInEnvvar);
}

inline std::string RemoveBrackets(const std::string& host) {
  if (!host.empty() && host.front() == '[' && host.back() == ']')
    return host.substr(1, host.size() - 2);
  else
    return host;
}

inline uint16_t ParseAndValidatePort(const std::string_view port,
                                     std::vector<std::string>* errors) {
  uint16_t result{};
  auto r = std::from_chars(port.data(), port.data() + port.size(), result);

  if (r.ec == std::errc::result_out_of_range ||
      (result != 0 && result < 1024)) {
    errors->push_back("must be 0 or in range 1024 to 65535.");
  }

  return result;
}

HostPort SplitHostPort(const std::string& arg,
                      std::vector<std::string>* errors) {
  // remove_brackets only works if no port is specified
  // so if it has an effect only an IPv6 address was specified.
  std::string host = RemoveBrackets(arg);
  if (host.length() < arg.length())
    return HostPort{host, DebugOptions::kDefaultInspectorPort};

  size_t colon = arg.rfind(':');
  if (colon == std::string::npos) {
    // Either a port number or a host name.  Assume that
    // if it's not all decimal digits, it's a host name.
    for (char c : arg) {
      if (c < '0' || c > '9') {
        return HostPort{arg, DebugOptions::kDefaultInspectorPort};
      }
    }
    return HostPort { "", ParseAndValidatePort(arg, errors) };
  }
  // Host and port found:
  return HostPort { RemoveBrackets(arg.substr(0, colon)),
                    ParseAndValidatePort(arg.substr(colon + 1), errors) };
}

std::string GetBashCompletion() {
  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  const auto& parser = _ppop_instance;

  std::ostringstream out;

  out << "_node_complete() {\n"
         "  local cur_word options\n"
         "  cur_word=\"${COMP_WORDS[COMP_CWORD]}\"\n"
         "  if [[ \"${cur_word}\" == -* ]] ; then\n"
         "    COMPREPLY=( $(compgen -W '";

  for (const auto& item : parser.options_) {
    if (item.first[0] != '[') {
      out << item.first << " ";
    }
  }
  for (const auto& item : parser.aliases_) {
    if (item.first[0] != '[') {
      out << item.first << " ";
    }
  }
  if (parser.aliases_.size() > 0) {
    out.seekp(-1, out.cur);  // Strip the trailing space
  }

  out << "' -- \"${cur_word}\") )\n"
         "    return 0\n"
         "  else\n"
         "    COMPREPLY=( $(compgen -f \"${cur_word}\") )\n"
         "    return 0\n"
         "  fi\n"
         "}\n"
         "complete -o filenames -o nospace -o bashdefault "
         "-F _node_complete node node_g";
  return out.str();
}

struct IterateCLIOptionsScope {
  explicit IterateCLIOptionsScope(Environment* env) {
    // Temporarily act as if the current Environment's/IsolateData's options
    // were the default options, i.e. like they are the ones we'd access for
    // global options parsing, so that all options are available from the main
    // parser.
    original_per_isolate = per_process::cli_options->per_isolate;
    per_process::cli_options->per_isolate = env->isolate_data()->options();
    original_per_env = per_process::cli_options->per_isolate->per_env;
    per_process::cli_options->per_isolate->per_env = env->options();
  }
  ~IterateCLIOptionsScope() {
    per_process::cli_options->per_isolate->per_env = original_per_env;
    per_process::cli_options->per_isolate = original_per_isolate;
  }
  std::shared_ptr<node::EnvironmentOptions> original_per_env;
  std::shared_ptr<node::PerIsolateOptions> original_per_isolate;
};

// Return a map containing all the options and their metadata as well
// as the aliases
void GetCLIOptionsValues(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);

  if (!env->has_run_bootstrapping_code()) {
    // No code because this is an assertion.
    return env->ThrowError(
        "Should not query options before bootstrapping is done");
  }
  env->set_has_serialized_options(true);

  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  IterateCLIOptionsScope s(env);

  LocalVector<Name> option_names(isolate);
  LocalVector<Value> option_values(isolate);
  option_names.reserve(_ppop_instance.options_.size() * 2);
  option_values.reserve(_ppop_instance.options_.size() * 2);

  Local<Value> undefined_value = Undefined(isolate);
  Local<Value> null_value = Null(isolate);

  for (const auto& item : _ppop_instance.options_) {
    Local<Value> value;
    const auto& option_info = item.second;
    auto field = option_info.field;
    PerProcessOptions* opts = per_process::cli_options.get();

    switch (option_info.type) {
      case kNoOp:
      case kV8Option:
        // Special case for --abort-on-uncaught-exception which is also
        // respected by Node.js internals
        if (item.first == "--abort-on-uncaught-exception") {
          value = Boolean::New(isolate,
                               s.original_per_env->abort_on_uncaught_exception);
        } else if (item.first == "--stack-trace-limit") {
          value =
              Number::New(isolate,
                          static_cast<double>(
                              *_ppop_instance.Lookup<int64_t>(field, opts)));
        } else {
          value = undefined_value;
        }
        break;
      case kBoolean: {
        bool original_value = *_ppop_instance.Lookup<bool>(field, opts);
        value = Boolean::New(isolate, original_value);

        // Add --no-* entries.
        std::string negated_name =
            "--no" + item.first.substr(1, item.first.size());
        Local<Value> negated_value = Boolean::New(isolate, !original_value);
        Local<Name> negated_name_v8 =
            ToV8Value(context, negated_name).ToLocalChecked().As<Name>();
        option_names.push_back(negated_name_v8);
        option_values.push_back(negated_value);
        break;
      }
      case kInteger:
        value = Number::New(
            isolate,
            static_cast<double>(*_ppop_instance.Lookup<int64_t>(field, opts)));
        break;
      case kUInteger:
        value = Number::New(
            isolate,
            static_cast<double>(*_ppop_instance.Lookup<uint64_t>(field, opts)));
        break;
      case kString:
        if (!ToV8Value(context,
                       *_ppop_instance.Lookup<std::string>(field, opts))
                 .ToLocal(&value)) {
          return;
        }
        break;
      case kStringList:
        if (!ToV8Value(context,
                       *_ppop_instance.Lookup<StringVector>(field, opts))
                 .ToLocal(&value)) {
          return;
        }
        break;
      case kHostPort: {
        const HostPort& host_port =
            *_ppop_instance.Lookup<HostPort>(field, opts);
        Local<Value> host;
        if (!ToV8Value(context, host_port.host()).ToLocal(&host)) {
          return;
        }
        constexpr size_t kHostPortLength = 2;
        std::array<Local<Name>, kHostPortLength> names = {env->host_string(),
                                                          env->port_string()};
        std::array<Local<Value>, kHostPortLength> values = {
            host, Integer::New(isolate, host_port.port())};
        value = Object::New(
            isolate, null_value, names.data(), values.data(), kHostPortLength);
        break;
      }
      default:
        UNREACHABLE();
    }
    CHECK(!value.IsEmpty());
    Local<Name> name =
        ToV8Value(context, item.first).ToLocalChecked().As<Name>();
    option_names.push_back(name);
    option_values.push_back(value);
  }

  Local<Object> options = Object::New(isolate,
                                      null_value,
                                      option_names.data(),
                                      option_values.data(),
                                      option_values.size());
  args.GetReturnValue().Set(options);
}

void GetCLIOptionsInfo(const FunctionCallbackInfo<Value>& args) {
  Isolate* isolate = args.GetIsolate();
  Local<Context> context = isolate->GetCurrentContext();
  Environment* env = Environment::GetCurrent(context);

  if (!env->has_run_bootstrapping_code()) {
    // No code because this is an assertion.
    return env->ThrowError(
        "Should not query options before bootstrapping is done");
  }

  Mutex::ScopedLock lock(per_process::cli_options_mutex);
  IterateCLIOptionsScope s(env);

  Local<Map> options = Map::New(isolate);
  if (options
          ->SetPrototypeV2(context,
                           env->primordials_safe_map_prototype_object())
          .IsNothing()) {
    return;
  }

  Local<Value> null_value = Null(isolate);
  for (const auto& item : _ppop_instance.options_) {
    const auto& option_info = item.second;
    auto field = option_info.field;

    Local<Name> name =
        ToV8Value(context, item.first).ToLocalChecked().As<Name>();
    Local<Value> help_text;
    if (!ToV8Value(context, option_info.help_text).ToLocal(&help_text)) {
      return;
    }
    constexpr size_t kInfoSize = 4;
    std::array<Local<Name>, kInfoSize> names = {
        env->help_text_string(),
        env->env_var_settings_string(),
        env->type_string(),
        env->default_is_true_string(),
    };
    std::array<Local<Value>, kInfoSize> values = {
        help_text,
        Integer::New(isolate, static_cast<int>(option_info.env_setting)),
        Integer::New(isolate, static_cast<int>(option_info.type)),
        Boolean::New(isolate, option_info.default_is_true),
    };
    Local<Object> info = Object::New(
        isolate, null_value, names.data(), values.data(), kInfoSize);
    if (options->Set(context, name, info).IsEmpty()) {
      return;
    }
  }

  Local<Value> aliases;
  if (!ToV8Value(context, _ppop_instance.aliases_).ToLocal(&aliases)) return;

  if (aliases.As<Object>()
          ->SetPrototypeV2(context,
                           env->primordials_safe_map_prototype_object())
          .IsNothing()) {
    return;
  }

  constexpr size_t kRetLength = 2;
  std::array<Local<Name>, kRetLength> names = {env->options_string(),
                                               env->aliases_string()};
  std::array<Local<Value>, kRetLength> values = {options, aliases};
  Local<Value> ret =
      Object::New(isolate, null_value, names.data(), values.data(), kRetLength);
  args.GetReturnValue().Set(ret);
}

void GetEmbedderOptions(const FunctionCallbackInfo<Value>& args) {
  Environment* env = Environment::GetCurrent(args);
  if (!env->has_run_bootstrapping_code()) {
    // No code because this is an assertion.
    return env->ThrowError(
        "Should not query options before bootstrapping is done");
  }
  Isolate* isolate = args.GetIsolate();

  constexpr size_t kOptionsSize = 4;
  std::array<Local<Name>, kOptionsSize> names = {
      FIXED_ONE_BYTE_STRING(env->isolate(), "shouldNotRegisterESMLoader"),
      FIXED_ONE_BYTE_STRING(env->isolate(), "noGlobalSearchPaths"),
      FIXED_ONE_BYTE_STRING(env->isolate(), "noBrowserGlobals"),
      FIXED_ONE_BYTE_STRING(env->isolate(), "hasEmbedderPreload")};

  std::array<Local<Value>, kOptionsSize> values = {
      Boolean::New(isolate, env->should_not_register_esm_loader()),
      Boolean::New(isolate, env->no_global_search_paths()),
      Boolean::New(isolate, env->no_browser_globals()),
      Boolean::New(isolate, env->embedder_preload() != nullptr)};

  Local<Object> ret = Object::New(
      isolate, Null(isolate), names.data(), values.data(), kOptionsSize);

  args.GetReturnValue().Set(ret);
}

void Initialize(Local<Object> target,
                Local<Value> unused,
                Local<Context> context,
                void* priv) {
  Environment* env = Environment::GetCurrent(context);
  Isolate* isolate = env->isolate();
  SetMethodNoSideEffect(
      context, target, "getCLIOptionsValues", GetCLIOptionsValues);
  SetMethodNoSideEffect(
      context, target, "getCLIOptionsInfo", GetCLIOptionsInfo);
  SetMethodNoSideEffect(
      context, target, "getEmbedderOptions", GetEmbedderOptions);

  Local<Object> env_settings = Object::New(isolate);
  NODE_DEFINE_CONSTANT(env_settings, kAllowedInEnvvar);
  NODE_DEFINE_CONSTANT(env_settings, kDisallowedInEnvvar);
  target
      ->Set(
          context, FIXED_ONE_BYTE_STRING(isolate, "envSettings"), env_settings)
      .Check();

  Local<Object> types = Object::New(isolate);
  NODE_DEFINE_CONSTANT(types, kNoOp);
  NODE_DEFINE_CONSTANT(types, kV8Option);
  NODE_DEFINE_CONSTANT(types, kBoolean);
  NODE_DEFINE_CONSTANT(types, kInteger);
  NODE_DEFINE_CONSTANT(types, kUInteger);
  NODE_DEFINE_CONSTANT(types, kString);
  NODE_DEFINE_CONSTANT(types, kHostPort);
  NODE_DEFINE_CONSTANT(types, kStringList);
  target->Set(context, FIXED_ONE_BYTE_STRING(isolate, "types"), types)
      .Check();
}

void RegisterExternalReferences(ExternalReferenceRegistry* registry) {
  registry->Register(GetCLIOptionsValues);
  registry->Register(GetCLIOptionsInfo);
  registry->Register(GetEmbedderOptions);
}
}  // namespace options_parser

void HandleEnvOptions(std::shared_ptr<EnvironmentOptions> env_options) {
  HandleEnvOptions(env_options, [](const char* name) {
    std::string text;
    return credentials::SafeGetenv(name, &text) ? text : "";
  });
}

void HandleEnvOptions(std::shared_ptr<EnvironmentOptions> env_options,
                      std::function<std::string(const char*)> opt_getter) {
  env_options->pending_deprecation =
      opt_getter("NODE_PENDING_DEPRECATION") == "1";

  env_options->preserve_symlinks = opt_getter("NODE_PRESERVE_SYMLINKS") == "1";

  env_options->preserve_symlinks_main =
      opt_getter("NODE_PRESERVE_SYMLINKS_MAIN") == "1";

  if (env_options->redirect_warnings.empty())
    env_options->redirect_warnings = opt_getter("NODE_REDIRECT_WARNINGS");
}

std::vector<std::string> ParseNodeOptionsEnvVar(
    const std::string& node_options, std::vector<std::string>* errors) {
  std::vector<std::string> env_argv;

  bool is_in_string = false;
  bool will_start_new_arg = true;
  for (std::string::size_type index = 0; index < node_options.size(); ++index) {
    char c = node_options.at(index);

    // Backslashes escape the following character
    if (c == '\\' && is_in_string) {
      if (index + 1 == node_options.size()) {
        errors->push_back("invalid value for NODE_OPTIONS "
                          "(invalid escape)\n");
        return env_argv;
      } else {
        c = node_options.at(++index);
      }
    } else if (c == ' ' && !is_in_string) {
      will_start_new_arg = true;
      continue;
    } else if (c == '"') {
      is_in_string = !is_in_string;
      continue;
    }

    if (will_start_new_arg) {
      env_argv.emplace_back(std::string(1, c));
      will_start_new_arg = false;
    } else {
      env_argv.back() += c;
    }
  }

  if (is_in_string) {
    errors->push_back("invalid value for NODE_OPTIONS "
                      "(unterminated string)\n");
  }
  return env_argv;
}
}  // namespace node

NODE_BINDING_CONTEXT_AWARE_INTERNAL(options, node::options_parser::Initialize)
NODE_BINDING_EXTERNAL_REFERENCE(
    options, node::options_parser::RegisterExternalReferences)
