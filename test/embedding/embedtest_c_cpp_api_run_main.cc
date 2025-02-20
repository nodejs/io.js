#include "embedtest_c_cpp_api_common.h"

namespace node::embedding {

// The simplest Node.js embedding scenario where the Node.js main function is
// invoked from the libnode shared library as it would be run from the Node.js
// CLI. No embedder customizations are available in this case.
int32_t test_main_c_cpp_api_nodejs_main(int32_t argc, const char* argv[]) {
  TestExitCodeHandler error_handler(argv[0]);
  NODE_EMBEDDING_CALL(
      NodePlatform::RunMain(NodeArgs(argc, argv), nullptr, nullptr));
  return 0;
}

}  // namespace node::embedding
