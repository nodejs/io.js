#include "embedtest_c_api_common.h"

using namespace node;

// Test the no_browser_globals option.
extern "C" int32_t test_main_c_api_env_no_browser_globals(int32_t argc,
                                                          char* argv[]) {
  return node_embedding_run_main(
      argc,
      argv,
      {},
      AsFunctorRef<node_embedding_configure_runtime_functor_ref>(
          [](node_embedding_platform platform,
             node_embedding_runtime_config runtime_config) {
            CHECK_STATUS(node_embedding_runtime_set_flags(
                runtime_config,
                node_embedding_runtime_flags_no_browser_globals));
            return LoadUtf8Script(runtime_config,
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
)JS");
          }));
}

// Test ESM loaded
extern "C" int32_t test_main_c_api_env_with_esm_loader(int32_t argc,
                                                       char* argv[]) {
  // We currently cannot pass argument to command line arguments to the runtime.
  // They must be parsed by the platform.
  std::vector<std::string> args_vec(argv, argv + argc);
  args_vec.push_back("--experimental-vm-modules");
  CStringArray args(args_vec);
  return node_embedding_run_main(
      args.argc(),
      const_cast<char**>(args.argv()),
      {},
      AsFunctorRef<node_embedding_configure_runtime_functor_ref>(
          [](node_embedding_platform platform,
             node_embedding_runtime_config runtime_config) {
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
}

// Test ESM loaded
extern "C" int32_t test_main_c_api_env_with_no_esm_loader(int32_t argc,
                                                          char* argv[]) {
  return node_embedding_run_main(
      argc,
      argv,
      {},
      AsFunctorRef<node_embedding_configure_runtime_functor_ref>(
          [](node_embedding_platform platform,
             node_embedding_runtime_config runtime_config) {
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
}
