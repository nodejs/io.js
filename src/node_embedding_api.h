//
// Description: C-based API for embedding Node.js.
//
// !!! WARNING !!! WARNING !!! WARNING !!!
// This is a new API and is subject to change.
// While it is C-based, it is not ABI safe yet.
// Consider all functions and data structures as experimental.
// !!! WARNING !!! WARNING !!! WARNING !!!
//
// This file contains the C-based API for embedding Node.js in a host
// application. The API is designed to be used by applications that want to
// embed Node.js as a shared library (.so or .dll) and can interop with
// C-based API.
//

#ifndef SRC_NODE_EMBEDDING_API_H_
#define SRC_NODE_EMBEDDING_API_H_

#include "node_api.h"

#define NODE_EMBEDDING_VERSION 1

#if defined(__cplusplus) && !defined(NODE_EMBEDDING_DISABLE_CPP_ENUMS)

#define NODE_ENUM(c_name, cpp_name) enum class cpp_name : int32_t

#define NODE_ENUM_FLAGS(c_name, cpp_name)                                      \
  enum class cpp_name : int32_t;                                               \
                                                                               \
  inline constexpr cpp_name operator|(cpp_name lhs, cpp_name rhs) {            \
    return static_cast<cpp_name>(static_cast<int32_t>(lhs) |                   \
                                 static_cast<int32_t>(rhs));                   \
  }                                                                            \
                                                                               \
  inline constexpr bool IsFlagSet(cpp_name flags, cpp_name flag) {             \
    return (static_cast<int32_t>(flags) & static_cast<int32_t>(flag)) != 0;    \
  }                                                                            \
                                                                               \
  enum class cpp_name : int32_t

#define NODE_ENUM_ITEM(c_name, cpp_name) cpp_name

#else

#define NODE_ENUM(c_name, cpp_name)                                            \
  typedef enum c_name c_name;                                                  \
  enum c_name

#define NODE_ENUM_FLAGS(c_name, cpp_name) NODE_ENUM(c_name, cpp_name)

#define NODE_ENUM_ITEM(c_name, cpp_name) c_name

#endif

//==============================================================================
// Data types
//==============================================================================

typedef struct node_embedding_platform_s* node_embedding_platform;
typedef struct node_embedding_runtime_s* node_embedding_runtime;
typedef struct node_embedding_platform_config_s* node_embedding_platform_config;
typedef struct node_embedding_runtime_config_s* node_embedding_runtime_config;
typedef struct node_embedding_node_api_scope_s* node_embedding_node_api_scope;

#ifdef __cplusplus
namespace node::embedding {
#endif

// The status returned by the Node.js embedding API functions.
NODE_ENUM(node_embedding_status, NodeStatus) {
  NODE_ENUM_ITEM(node_embedding_status_ok, kOk) = 0,
  NODE_ENUM_ITEM(node_embedding_status_generic_error, kGenericError) = 1,
  NODE_ENUM_ITEM(node_embedding_status_null_arg, kNullArg) = 2,
  NODE_ENUM_ITEM(node_embedding_status_bad_arg, kBadArg) = 3,
  NODE_ENUM_ITEM(node_embedding_status_out_of_memory, kOutOfMemory) = 4,
  // This value is added to the exit code in cases when Node.js API returns
  // an error exit code.
  NODE_ENUM_ITEM(node_embedding_status_error_exit_code, kErrorExitCode) = 512,
};

// The flags for the Node.js platform initialization.
// They match the internal ProcessInitializationFlags::Flags enum.
NODE_ENUM_FLAGS(node_embedding_platform_flags, NodePlatformFlags) {
  NODE_ENUM_ITEM(node_embedding_platform_flags_none, kNone) = 0,
  // Enable stdio inheritance, which is disabled by default.
  // This flag is also implied by
  // node_embedding_platform_flags_no_stdio_initialization.
  NODE_ENUM_ITEM(node_embedding_platform_flags_enable_stdio_inheritance,
                 kEnableStdioInheritance) = 1 << 0,
  // Disable reading the NODE_ENUM_ITEMS environment variable.
  NODE_ENUM_ITEM(node_embedding_platform_flags_disable_node_options_env,
                 kDisableNodeOptionsEnv) = 1 << 1,
  // Do not parse CLI options.
  NODE_ENUM_ITEM(node_embedding_platform_flags_disable_cli_options,
                 kDisableCliOptions) = 1 << 2,
  // Do not initialize ICU.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_icu, kNoICU) = 1 << 3,
  // Do not modify stdio file descriptor or TTY state.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_stdio_initialization,
                 kNoStdioInitialization) = 1 << 4,
  // Do not register Node.js-specific signal handlers
  // and reset other signal handlers to default state.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_default_signal_handling,
                 kNoDefaultSignalHandling) = 1 << 5,
  // Do not initialize OpenSSL config.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_init_openssl,
                 kNoInitOpenSSL) = 1 << 8,
  // Do not initialize Node.js debugging based on environment variables.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_parse_global_debug_variables,
                 kNoParseGlobalDebugVariables) = 1 << 9,
  // Do not adjust OS resource limits for this process.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_adjust_resource_limits,
                 kNoAdjustResourceLimits) = 1 << 10,
  // Do not map code segments into large pages for this process.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_use_large_pages,
                 kNoUseLargePages) = 1 << 11,
  // Skip printing output for --help, --version, --v8-options.
  NODE_ENUM_ITEM(node_embedding_platform_flags_no_print_help_or_version_output,
                 kNoPrintHelpOrVersionOutput) = 1 << 12,
  // Initialize the process for predictable snapshot generation.
  NODE_ENUM_ITEM(node_embedding_platform_flags_generate_predictable_snapshot,
                 kGeneratePredictableSnapshot) = 1 << 14,
};

// The flags for the Node.js runtime initialization.
// They match the internal EnvironmentFlags::Flags enum.
NODE_ENUM_FLAGS(node_embedding_runtime_flags, NodeRuntimeFlags) {
  NODE_ENUM_ITEM(node_embedding_runtime_flags_none, kNone) = 0,
  // Use the default behavior for Node.js instances.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_default, kDefault) = 1 << 0,
  // Controls whether this Environment is allowed to affect per-process state
  // (e.g. cwd, process title, uid, etc.).
  // This is set when using node_embedding_runtime_flags_default.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_owns_process_state,
                 kOwnsProcessState) = 1 << 1,
  // Set if this Environment instance is associated with the global inspector
  // handling code (i.e. listening on SIGUSR1).
  // This is set when using node_embedding_runtime_flags_default.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_owns_inspector,
                 kOwnsInspector) = 1 << 2,
  // Set if Node.js should not run its own esm loader. This is needed by some
  // embedders, because it's possible for the Node.js esm loader to conflict
  // with another one in an embedder environment, e.g. Blink's in Chromium.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_no_register_esm_loader,
                 kNoRegisterEsmLoader) = 1 << 3,
  // Set this flag to make Node.js track "raw" file descriptors, i.e. managed
  // by fs.open() and fs.close(), and close them during
  // node_embedding_runtime_delete().
  NODE_ENUM_ITEM(node_embedding_runtime_flags_track_unmanaged_fds,
                 kTrackUnmanagedFds) = 1 << 4,
  // Set this flag to force hiding console windows when spawning child
  // processes. This is usually used when embedding Node.js in GUI programs on
  // Windows.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_hide_console_windows,
                 kHideConsoleWindows) = 1 << 5,
  // Set this flag to disable loading native addons via `process.dlopen`.
  // This environment flag is especially important for worker threads
  // so that a worker thread can't load a native addon even if `execArgv`
  // is overwritten and `--no-addons` is not specified but was specified
  // for this Environment instance.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_no_native_addons,
                 kNoNativeAddons) = 1 << 6,
  // Set this flag to disable searching modules from global paths like
  // $HOME/.node_modules and $NODE_PATH. This is used by standalone apps that
  // do not expect to have their behaviors changed because of globally
  // installed modules.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_no_global_search_paths,
                 kNoGlobalSearchPaths) = 1 << 7,
  // Do not export browser globals like setTimeout, console, etc.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_no_browser_globals,
                 kNoBrowserGlobals) = 1 << 8,
  // Controls whether or not the Environment should call
  // V8Inspector::create(). This control is needed by embedders who may not
  // want to initialize the V8 inspector in situations where one has already
  // been created, e.g. Blink's in Chromium.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_no_create_inspector,
                 kNoCreateInspector) = 1 << 9,
  // Controls whether or not the InspectorAgent for this Environment should
  // call StartDebugSignalHandler. This control is needed by embedders who may
  // not want to allow other processes to start the V8 inspector.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_no_start_debug_signal_handler,
                 kNoStartDebugSignalHandler) = 1 << 10,
  // Controls whether the InspectorAgent created for this Environment waits
  // for Inspector frontend events during the Environment creation. It's used
  // to call node::Stop(env) on a Worker thread that is waiting for the
  // events.
  NODE_ENUM_ITEM(node_embedding_runtime_flags_no_wait_for_inspector_frontend,
                 kNoWaitForInspectorFrontend) = 1 << 11,
};

#ifdef __cplusplus
}  // namespace node::embedding
using node_embedding_status = node::embedding::NodeStatus;
using node_embedding_platform_flags = node::embedding::NodePlatformFlags;
using node_embedding_runtime_flags = node::embedding::NodeRuntimeFlags;
#endif

//==============================================================================
// Callbacks
//==============================================================================

typedef node_embedding_status(NAPI_CDECL* node_embedding_data_release_callback)(
    void* data);

typedef node_embedding_status(
    NAPI_CDECL* node_embedding_platform_configure_callback)(
    void* cb_data, node_embedding_platform_config platform_config);

typedef node_embedding_status(
    NAPI_CDECL* node_embedding_runtime_configure_callback)(
    void* cb_data,
    node_embedding_platform platform,
    node_embedding_runtime_config runtime_config);

// The error is to be handled by using napi_env.
typedef void(NAPI_CDECL* node_embedding_runtime_preload_callback)(
    void* cb_data,
    node_embedding_runtime runtime,
    napi_env env,
    napi_value process,
    napi_value require);

// The error is to be handled by using napi_env.
typedef napi_value(NAPI_CDECL* node_embedding_runtime_loading_callback)(
    void* cb_data,
    node_embedding_runtime runtime,
    napi_env env,
    napi_value process,
    napi_value require,
    napi_value run_cjs);

// The error is to be handled by using napi_env.
typedef void(NAPI_CDECL* node_embedding_runtime_loaded_callback)(
    void* cb_data,
    node_embedding_runtime runtime,
    napi_env env,
    napi_value load_result);

// The error is to be handled by using napi_env.
typedef napi_value(NAPI_CDECL* node_embedding_module_initialize_callback)(
    void* cb_data,
    node_embedding_runtime runtime,
    napi_env env,
    const char* module_name,
    napi_value exports);

typedef node_embedding_status(NAPI_CDECL* node_embedding_task_run_callback)(
    void* cb_data);

typedef node_embedding_status(NAPI_CDECL* node_embedding_task_post_callback)(
    void* cb_data,
    node_embedding_task_run_callback run_task,
    void* task_data,
    node_embedding_data_release_callback release_task_data,
    bool* is_posted);

// The error is to be handled by using napi_env.
typedef void(NAPI_CDECL* node_embedding_node_api_run_callback)(void* cb_data,
                                                               napi_env env);

//==============================================================================
// Functions
//==============================================================================

EXTERN_C_START

//------------------------------------------------------------------------------
// Error handling functions.
//------------------------------------------------------------------------------

// Gets the last error message for the current thread.
NAPI_EXTERN const char* NAPI_CDECL node_embedding_last_error_message_get();

// Sets the last error message for the current thread.
NAPI_EXTERN void NAPI_CDECL
node_embedding_last_error_message_set(const char* message);

// Sets the last error message for the current thread using C printf string
// formatting.
NAPI_EXTERN void NAPI_CDECL
node_embedding_last_error_message_set_format(const char* format, ...);

//------------------------------------------------------------------------------
// Node.js global platform functions.
//------------------------------------------------------------------------------

// Runs Node.js main function.
// By default it is the same as running Node.js from CLI.
NAPI_EXTERN node_embedding_status NAPI_CDECL node_embedding_main_run(
    int32_t embedding_api_version,
    int32_t argc,
    const char* argv[],
    node_embedding_platform_configure_callback configure_platform,
    void* configure_platform_data,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data);

// Creates and configures a new Node.js platform instance.
NAPI_EXTERN node_embedding_status NAPI_CDECL node_embedding_platform_create(
    int32_t embedding_api_version,
    int32_t argc,
    const char* argv[],
    node_embedding_platform_configure_callback configure_platform,
    void* configure_platform_data,
    node_embedding_platform* result);

// Deletes the Node.js platform instance.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_platform_delete(node_embedding_platform platform);

// Sets the flags for the Node.js platform initialization.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_platform_config_set_flags(
    node_embedding_platform_config platform_config,
    node_embedding_platform_flags flags);

// Gets the parsed list of non-Node.js and Node.js arguments.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_platform_get_parsed_args(node_embedding_platform platform,
                                        int32_t* args_count,
                                        const char** args[],
                                        int32_t* runtime_args_count,
                                        const char** runtime_args[]);

//------------------------------------------------------------------------------
// Node.js runtime functions.
//------------------------------------------------------------------------------

// Runs the Node.js runtime with the provided configuration.
NAPI_EXTERN node_embedding_status NAPI_CDECL node_embedding_runtime_run(
    node_embedding_platform platform,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data);

// Creates a new Node.js runtime instance.
NAPI_EXTERN node_embedding_status NAPI_CDECL node_embedding_runtime_create(
    node_embedding_platform platform,
    node_embedding_runtime_configure_callback configure_runtime,
    void* configure_runtime_data,
    node_embedding_runtime* result);

// Deletes the Node.js runtime instance.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_delete(node_embedding_runtime runtime);

// Sets the Node-API version used for Node.js runtime.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_set_node_api_version(
    node_embedding_runtime_config runtime_config, int32_t node_api_version);

// Sets the flags for the Node.js runtime initialization.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_set_flags(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_flags flags);

// Sets the non-Node.js and Node.js CLI arguments for the Node.js runtime
// initialization.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_set_args(
    node_embedding_runtime_config runtime_config,
    int32_t argc,
    const char* argv[],
    int32_t runtime_argc,
    const char* runtime_argv[]);

// Sets the preload callback for the Node.js runtime initialization.
// It is invoked before any other code execution for the runtime Node-API
// environment and for its worker thread environments.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_on_preload(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_preload_callback preload,
    void* preload_data,
    node_embedding_data_release_callback release_preload_data);

// Sets the start execution callback for the Node.js runtime initialization.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_on_loading(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_loading_callback run_load,
    void* load_data,
    node_embedding_data_release_callback release_load_data);

// Handles the execution result for the Node.js runtime initialization.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_on_loaded(
    node_embedding_runtime_config runtime_config,
    node_embedding_runtime_loaded_callback handle_loaded,
    void* handle_loaded_data,
    node_embedding_data_release_callback release_handle_loaded_data);

// Adds a new module to the Node.js runtime.
// It is accessed as process._linkedBinding(module_name) in the main JS and in
// the related worker threads.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_add_module(
    node_embedding_runtime_config runtime_config,
    const char* module_name,
    node_embedding_module_initialize_callback init_module,
    void* init_module_data,
    node_embedding_data_release_callback release_init_module_data,
    int32_t module_node_api_version);

// Sets user data for the Node.js runtime.
// The release callback is not called for the existing user data.
// It is only called when the runtime is deleted.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_user_data_set(
    node_embedding_runtime runtime,
    void* user_data,
    node_embedding_data_release_callback release_user_data);

// Gets user data for the Node.js runtime.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_user_data_get(node_embedding_runtime runtime,
                                     void** user_data);

//------------------------------------------------------------------------------
// Node.js runtime functions for the event loop.
//------------------------------------------------------------------------------

// Sets the task runner for the Node.js runtime.
// This is an alternative way to run the Node.js runtime event loop that helps
// running it inside of an existing task scheduler.
// E.g. it enables running Node.js event loop inside of the application UI event
// loop or UI dispatcher.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_config_set_task_runner(
    node_embedding_runtime_config runtime_config,
    node_embedding_task_post_callback post_task,
    void* post_task_data,
    node_embedding_data_release_callback release_post_task_data);

// Runs the Node.js runtime event loop in UV_RUN_DEFAULT mode.
// It finishes it with emitting the beforeExit and exit process events.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_event_loop_run(node_embedding_runtime runtime);

// Stops the Node.js runtime event loop. It cannot be resumed after this call.
// It does not emit the beforeExit and exit process events if they were not
// emitted before.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_event_loop_terminate(node_embedding_runtime runtime);

// Runs the Node.js runtime event loop once. It may block the current thread.
// It matches the UV_RUN_ONCE behavior
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_event_loop_run_once(node_embedding_runtime runtime,
                                           bool* has_more_work);

// Runs the Node.js runtime event loop once. It does not block the thread.
// It matches the UV_RUN_NOWAIT behavior.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_event_loop_run_no_wait(node_embedding_runtime runtime,
                                              bool* has_more_work);

//------------------------------------------------------------------------------
// Node.js runtime functions for the Node-API interop.
//------------------------------------------------------------------------------

// Runs Node-API code in the Node-API scope.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_node_api_run(
    node_embedding_runtime runtime,
    node_embedding_node_api_run_callback run_node_api,
    void* run_node_api_data);

// Opens a new Node-API scope.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_node_api_scope_open(
    node_embedding_runtime runtime,
    node_embedding_node_api_scope* node_api_scope,
    napi_env* env);

// Closes the Node-API invocation scope.
NAPI_EXTERN node_embedding_status NAPI_CDECL
node_embedding_runtime_node_api_scope_close(
    node_embedding_runtime runtime,
    node_embedding_node_api_scope node_api_scope);

EXTERN_C_END

#endif  // SRC_NODE_EMBEDDING_API_H_
