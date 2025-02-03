'use strict';

require('../common');
const assert = require('assert');
const { registerHooks } = require('module');

// This tests that the source in the load hook can be returned as
// array buffers or array buffer views.
const arrayBufferSource = 'module.exports = "arrayBuffer"';
const arrayBufferViewSource = 'module.exports = "arrayBufferView"';

const encoder = new TextEncoder();

const hook1 = registerHooks({
  resolve(specifier, context, nextResolve) {
    return { shortCircuit: true, url: `test://${specifier}` };
  },
  load(url, context, nextLoad) {
    const result = nextLoad(url, context);
    if (url === 'test://array_buffer') {
      assert.deepStrictEqual(result.source, encoder.encode(arrayBufferSource).buffer);
    } else if (url === 'test://array_buffer_view') {
      assert.deepStrictEqual(result.source, encoder.encode(arrayBufferViewSource));
    }
    return result;
  },
});

const hook2 = registerHooks({
  load(url, context, nextLoad) {
    if (url === 'test://array_buffer') {
      return {
        shortCircuit: true,
        source: encoder.encode(arrayBufferSource).buffer,
      };
    } else if (url === 'test://array_buffer_view') {
      return {
        shortCircuit: true,
        source: encoder.encode(arrayBufferViewSource),
      };
    }
    assert.fail('unreachable');
  },
});

assert.strictEqual(require('array_buffer'), 'arrayBuffer');
assert.strictEqual(require('array_buffer_view'), 'arrayBufferView');

hook1.deregister();
hook2.deregister();
