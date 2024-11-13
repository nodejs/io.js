// Flags: --experimental-require-module

import '../common/index.mjs';
import assert from 'node:assert';
import * as staticImport from '../fixtures/es-modules/module-condition/import.mjs';
import { import as _import } from '../fixtures/es-modules/module-condition/dynamic_import.js';

async function dynamicImport(id) {
  const result = await _import(id);
  return result.resolved;
}

assert.deepStrictEqual({ ...staticImport }, {
  import_module_require: 'import',
  module_and_import: 'module',
  module_and_require: 'module',
  module_import_require: 'module',
  module_only: 'module',
  module_require_import: 'module',
  require_module_import: 'module',
});

assert.strictEqual(await dynamicImport('import-module-require'), 'import');
assert.strictEqual(await dynamicImport('module-and-import'), 'module');
assert.strictEqual(await dynamicImport('module-and-require'), 'module');
assert.strictEqual(await dynamicImport('module-import-require'), 'module');
assert.strictEqual(await dynamicImport('module-only'), 'module');
assert.strictEqual(await dynamicImport('module-require-import'), 'module');
assert.strictEqual(await dynamicImport('require-module-import'), 'module');
