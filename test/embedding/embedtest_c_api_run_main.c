#include "embedtest_c_api_common.h"

// The simplest Node.js embedding scenario where the Node.js main function is
// invoked from the libnode shared library as it would be run from the Node.js
// CLI. No embedder customizations are available in this case.
int32_t test_main_c_api_nodejs_main(int32_t argc, const char* argv[]) {
  node_embedding_status embedding_status = node_embedding_status_ok;
  NODE_EMBEDDING_CALL(node_embedding_main_run(
      NODE_EMBEDDING_VERSION, argc, argv, NULL, NULL, NULL, NULL));
on_exit:
  return StatusToExitCode(PrintErrorMessage(argv[0], embedding_status));
}
