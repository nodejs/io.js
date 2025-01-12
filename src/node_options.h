#ifndef SRC_NODE_OPTIONS_H_
#define SRC_NODE_OPTIONS_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "node_constants.h"
#include "node_mutex.h"
#include "util.h"

#if HAVE_OPENSSL
#include "openssl/opensslv.h"
#endif

namespace node {

class HostPort {
 public:
  HostPort(const std::string& host_name, int port)
      : host_name_(host_name), port_(port) {}
  HostPort(const HostPort&) = default;
  HostPort& operator=(const HostPort&) = default;
  HostPort(HostPort&&) = default;
  HostPort& operator=(HostPort&&) = default;

  void set_host(const std::string& host) { host_name_ = host; }

  void set_port(uint16_t port) { port_ = port; }

  const std::string& host() const { return host_name_; }

  uint16_t port() const { return port_; }

  void Update(const HostPort& other) {
    if (!other.host_name_.empty()) host_name_ = other.host_name_;
    port_ = other.port_;
  }

 private:
  std::string host_name_;
  uint16_t port_;
};

class Options {
 public:
  virtual void CheckOptions(std::vector<std::string>* errors,
                            std::vector<std::string>* argv) {}
  virtual ~Options() = default;
};

struct InspectPublishUid {
  bool console;
  bool http;
};

// These options are currently essentially per-Environment, but it can be nice
// to keep them separate since they are a group of options applying to a very
// specific part of Node. It might also make more sense for them to be
// per-Isolate, rather than per-Environment.
class DebugOptions : public Options {
 public:
  DebugOptions() = default;
  DebugOptions(const DebugOptions&) = default;
  DebugOptions& operator=(const DebugOptions&) = default;
  DebugOptions(DebugOptions&&) = default;
  DebugOptions& operator=(DebugOptions&&) = default;

  bool allow_attaching_debugger = true;
  // --inspect
  bool inspector_enabled = false;
  // --inspect-wait
  bool inspect_wait = false;
  // --debug
  bool deprecated_debug = false;
  // --inspect-brk
  bool break_first_line = false;
  // --inspect-brk-node
  bool break_node_first_line = false;
  // --inspect-publish-uid
  std::string inspect_publish_uid_string = "stderr,http";

  InspectPublishUid inspect_publish_uid;

  enum { kDefaultInspectorPort = 9229 };

  HostPort host_port{"127.0.0.1", kDefaultInspectorPort};

  // Used to patch the options as if --inspect-brk is passed.
  void EnableBreakFirstLine() {
    inspector_enabled = true;
    break_first_line = true;
  }

  void DisableWaitOrBreakFirstLine() {
    inspect_wait = false;
    break_first_line = false;
  }

  bool wait_for_connect() const {
    return break_first_line || break_node_first_line || inspect_wait;
  }

  bool should_break_first_line() const {
    return break_first_line || break_node_first_line;
  }

  void CheckOptions(std::vector<std::string>* errors,
                    std::vector<std::string>* argv) override;
};

class EnvironmentOptions : public Options {
 public:
  bool abort_on_uncaught_exception = false;
  std::vector<std::string> conditions;
  bool detect_module = true;
  bool disable_sigusr1 = false;
  bool print_required_tla = false;
  bool require_module = true;
  std::string dns_result_order;
  bool enable_source_maps = false;
  bool experimental_addon_modules = false;
  bool experimental_eventsource = false;
  bool experimental_fetch = true;
  bool experimental_websocket = true;
  bool experimental_sqlite = true;
  bool experimental_webstorage = false;
  bool experimental_quic = false;
  std::string localstorage_file;
  bool experimental_global_navigator = true;
  bool experimental_global_web_crypto = true;
  bool experimental_wasm_modules = false;
  bool experimental_import_meta_resolve = false;
  std::string input_type;  // Value of --input-type
  bool entry_is_url = false;
  bool permission = false;
  std::vector<std::string> allow_fs_read;
  std::vector<std::string> allow_fs_write;
  bool allow_addons = false;
  bool allow_child_process = false;
  bool allow_wasi = false;
  bool allow_worker_threads = false;
  bool experimental_repl_await = true;
  bool experimental_vm_modules = false;
  bool async_context_frame = false;
  bool expose_internals = false;
  bool force_node_api_uncaught_exceptions_policy = false;
  bool frozen_intrinsics = false;
  int64_t heap_snapshot_near_heap_limit = 0;
  std::string heap_snapshot_signal;
  bool network_family_autoselection = true;
  uint64_t network_family_autoselection_attempt_timeout = 250;
  uint64_t max_http_header_size = 16 * 1024;
  bool deprecation = true;
  bool force_async_hooks_checks = true;
  bool allow_native_addons = true;
  bool global_search_paths = true;
  bool warnings = true;
  std::vector<std::string> disable_warnings;
  bool force_context_aware = false;
  bool pending_deprecation = false;
  bool preserve_symlinks = false;
  bool preserve_symlinks_main = false;
  bool prof_process = false;
#if HAVE_INSPECTOR
  std::string cpu_prof_dir;
  static const uint64_t kDefaultCpuProfInterval = 1000;
  uint64_t cpu_prof_interval = kDefaultCpuProfInterval;
  std::string cpu_prof_name;
  bool cpu_prof = false;
  bool experimental_network_inspection = false;
  std::string heap_prof_dir;
  std::string heap_prof_name;
  static const uint64_t kDefaultHeapProfInterval = 512 * 1024;
  uint64_t heap_prof_interval = kDefaultHeapProfInterval;
  bool heap_prof = false;
#endif  // HAVE_INSPECTOR
  std::string redirect_warnings;
  std::string diagnostic_dir;
  std::string env_file;
  std::string optional_env_file;
  bool has_env_file_string = false;
  bool test_runner = false;
  uint64_t test_runner_concurrency = 0;
  uint64_t test_runner_timeout = 0;
  bool test_runner_coverage = false;
  bool test_runner_force_exit = false;
  uint64_t test_coverage_branches = 0;
  uint64_t test_coverage_functions = 0;
  uint64_t test_coverage_lines = 0;
  bool test_runner_module_mocks = false;
  bool test_runner_update_snapshots = false;
  std::vector<std::string> test_name_pattern;
  std::vector<std::string> test_reporter;
  std::vector<std::string> test_reporter_destination;
  bool test_only = false;
  bool test_udp_no_try_send = false;
  std::string test_isolation = "process";
  std::string test_shard;
  std::vector<std::string> test_skip_pattern;
  std::vector<std::string> coverage_include_pattern;
  std::vector<std::string> coverage_exclude_pattern;
  bool throw_deprecation = false;
  bool trace_deprecation = false;
  bool trace_exit = false;
  bool trace_sync_io = false;
  bool trace_tls = false;
  bool trace_uncaught = false;
  bool trace_warnings = false;
  bool trace_promises = false;
  bool trace_env = false;
  bool trace_env_js_stack = false;
  bool trace_env_native_stack = false;
  std::string trace_require_module;
  bool extra_info_on_fatal_exception = true;
  std::string unhandled_rejections;
  std::vector<std::string> userland_loaders;
  bool verify_base_objects =
#ifdef DEBUG
      true;
#else
      false;
#endif  // DEBUG

  bool watch_mode = false;
  bool watch_mode_report_to_parent = false;
  bool watch_mode_preserve_output = false;
  std::vector<std::string> watch_mode_paths;

  bool syntax_check_only = false;
  bool has_eval_string = false;
  std::string eval_string;
  bool print_eval = false;
  bool force_repl = false;

  bool insecure_http_parser = false;

  bool tls_min_v1_0 = false;
  bool tls_min_v1_1 = false;
  bool tls_min_v1_2 = false;
  bool tls_min_v1_3 = false;
  bool tls_max_v1_2 = false;
  bool tls_max_v1_3 = false;
  std::string tls_keylog;

  std::vector<std::string> preload_cjs_modules;

  std::vector<std::string> preload_esm_modules;

  bool experimental_strip_types = true;
  bool experimental_transform_types = false;

  std::vector<std::string> user_argv;

  bool report_exclude_env = false;
  bool report_exclude_network = false;

  inline DebugOptions* get_debug_options() { return &debug_options_; }
  inline const DebugOptions& debug_options() const { return debug_options_; }

  void CheckOptions(std::vector<std::string>* errors,
                    std::vector<std::string>* argv) override;

 private:
  DebugOptions debug_options_;
};

class PerIsolateOptions : public Options {
 public:
  PerIsolateOptions() = default;
  PerIsolateOptions(PerIsolateOptions&&) = default;

  std::shared_ptr<EnvironmentOptions> per_env { new EnvironmentOptions() };
  bool track_heap_objects = false;
  bool report_uncaught_exception = false;
  bool report_on_signal = false;
  bool experimental_shadow_realm = false;
  int64_t stack_trace_limit = 10;
  std::string report_signal = "SIGUSR2";
  bool build_snapshot = false;
  std::string build_snapshot_config;
  inline EnvironmentOptions* get_per_env_options();
  void CheckOptions(std::vector<std::string>* errors,
                    std::vector<std::string>* argv) override;

  inline std::shared_ptr<PerIsolateOptions> Clone() const;

 private:
  PerIsolateOptions(const PerIsolateOptions&) = default;
};

class PerProcessOptions : public Options {
 public:
  // Options shouldn't be here unless they affect the entire process scope, and
  // that should avoided when possible.
  //
  // When an option is used during process initialization, it does not need
  // protection, but any use after that will likely require synchronization
  // using the node::per_process::cli_options_mutex, typically:
  //
  //     Mutex::ScopedLock lock(node::per_process::cli_options_mutex);
  std::shared_ptr<PerIsolateOptions> per_isolate { new PerIsolateOptions() };

  std::string title;
  std::string trace_event_categories;
  std::string trace_event_file_pattern = "node_trace.${rotation}.log";
  int64_t v8_thread_pool_size = 4;
  bool zero_fill_all_buffers = false;
  bool debug_arraybuffer_allocations = false;
  std::string disable_proto;
  // We enable the shared read-only heap which currently requires that the
  // snapshot used in different isolates in the same process to be the same.
  // Therefore --node-snapshot is a per-process option.
  bool node_snapshot = true;
  std::string snapshot_blob;

  std::vector<std::string> security_reverts;
  bool print_bash_completion = false;
  bool print_help = false;
  bool print_v8_help = false;
  bool print_version = false;
  std::string experimental_sea_config;
  std::string run;

#ifdef NODE_HAVE_I18N_SUPPORT
  std::string icu_data_dir;
#endif

  // Per-process because they affect singleton OpenSSL shared library state,
  // or are used once during process initialization.
#if HAVE_OPENSSL
  std::string openssl_config;
  std::string tls_cipher_list = DEFAULT_CIPHER_LIST_CORE;
  int64_t secure_heap = 0;
  int64_t secure_heap_min = 2;
#ifdef NODE_OPENSSL_CERT_STORE
  bool ssl_openssl_cert_store = true;
#else
  bool ssl_openssl_cert_store = false;
#endif
  bool use_openssl_ca = false;
  bool use_bundled_ca = false;
  bool enable_fips_crypto = false;
  bool force_fips_crypto = false;
#endif
#if OPENSSL_VERSION_MAJOR >= 3
  bool openssl_legacy_provider = false;
  bool openssl_shared_config = false;
#endif

  bool disable_wasm_trap_handler = false;

  // Per-process because reports can be triggered outside a known V8 context.
  bool report_on_fatalerror = false;
  bool report_compact = false;
  std::string report_directory;
  std::string report_filename;

  // TODO(addaleax): Some of these could probably be per-Environment.
  std::string use_largepages = "off";
  bool trace_sigint = false;
  std::vector<std::string> cmdline;

  inline PerIsolateOptions* get_per_isolate_options();
  void CheckOptions(std::vector<std::string>* errors,
                    std::vector<std::string>* argv) override;
};

// The actual options parser, as opposed to the structs containing them:

namespace options_parser {

HostPort SplitHostPort(const std::string& arg,
    std::vector<std::string>* errors);
void GetOptions(const v8::FunctionCallbackInfo<v8::Value>& args);
std::string GetBashCompletion();

enum OptionType {
  kNoOp,
  kV8Option,
  kBoolean,
  kInteger,
  kUInteger,
  kString,
  kHostPort,
  kStringList,
};

template <typename Options>
class OptionsParser {
 public:
  virtual ~OptionsParser() = default;

  typedef Options TargetType;

  struct NoOp {};
  struct V8Option {};

  // These methods add a single option to the parser. Optionally, it can be
  // specified whether the option should be allowed from environment variable
  // sources (i.e. NODE_OPTIONS).
  void AddOption(const char* name,
                 const char* help_text,
                 bool Options::*field,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar,
                 bool default_is_true = false);
  void AddOption(const char* name,
                 const char* help_text,
                 uint64_t Options::*field,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar);
  void AddOption(const char* name,
                 const char* help_text,
                 int64_t Options::*field,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar);
  void AddOption(const char* name,
                 const char* help_text,
                 std::string Options::*field,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar);
  void AddOption(const char* name,
                 const char* help_text,
                 std::vector<std::string> Options::*field,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar);
  void AddOption(const char* name,
                 const char* help_text,
                 HostPort Options::*field,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar);
  void AddOption(const char* name,
                 const char* help_text,
                 NoOp no_op_tag,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar);
  void AddOption(const char* name,
                 const char* help_text,
                 V8Option v8_option_tag,
                 OptionEnvvarSettings env_setting = kDisallowedInEnvvar);

  // Adds aliases. An alias can be of the form "--option-a" -> "--option-b",
  // or have a more complex group expansion, like
  //   "--option-a" -> { "--option-b", "--harmony-foobar", "--eval", "42" }
  // If `from` has the form "--option-a=", the alias will only be expanded if
  // the option is presented in that form (i.e. with a '=').
  // If `from` has the form "--option-a <arg>", the alias will only be expanded
  // if the option has a non-option argument (not starting with -) following it.
  void AddAlias(const char* from, const char* to);
  void AddAlias(const char* from, const std::vector<std::string>& to);
  void AddAlias(const char* from,
                const std::initializer_list<std::string>& to);

  // Add implications from some arbitrary option to a boolean one, either
  // in a way that makes `from` set `to` to true or to false.
  void Implies(const char* from, const char* to);
  void ImpliesNot(const char* from, const char* to);

  // Insert options from another options parser into this one, along with
  // a method that yields the target options type from this parser's options
  // type.
  template <typename ChildOptions>
  void Insert(const OptionsParser<ChildOptions>& child_options_parser,
              ChildOptions* (Options::* get_child)());

  // Parse a sequence of options into an options struct, a list of
  // arguments that were parsed as options, a list of unknown/JS engine options,
  // and leave the remainder in the input `args` vector.
  //
  // For example, an `args` input of
  //
  //   node --foo --harmony-bar --fizzle=42 -- /path/to/cow moo
  //
  // expands as
  //
  // - `args` -> { "node", "/path/to/cow", "moo" }
  // - `exec_args` -> { "--foo", "--harmony-bar", "--fizzle=42" }
  // - `v8_args` -> `{ "node", "--harmony-bar" }
  // - `options->foo == true`, `options->fizzle == 42`.
  //
  // If `*error` is set, the result of the parsing should be discarded and the
  // contents of any of the argument vectors should be considered undefined.
  void Parse(std::vector<std::string>* const args,
             std::vector<std::string>* const exec_args,
             std::vector<std::string>* const v8_args,
             Options* const options,
             OptionEnvvarSettings required_env_settings,
             std::vector<std::string>* const errors) const;

 private:
  // We support the wide variety of different option types by remembering
  // how to access them, given a certain `Options` struct.

  // Represents a field within `Options`.
  class BaseOptionField {
   public:
    virtual ~BaseOptionField() = default;
    virtual void* LookupImpl(Options* options) const = 0;

    template <typename T>
    inline T* Lookup(Options* options) const {
      return static_cast<T*>(LookupImpl(options));
    }
  };

  // Represents a field of type T within `Options` that can be looked up
  // as a C++ member field.
  template <typename T>
  class SimpleOptionField : public BaseOptionField {
   public:
    explicit SimpleOptionField(T Options::* field) : field_(field) {}
    void* LookupImpl(Options* options) const override {
      return static_cast<void*>(&(options->*field_));
    }

   private:
    T Options::* field_;
  };

  template <typename T>
  inline T* Lookup(std::shared_ptr<BaseOptionField> field,
                   Options* options) const {
    return field->template Lookup<T>(options);
  }

  // An option consists of:
  // - A type.
  // - A way to store/access the property value.
  // - The information of whether it may occur in an env var or not.
  struct OptionInfo {
    OptionType type;
    std::shared_ptr<BaseOptionField> field;
    OptionEnvvarSettings env_setting;
    std::string help_text;
    bool default_is_true = false;
  };

  // An implied option is composed of the information on where to store a
  // specific boolean value (if another specific option is encountered).
  struct Implication {
    OptionType type;
    std::string name;
    std::shared_ptr<BaseOptionField> target_field;
    bool target_value;
  };

  // These are helpers that make `Insert()` support properties of other
  // options structs, if we know how to access them.
  template <typename OriginalField, typename ChildOptions>
  static auto Convert(
      std::shared_ptr<OriginalField> original,
      ChildOptions* (Options::* get_child)());
  template <typename ChildOptions>
  static auto Convert(
      typename OptionsParser<ChildOptions>::OptionInfo original,
      ChildOptions* (Options::* get_child)());
  template <typename ChildOptions>
  static auto Convert(
      typename OptionsParser<ChildOptions>::Implication original,
      ChildOptions* (Options::* get_child)());

  std::unordered_map<std::string, OptionInfo> options_;
  std::unordered_map<std::string, std::vector<std::string>> aliases_;
  std::unordered_multimap<std::string, Implication> implications_;

  template <typename OtherOptions>
  friend class OptionsParser;

  friend void GetCLIOptionsValues(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  friend void GetCLIOptionsInfo(
      const v8::FunctionCallbackInfo<v8::Value>& args);
  friend std::string GetBashCompletion();
};

using StringVector = std::vector<std::string>;
template <class OptionsType, class = Options>
void Parse(
  StringVector* const args, StringVector* const exec_args,
  StringVector* const v8_args, OptionsType* const options,
  OptionEnvvarSettings required_env_settings, StringVector* const errors);

}  // namespace options_parser

namespace per_process {

extern Mutex cli_options_mutex;
extern NODE_EXTERN_PRIVATE std::shared_ptr<PerProcessOptions> cli_options;

}  // namespace per_process

void HandleEnvOptions(std::shared_ptr<EnvironmentOptions> env_options);
void HandleEnvOptions(std::shared_ptr<EnvironmentOptions> env_options,
                      std::function<std::string(const char*)> opt_getter);

std::vector<std::string> ParseNodeOptionsEnvVar(
    const std::string& node_options, std::vector<std::string>* errors);
}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_OPTIONS_H_
