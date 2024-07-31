'use strict';

// Flags: --experimental-vm-modules

const common = require('../common');
const { SyntheticModule, SourceTextModule } = require('vm');
const assert = require('assert');

(async () => {
  {
    const s = new SyntheticModule(['x'], () => {
      s.setExport('x', 1);
    });

    const m = new SourceTextModule(`
    import { x } from 'synthetic';

    export const getX = () => x;
    `);

    await m.link(() => s);
    await m.evaluate();

    assert.strictEqual(m.namespace.getX(), 1);
    s.setExport('x', 42);
    assert.strictEqual(m.namespace.getX(), 42);
  }

  {
    const s = new SyntheticModule([], () => {
      const p = Promise.reject();
      p.catch(() => {});
      return p;
    });

    await s.link(common.mustNotCall());
    assert.strictEqual(await s.evaluate(), undefined);
  }

  for (const invalidName of [1, Symbol.iterator, {}, [], null, true, 0]) {
    const s = new SyntheticModule([], () => {});
    await s.link(() => {});
    assert.throws(() => {
      s.setExport(invalidName, undefined);
    }, {
      name: 'TypeError',
    });
  }

  {
    const s = new SyntheticModule([], () => {});
    await s.link(() => {});
    assert.throws(() => {
      s.setExport('does not exist');
    }, {
      name: 'ReferenceError',
    });
  }

  {
    const s = new SyntheticModule([], () => {});
    assert.throws(() => {
      s.setExport('name', 'value');
    }, {
      code: 'ERR_VM_MODULE_STATUS',
    });
  }

  for (const value of [null, {}, SyntheticModule.prototype]) {
    assert.throws(() => {
      SyntheticModule.prototype.setExport.call(value, 'foo');
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /The "this" argument must be an instance of SyntheticModule/
    });
  }

})().then(common.mustCall());
