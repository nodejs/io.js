// This is expected to be used by test-esm-loader-hooks.mjs via:
// node --loader ./test/fixtures/es-module-loaders/hooks-input.mjs ./test/fixtures/es-modules/json-modules.mjs

import assert from 'assert';
import { writeSync } from 'fs';
import { readFile } from 'fs/promises';
import { fileURLToPath } from 'url';


let resolveCalls = 0;
let loadCalls = 0;

export async function resolve(specifier, context, next) {
  resolveCalls++;
  let url;

  if (resolveCalls === 1) {
    url = new URL(specifier).href;
    assert.match(specifier, /json-modules\.mjs$/);

    if (!(/\[eval\d*\]$/).test(context.parentURL)) {
      assert.strictEqual(context.parentURL, undefined);
    }

    assert.deepStrictEqual(context.importAttributes, {});
  } else if (resolveCalls === 2) {
    url = new URL(specifier, context.parentURL).href;
    assert.match(specifier, /experimental\.json$/);
    assert.match(context.parentURL, /json-modules\.mjs$/);
    assert.deepStrictEqual(context.importAttributes, {
      type: 'json',
    });
  }

  // Ensure `context` has all and only the properties it's supposed to
  assert.deepStrictEqual(Reflect.ownKeys(context), [
    'conditions',
    'importAttributes',
    'parentURL',
    'importAssertions',
  ]);
  assert.ok(Array.isArray(context.conditions));
  assert.strictEqual(typeof next, 'function');

  const returnValue = {
    url,
    format: 'test',
    shortCircuit: true,
  }

  writeSync(1, JSON.stringify(returnValue) + '\n'); // For the test to validate when it parses stdout

  return returnValue;
}

export async function load(url, context, next) {
  loadCalls++;
  const source = await readFile(fileURLToPath(url));
  let format;

  if (loadCalls === 1) {
    assert.match(url, /json-modules\.mjs$/);
    assert.deepStrictEqual(context.importAttributes, {});
    format = 'module';
  } else if (loadCalls === 2) {
    assert.match(url, /experimental\.json$/);
    assert.deepStrictEqual(context.importAttributes, {
      type: 'json',
    });
    format = 'json';
  }

  assert.ok(new URL(url));
  // Ensure `context` has all and only the properties it's supposed to
  assert.deepStrictEqual(Reflect.ownKeys(context), [
    'format',
    'importAttributes',
    'importAssertions',
  ]);
  assert.strictEqual(context.format, 'test');
  assert.strictEqual(typeof next, 'function');

  const returnValue = {
    source,
    format,
    shortCircuit: true,
  };

  writeSync(1, JSON.stringify(returnValue) + '\n'); // For the test to validate when it parses stdout

  return returnValue;
}
