'use strict';
const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('assert');
function createURL(mime, body) {
  return `data:${mime},${body}`;
}

function createBase64URL(mime, body) {
  return `data:${mime};base64,${Buffer.from(body).toString('base64')}`;
}
(async () => {
  {
    const body = 'export default {a:"aaa"};';
    const plainESMURL = createURL('text/javascript', body);
    const ns = await import(plainESMURL);
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default.a, 'aaa');
    const importerOfURL = createURL(
      'text/javascript',
      `export {default as default} from ${JSON.stringify(plainESMURL)}`
    );
    assert.strictEqual(
      (await import(importerOfURL)).default,
      ns.default
    );
    const base64ESMURL = createBase64URL('text/javascript', body);
    assert.notStrictEqual(
      await import(base64ESMURL),
      ns
    );
  }
  {
    const body = 'export default import.meta.url;';
    const plainESMURL = createURL('text/javascript', body);
    const ns = await import(plainESMURL);
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default, plainESMURL);
  }
  {
    const body = 'export default import.meta.url;';
    const plainESMURL = createURL('text/javascript;charset=UTF-8', body);
    const ns = await import(plainESMURL);
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default, plainESMURL);
  }
  {
    const body = 'export default import.meta.url;';
    const plainESMURL = createURL('text/javascript;charset="UTF-8"', body);
    const ns = await import(plainESMURL);
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default, plainESMURL);
  }
  {
    const body = 'export default import.meta.url;';
    const plainESMURL = createURL('text/javascript;;a=a;b=b;;', body);
    const ns = await import(plainESMURL);
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default, plainESMURL);
  }
  {
    const ns = await import('data:application/json;foo="test,"this"',
      { with: { type: 'json' } });
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default, 'this');
  }
  {
    const ns = await import(`data:application/json;foo=${
      encodeURIComponent('test,')
    },0`, { with: { type: 'json' } });
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default, 0);
  }
  {
    await assert.rejects(async () =>
      import('data:application/json;foo="test,",0',
        { with: { type: 'json' } }), {
      name: 'SyntaxError',
      message: /Unterminated string in JSON at position 3/
    });
  }
  {
    const body = '{"x": 1}';
    const plainESMURL = createURL('application/json', body);
    const ns = await import(plainESMURL, { with: { type: 'json' } });
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default.x, 1);
  }
  {
    const body = '{"default": 2}';
    const plainESMURL = createURL('application/json', body);
    const ns = await import(plainESMURL, { with: { type: 'json' } });
    assert.deepStrictEqual(Object.keys(ns), ['default']);
    assert.strictEqual(ns.default.default, 2);
  }
  {
    const body = 'null';
    const plainESMURL = createURL('invalid', body);
    await assert.rejects(import(plainESMURL), { code: 'ERR_UNKNOWN_MODULE_FORMAT' });
  }
  {
    const plainESMURL = 'data:text/javascript,export%20default%202';
    const module = await import(plainESMURL);
    assert.strictEqual(module.default, 2);
  }
  {
    const plainESMURL = `data:text/javascript,${encodeURIComponent(`import ${JSON.stringify(fixtures.fileURL('es-module-url', 'empty.js'))}`)}`;
    await import(plainESMURL);
  }
  {
    const plainESMURL = 'data:text/javascript,var x = "hello world?"';
    await import(plainESMURL);
  }
})().then(common.mustCall());
