#include <cstring>
#include <string_view>
#include <unordered_map>
#include "executable_wrapper.h"

int32_t test_main_cpp_api(int32_t argc, const char* argv[]);

extern "C" int32_t test_main_c_api(int32_t argc, const char* argv[]);
extern "C" int32_t test_main_c_api_nodejs_main(int32_t argc,
                                               const char* argv[]);
extern "C" int32_t test_main_c_api_threading_runtime_per_thread(
    int32_t argc, const char* argv[]);
extern "C" int32_t test_main_c_api_threading_several_runtimes_per_thread(
    int32_t argc, const char* argv[]);
extern "C" int32_t test_main_c_api_threading_runtime_in_several_threads(
    int32_t argc, const char* argv[]);
extern "C" int32_t test_main_c_api_threading_runtime_in_ui_thread(
    int32_t argc, const char* argv[]);
extern "C" int32_t test_main_c_api_preload(int32_t argc, const char* argv[]);
extern "C" int32_t test_main_c_api_linked_modules(int32_t argc,
                                                  const char* argv[]);
extern "C" int32_t test_main_c_api_env_no_browser_globals(int32_t argc,
                                                          const char* argv[]);
extern "C" int32_t test_main_c_api_env_with_esm_loader(int32_t argc,
                                                       const char* argv[]);
extern "C" int32_t test_main_c_api_env_with_no_esm_loader(int32_t argc,
                                                          const char* argv[]);
namespace node::embedding {

int32_t test_main_c_cpp_api(int32_t argc, const char* argv[]);
int32_t test_main_c_cpp_api_nodejs_main(int32_t argc, const char* argv[]);
int32_t test_main_c_cpp_api_threading_runtime_per_thread(int32_t argc,
                                                         const char* argv[]);
int32_t test_main_c_cpp_api_threading_several_runtimes_per_thread(
    int32_t argc, const char* argv[]);
int32_t test_main_c_cpp_api_threading_runtime_in_several_threads(
    int32_t argc, const char* argv[]);
int32_t test_main_c_cpp_api_threading_runtime_in_ui_thread(int32_t argc,
                                                           const char* argv[]);
int32_t test_main_c_cpp_api_preload(int32_t argc, const char* argv[]);
int32_t test_main_c_cpp_api_linked_modules(int32_t argc, const char* argv[]);
int32_t test_main_c_cpp_api_env_no_browser_globals(int32_t argc,
                                                   const char* argv[]);
int32_t test_main_c_cpp_api_env_with_esm_loader(int32_t argc,
                                                const char* argv[]);
int32_t test_main_c_cpp_api_env_with_no_esm_loader(int32_t argc,
                                                   const char* argv[]);
}  // namespace node::embedding

typedef int32_t (*main_callback)(int32_t argc, const char* argv[]);

int32_t CallWithoutArg1(main_callback main, int32_t argc, const char* argv[]) {
  for (int32_t i = 2; i < argc; i++) {
    argv[i - 1] = argv[i];
  }
  argv[--argc] = nullptr;
  return main(argc, argv);
}

NODE_MAIN(int32_t argc, node::argv_type raw_argv[]) {
  char** argv = nullptr;
  node::FixupMain(argc, raw_argv, &argv);

  const std::unordered_map<std::string_view, main_callback> main_map = {
      {"cpp-api", test_main_cpp_api},
      {"c-api", test_main_c_api},
      {"c-api-nodejs-main", test_main_c_api_nodejs_main},
      {"c-api-threading-runtime-per-thread",
       test_main_c_api_threading_runtime_per_thread},
      {"c-api-threading-several-runtimes-per-thread",
       test_main_c_api_threading_several_runtimes_per_thread},
      {"c-api-threading-runtime-in-several-threads",
       test_main_c_api_threading_runtime_in_several_threads},
      {"c-api-threading-runtime-in-ui-thread",
       test_main_c_api_threading_runtime_in_ui_thread},
      {"c-api-preload", test_main_c_api_preload},
      {"c-api-linked-modules", test_main_c_api_linked_modules},
      {"c-api-env-no-browser-globals", test_main_c_api_env_no_browser_globals},
      {"c-api-env-with-esm-loader", test_main_c_api_env_with_esm_loader},
      {"c-api-env-with-no-esm-loader", test_main_c_api_env_with_no_esm_loader},
      {"c-cpp-api", node::embedding::test_main_c_cpp_api},
      {"c-cpp-api-nodejs-main",
       node::embedding::test_main_c_cpp_api_nodejs_main},
      {"c-cpp-api-threading-runtime-per-thread",
       node::embedding::test_main_c_cpp_api_threading_runtime_per_thread},
      {"c-cpp-api-threading-several-runtimes-per-thread",
       node::embedding::
           test_main_c_cpp_api_threading_several_runtimes_per_thread},
      {"c-cpp-api-threading-runtime-in-several-threads",
       node::embedding::
           test_main_c_cpp_api_threading_runtime_in_several_threads},
      {"c-cpp-api-threading-runtime-in-ui-thread",
       node::embedding::test_main_c_cpp_api_threading_runtime_in_ui_thread},
      {"c-cpp-api-preload", node::embedding::test_main_c_cpp_api_preload},
      {"c-cpp-api-linked-modules",
       node::embedding::test_main_c_cpp_api_linked_modules},
      {"c-cpp-api-env-no-browser-globals",
       node::embedding::test_main_c_cpp_api_env_no_browser_globals},
      {"c-cpp-api-env-with-esm-loader",
       node::embedding::test_main_c_cpp_api_env_with_esm_loader},
      {"c-cpp-api-env-with-no-esm-loader",
       node::embedding::test_main_c_cpp_api_env_with_no_esm_loader},
  };
  if (argc > 1) {
    const char* arg1 = argv[1];
    for (const auto& [key, value] : main_map) {
      if (key == arg1) {
        return CallWithoutArg1(value, argc, (const char**)argv);
      }
    }
  }
  return test_main_cpp_api(argc, (const char**)argv);
}
