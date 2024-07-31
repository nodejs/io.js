'use strict';
const common = require('../common');
const { rejects } = require('assert');

const jsModuleDataUrl = 'data:text/javascript,export{}';
const jsonModuleDataUrl = 'data:application/json,""';

async function test() {
  await rejects(
    import('data:text/css,', { with: { type: 'css' } }),
    { code: 'ERR_UNKNOWN_MODULE_FORMAT' }
  );

  await rejects(
    import('data:text/css,', { with: { unsupportedAttribute: 'value' } }),
    { code: 'ERR_IMPORT_ATTRIBUTE_UNSUPPORTED' }
  );

  await rejects(
    import(`data:text/javascript,import${JSON.stringify(jsModuleDataUrl)}with{type:"json"}`),
    { code: 'ERR_IMPORT_ATTRIBUTE_TYPE_INCOMPATIBLE' }
  );

  await rejects(
    import(jsModuleDataUrl, { with: { type: 'json' } }),
    { code: 'ERR_IMPORT_ATTRIBUTE_TYPE_INCOMPATIBLE' }
  );

  await rejects(
    import(jsModuleDataUrl, { with: { type: 'json', other: 'unsupported' } }),
    { code: 'ERR_IMPORT_ATTRIBUTE_TYPE_INCOMPATIBLE' }
  );

  await rejects(
    import(jsModuleDataUrl, { with: { type: 'unsupported' } }),
    { code: 'ERR_IMPORT_ATTRIBUTE_UNSUPPORTED' }
  );

  await rejects(
    import(jsonModuleDataUrl),
    { code: 'ERR_IMPORT_ATTRIBUTE_MISSING' }
  );

  await rejects(
    import(jsonModuleDataUrl, { with: {} }),
    { code: 'ERR_IMPORT_ATTRIBUTE_MISSING' }
  );

  await rejects(
    import(jsonModuleDataUrl, { with: { foo: 'bar' } }),
    { code: 'ERR_IMPORT_ATTRIBUTE_MISSING' }
  );

  await rejects(
    import(jsonModuleDataUrl, { with: { type: 'unsupported' } }),
    { code: 'ERR_IMPORT_ATTRIBUTE_UNSUPPORTED' }
  );

  await rejects(
    import(jsonModuleDataUrl, { assert: { type: 'json' } }),
    { code: 'ERR_IMPORT_ATTRIBUTE_MISSING' }
  );

  await rejects(
    import(`data:text/javascript,import${JSON.stringify(jsonModuleDataUrl)}assert{type:"json"}`),
    SyntaxError
  );
}

test().then(common.mustCall());
