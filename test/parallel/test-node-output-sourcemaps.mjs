import * as common from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import * as snapshot from '../common/assertSnapshot.js';
import * as path from 'node:path';
import { describe, it } from 'node:test';

describe('sourcemaps output', { concurrency: !process.env.TEST_PARALLEL }, () => {
  function normalize(str) {
    const result = str
    .replaceAll(snapshot.replaceWindowsPaths(process.cwd()), '')
    .replaceAll('//', '*')
    .replaceAll('/Users/bencoe/oss/coffee-script-test', '')
    .replaceAll(/\/(\w)/g, '*$1')
    .replaceAll('*test*', '*')
    .replaceAll('*fixtures*source-map*', '*')
    .replaceAll(/(\W+).*node:.*/g, '$1*');
    if (common.isWindows) {
      const currentDeviceLetter = path.parse(process.cwd()).root.substring(0, 1).toLowerCase();
      const regex = new RegExp(`${currentDeviceLetter}:/?`, 'gi');
      return result.replaceAll(regex, '');
    }
    return result;
  }
  const defaultTransform = snapshot
    .transform(snapshot.replaceWindowsLineEndings, snapshot.replaceWindowsPaths,
               normalize, snapshot.replaceNodeVersion);

  const tests = [
    { name: 'source-map/output/source_map_disabled_by_api.js' },
    { name: 'source-map/output/source_map_enabled_by_api.js' },
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
  ];
  for (const { name, transform } of tests) {
    it(name, async () => {
      await snapshot.spawnAndAssert(fixtures.path(name), transform ?? defaultTransform);
    });
  }
});
