#ifndef SRC_NODE_TASK_RUNNER_H_
#define SRC_NODE_TASK_RUNNER_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "node_internals.h"
#include "simdjson.h"
#include "spawn_sync.h"
#include "uv.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <tuple>

namespace node::task_runner {

using PositionalArgs = std::vector<std::string_view>;

// ProcessRunner is the class responsible for running a process.
// A class instance is created for each process to be run.
// The class is responsible for spawning the process and handling its exit.
// The class also handles the environment variables and arguments.
class ProcessRunner {
 public:
  ProcessRunner(std::shared_ptr<InitializationResultImpl> result,
                const std::filesystem::path& package_json_path,
                std::string_view script_name,
                std::string_view command,
                std::string_view path_env_var,
                const PositionalArgs& positional_args);
  void Run();
  static void ExitCallback(uv_process_t* req,
                           int64_t exit_status,
                           int term_signal);

 private:
  uv_loop_t* loop_ = uv_default_loop();
  uv_process_t process_{};
  uv_process_options_t options_{};
  uv_stdio_container_t child_stdio[3]{};
  std::shared_ptr<InitializationResultImpl> init_result;
  std::vector<std::string> command_args_{};
  std::vector<std::string> env_vars_{};
  std::unique_ptr<char* []> env {};  // memory for options_.env
  std::unique_ptr<char* []> arg {};  // memory for options_.args
  std::string cwd;

  // OnExit is the callback function that is called when the process exits.
  void OnExit(int64_t exit_status, int term_signal);
  void SetEnvironmentVariables();

#ifdef _WIN32
  std::string file_ = "cmd.exe";
#else
  std::string file_ = "/bin/sh";
#endif  // _WIN32

  // Represents the absolute path to the package.json file.
  std::filesystem::path package_json_path_;
  // Represents the name of the script that is being run.
  std::string script_name_;
  // Represents PATH environment variable that contains
  // all subdirectory paths appended with node_modules/.bin suffix.
  std::string path_env_var_;
};

// This function traverses up to the root directory.
// While traversing up, if it finds a package.json file, it reads its content.
// If it cannot find a package.json file, it returns std::nullopt.
// Otherwise, it returns a tuple of:
// - the path to the package.json file
// - package.json file content
// - `path_env_var` variable
//
// For example, on POSIX, it returns the following for `path_env_var`,
// if the current directory is `/anonrig`:
// `/anonrig/node_modules/.bin:/node_modules/.bin`
std::optional<std::tuple<std::filesystem::path, std::string, std::string>>
FindPackageJson(const std::filesystem::path& cwd);

void RunTask(const std::shared_ptr<InitializationResultImpl>& result,
             std::string_view command_id,
             const PositionalArgs& positional_args);
PositionalArgs GetPositionalArgs(const std::vector<std::string>& args);
std::string EscapeShell(std::string_view command);

}  // namespace node::task_runner

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_NODE_TASK_RUNNER_H_
