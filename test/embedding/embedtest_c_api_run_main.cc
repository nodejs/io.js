#include <node_embedding_api.h>

// The simplest Node.js embedding scenario where the Node.js main function is
// invoked from the libnode shared library as it would be run from the Node.js
// CLI. No embedder customizations are available in this case.
extern "C" int32_t test_main_nodejs_main_node_api(int32_t argc, char* argv[]) {
  return node_embedding_run_main(argc, argv, {}, {});
}
