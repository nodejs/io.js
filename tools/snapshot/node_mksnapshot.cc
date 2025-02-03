#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "libplatform/libplatform.h"
#include "node_internals.h"
#include "node_snapshot_builder.h"
#include "util-inl.h"
#include "v8.h"

int BuildSnapshot(int argc, char* argv[]);

#ifdef _WIN32
#include <windows.h>

int wmain(int argc, wchar_t* wargv[]) {
  // Windows needs conversion from wchar_t to char.

  // Convert argv to UTF8.
  char** argv = new char*[argc + 1];
  for (int i = 0; i < argc; i++) {
    // Compute the size of the required buffer
    DWORD size = WideCharToMultiByte(
        CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) {
      // This should never happen.
      fprintf(stderr, "Could not convert arguments to utf8.");
      exit(1);
    }
    // Do the actual conversion
    argv[i] = new char[size];
    DWORD result = WideCharToMultiByte(
        CP_UTF8, 0, wargv[i], -1, argv[i], size, nullptr, nullptr);
    if (result == 0) {
      // This should never happen.
      fprintf(stderr, "Could not convert arguments to utf8.");
      exit(1);
    }
  }
  argv[argc] = nullptr;
#else   // UNIX
int main(int argc, char* argv[]) {
  argv = uv_setup_args(argc, argv);

  // Disable stdio buffering, it interacts poorly with printf()
  // calls elsewhere in the program (e.g., any logging from V8.)
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
#endif  // _WIN32

  return BuildSnapshot(argc, argv);
}

int BuildSnapshot(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <path/to/output.cc>\n";
    std::cerr << "       " << argv[0] << " --build-snapshot "
              << "<path/to/script.js> <path/to/output.cc>\n";
    return 1;
  }

  std::shared_ptr<node::InitializationResult> result =
      node::InitializeOncePerProcess(
          std::vector<std::string>(argv, argv + argc),
          node::ProcessInitializationFlags::kGeneratePredictableSnapshot);

  CHECK(!result->early_return());
  CHECK_EQ(result->exit_code(), 0);

  std::string out_path;
  std::optional<std::string_view> builder_script_path = std::nullopt;
  if (node::per_process::cli_options->per_isolate->build_snapshot) {
    builder_script_path = result->args()[1];
    out_path = result->args()[2];
  } else {
    out_path = result->args()[1];
  }

#ifdef NODE_MKSNAPSHOT_USE_ARRAY_LITERALS
  bool use_array_literals = true;
#else
  bool use_array_literals = false;
#endif

  node::SnapshotConfig snapshot_config;
  snapshot_config.builder_script_path = builder_script_path;

#ifdef NODE_USE_NODE_CODE_CACHE
  snapshot_config.flags = node::SnapshotFlags::kDefault;
#else
  snapshot_config.flags = node::SnapshotFlags::kWithoutCodeCache;
#endif

  node::ExitCode exit_code =
      node::SnapshotBuilder::GenerateAsSource(out_path.c_str(),
                                              result->args(),
                                              result->exec_args(),
                                              snapshot_config,
                                              use_array_literals);

  node::TearDownOncePerProcess();
  return static_cast<int>(exit_code);
}
