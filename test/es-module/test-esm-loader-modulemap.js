'use strict';
// Flags: --expose-internals

require('../common');

const { strictEqual, throws } = require('assert');
const { createModuleLoader } = require('internal/modules/esm/loader');
const { LoadCache, ResolveCache } = require('internal/modules/esm/module_map');
const { ModuleJob } = require('internal/modules/esm/module_job');
const createDynamicModule = require(
  'internal/modules/esm/create_dynamic_module');

const jsModuleDataUrl = 'data:text/javascript,export{}';
const jsonModuleDataUrl = 'data:application/json,""';

const stubJsModule = createDynamicModule([], ['default'], jsModuleDataUrl);
const stubJsonModule = createDynamicModule([], ['default'], jsonModuleDataUrl);

const loader = createModuleLoader(false);
const jsModuleJob = new ModuleJob(loader, stubJsModule.module, undefined,
                                  () => new Promise(() => {}));
const jsonModuleJob = new ModuleJob(loader, stubJsonModule.module,
                                    { type: 'json' },
                                    () => new Promise(() => {}));


// LoadCache.set and LoadCache.get store and retrieve module jobs for a
// specified url/type tuple; LoadCache.has correctly reports whether such jobs
// are stored in the map.
{
  const moduleMap = new LoadCache();

  moduleMap.set(jsModuleDataUrl, undefined, jsModuleJob);
  moduleMap.set(jsonModuleDataUrl, 'json', jsonModuleJob);

  strictEqual(moduleMap.get(jsModuleDataUrl), jsModuleJob);
  strictEqual(moduleMap.get(jsonModuleDataUrl, 'json'), jsonModuleJob);

  strictEqual(moduleMap.has(jsModuleDataUrl), true);
  strictEqual(moduleMap.has(jsModuleDataUrl, 'javascript'), true);
  strictEqual(moduleMap.has(jsonModuleDataUrl, 'json'), true);

  strictEqual(moduleMap.has('unknown'), false);

  // The types must match
  strictEqual(moduleMap.has(jsModuleDataUrl, 'json'), false);
  strictEqual(moduleMap.has(jsonModuleDataUrl, 'javascript'), false);
  strictEqual(moduleMap.has(jsonModuleDataUrl), false);
  strictEqual(moduleMap.has(jsModuleDataUrl, 'unknown'), false);
  strictEqual(moduleMap.has(jsonModuleDataUrl, 'unknown'), false);
}

// LoadCache.get, LoadCache.has and LoadCache.set should only accept string
// values as url argument.
{
  const moduleMap = new LoadCache();

  const errorObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: /^The "url" argument must be of type string/
  };

  [{}, [], true, 1].forEach((value) => {
    throws(() => moduleMap.get(value), errorObj);
    throws(() => moduleMap.has(value), errorObj);
    throws(() => moduleMap.set(value, undefined, jsModuleJob), errorObj);
  });
}

// LoadCache.get, LoadCache.has and LoadCache.set should only accept string
// values (or the kAssertType symbol) as type argument.
{
  const moduleMap = new LoadCache();

  const errorObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: /^The "type" argument must be of type string/
  };

  [{}, [], true, 1].forEach((value) => {
    throws(() => moduleMap.get(jsModuleDataUrl, value), errorObj);
    throws(() => moduleMap.has(jsModuleDataUrl, value), errorObj);
    throws(() => moduleMap.set(jsModuleDataUrl, value, jsModuleJob), errorObj);
  });
}

// LoadCache.set should only accept ModuleJob values as job argument.
{
  const moduleMap = new LoadCache();

  [{}, [], true, 1].forEach((value) => {
    throws(() => moduleMap.set('', undefined, value), {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError',
      message: /^The "job" argument must be an instance of ModuleJob/
    });
  });
}

{
  const resolveMap = new ResolveCache();

  strictEqual(resolveMap.serializeKey('./file', { __proto__: null }), './file::');
  strictEqual(resolveMap.serializeKey('./file', { __proto__: null, type: 'json' }), './file::"type""json"');
  strictEqual(resolveMap.serializeKey('./file::"type""json"', { __proto__: null }), './file::"type""json"::');
  strictEqual(resolveMap.serializeKey('./file', { __proto__: null, c: 'd', a: 'b' }), './file::"a""b","c""d"');
  strictEqual(resolveMap.serializeKey('./s', { __proto__: null, c: 'd', a: 'b', b: 'c' }), './s::"a""b","b""c","c""d"');

  resolveMap.set('key1', 'parent1', 1);
  resolveMap.set('key2', 'parent1', 2);
  resolveMap.set('key2', 'parent2', 3);

  strictEqual(resolveMap.get('key1', 'parent1'), 1);
  strictEqual(resolveMap.get('key2', 'parent1'), 2);
  strictEqual(resolveMap.get('key2', 'parent2'), 3);
}
