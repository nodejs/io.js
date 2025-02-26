import '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import * as snapshot from '../common/assertSnapshot.js';
import { describe, it } from 'node:test';

describe('sourcemaps output', { concurrency: !process.env.TEST_PARALLEL }, () => {
  const defaultTransform = snapshot
    .transform(
      snapshot.replaceWindowsLineEndings,
      snapshot.transformCwd('*'),
      snapshot.replaceWindowsPaths,
      // Remove drive letters from synthesized paths (i.e. not cwd).
      snapshot.replaceWindowsDriveLetter,
      snapshot.replaceInternalStackTrace,
      snapshot.replaceExperimentalWarning,
      snapshot.replaceNodeVersion
    );

  const tests = [
    { name: 'source-map/output/source_map_disabled_by_api.js' },
    { name: 'source-map/output/source_map_disabled_by_process_api.js' },
    { name: 'source-map/output/source_map_enabled_by_api.js' },
    { name: 'source-map/output/source_map_enabled_by_api_node_modules.js' },
    { name: 'source-map/output/source_map_enabled_by_process_api.js' },
    { name: 'source-map/output/source_map_enclosing_function.js' },
    { name: 'source-map/output/source_map_eval.js' },
    { name: 'source-map/output/source_map_no_source_file.js' },
    { name: 'source-map/output/source_map_prepare_stack_trace.js' },
    { name: 'source-map/output/source_map_reference_error_tabs.js' },
    { name: 'source-map/output/source_map_sourcemapping_url_string.js' },
    { name: 'source-map/output/source_map_throw_async_stack_trace.mjs' },
    { name: 'source-map/output/source_map_throw_catch.js' },
    { name: 'source-map/output/source_map_throw_construct.mjs' },
    { name: 'source-map/output/source_map_throw_first_tick.js' },
    { name: 'source-map/output/source_map_throw_icu.js' },
    { name: 'source-map/output/source_map_throw_set_immediate.js' },
    { name: 'source-map/output/source_map_vm_function.js' },
    { name: 'source-map/output/source_map_vm_module.js' },
    { name: 'source-map/output/source_map_vm_script.js' },
  ];
  for (const { name, transform } of tests) {
    it(name, async () => {
      await snapshot.spawnAndAssert(fixtures.path(name), transform ?? defaultTransform);
    });
  }
});
