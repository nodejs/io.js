import { spawnPromisified } from '../common/index.mjs';
import * as tmpdir from '../common/tmpdir.js';
import * as fixtures from '../common/fixtures.mjs';
import { deepStrictEqual } from 'node:assert';
import { mkdir, rm, cp } from 'node:fs/promises';
import { execPath } from 'node:process';

tmpdir.refresh();

const base = tmpdir.fileURL(`test-esm-loader-resolve-type-${(Math.random() * Date.now()).toFixed(0)}`);
const moduleName = 'module-counter-by-type';
const moduleURL = new URL(`${base}/node_modules/${moduleName}`);
try {
  await mkdir(moduleURL, { recursive: true });
  await cp(
    fixtures.path('es-modules', 'module-counter-by-type'),
    moduleURL,
    { recursive: true }
  );

  const output = await spawnPromisified(
    execPath,
    [
      '--no-warnings',
      '--input-type=module',
      '--eval',
      `import { getModuleTypeStats } from ${JSON.stringify(fixtures.fileURL('es-module-loaders', 'hook-resolve-type.mjs'))};
      const before = getModuleTypeStats();
      await import(${JSON.stringify(moduleName)});
      const after = getModuleTypeStats();
      console.log(JSON.stringify({ before, after }));`,
    ],
    { cwd: base },
  );

  deepStrictEqual(output, {
    code: 0,
    signal: null,
    stderr: '',
    stdout: JSON.stringify({
      before: { importedESM: 0, importedCJS: 0 },
      // Dynamic import in the eval script should increment ESM counter but not CJS counter
      after: { importedESM: 1, importedCJS: 0 },
    }) + '\n',
  });
} finally {
  await rm(base, { recursive: true, force: true });
}
