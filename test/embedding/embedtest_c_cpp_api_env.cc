#include "embedtest_c_cpp_api_common.h"

namespace node::embedding {

// Test the no_browser_globals option.
int32_t test_main_c_cpp_api_env_no_browser_globals(int32_t argc,
                                                   const char* argv[]) {
  TestExitCodeHandler error_handler(argv[0]);
  NODE_EMBEDDING_CALL(NodePlatform::RunMain(
      NodeArgs(argc, argv),
      nullptr,
      [](const NodePlatform& platform,
         const NodeRuntimeConfig& runtime_config) {
        NodeEmbeddingErrorHandler error_handler;
        NODE_EMBEDDING_CALL(
            runtime_config.SetFlags(NodeRuntimeFlags::kNoBrowserGlobals));
        NODE_EMBEDDING_CALL(LoadUtf8Script(runtime_config,
                                           R"JS(
const assert = require('assert');
const path = require('path');
const relativeRequire =
  require('module').createRequire(path.join(process.cwd(), 'stub.js'));
const { intrinsics, nodeGlobals } =
  relativeRequire('./test/common/globals');
const items = Object.getOwnPropertyNames(globalThis);
const leaks = [];
for (const item of items) {
  if (intrinsics.has(item)) {
    continue;
  }
  if (nodeGlobals.has(item)) {
    continue;
  }
  if (item === '$jsDebugIsRegistered') {
    continue;
  }
  leaks.push(item);
}
assert.deepStrictEqual(leaks, []);
)JS"));
        return NodeExpected<void>();
      }));
  return error_handler.ReportResult();
}

// Test ESM loaded
int32_t test_main_c_cpp_api_env_with_esm_loader(int32_t argc,
                                                const char* argv[]) {
  TestExitCodeHandler error_handler(argv[0]);
  // We currently cannot pass argument to command line arguments to the runtime.
  // They must be parsed by the platform.
  std::vector<std::string> args_vec(argv, argv + argc);
  args_vec.push_back("--experimental-vm-modules");
  NodeCStringArray args(args_vec);
  NODE_EMBEDDING_CALL(
      NodePlatform::RunMain(NodeArgs(args),
                            nullptr,
                            [](const NodePlatform& platform,
                               const NodeRuntimeConfig& runtime_config) {
                              return LoadUtf8Script(runtime_config,
                                                    R"JS(
globalThis.require = require('module').createRequire(process.execPath);
const { SourceTextModule } = require('node:vm');
(async () => {
  const stmString = 'globalThis.importResult = import("")';
  const m = new SourceTextModule(stmString, {
    importModuleDynamically: (async () => {
      const m = new SourceTextModule('');
      await m.link(() => 0);
      await m.evaluate();
      return m.namespace;
    }),
  });
  await m.link(() => 0);
  await m.evaluate();
  delete globalThis.importResult;
  process.exit(0);
})();
)JS");
                            }));
  return error_handler.ReportResult();
}

// Test ESM loaded
int32_t test_main_c_cpp_api_env_with_no_esm_loader(int32_t argc,
                                                   const char* argv[]) {
  TestExitCodeHandler error_handler(argv[0]);
  NODE_EMBEDDING_CALL(
      NodePlatform::RunMain(NodeArgs(argc, argv),
                            nullptr,
                            [](const NodePlatform& platform,
                               const NodeRuntimeConfig& runtime_config) {
                              return LoadUtf8Script(runtime_config,
                                                    R"JS(
globalThis.require = require('module').createRequire(process.execPath);
const { SourceTextModule } = require('node:vm');
(async () => {
  const stmString = 'globalThis.importResult = import("")';
  const m = new SourceTextModule(stmString, {
    importModuleDynamically: (async () => {
      const m = new SourceTextModule('');
      await m.link(() => 0);
      await m.evaluate();
      return m.namespace;
    }),
  });
  await m.link(() => 0);
  await m.evaluate();
  delete globalThis.importResult;
})();
)JS");
                            }));
  return error_handler.ReportResult();
}

}  // namespace node::embedding
