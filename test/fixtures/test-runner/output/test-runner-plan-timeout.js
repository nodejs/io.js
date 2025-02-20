'use strict';
const { describe, it } = require('node:test');
const { platformTimeout } = require('../../../common');

describe('planning with wait', () => {
  it('planning with wait and passing', async (t) => {
    t.plan(1, { wait: platformTimeout(5000) });

    const asyncActivity = () => {
      setTimeout(() => {
        t.assert.ok(true);
      }, platformTimeout(250));
    };

    asyncActivity();
  });

  it('planning with wait and failing', async (t) => {
    t.plan(1, { wait: platformTimeout(5000) });

    const asyncActivity = () => {
      setTimeout(() => {
        t.assert.ok(false);
      }, platformTimeout(250));
    };

    asyncActivity();
  });

  it('planning wait time expires before plan is met', async (t) => {
    t.plan(2, { wait: platformTimeout(500) });

    const asyncActivity = () => {
      setTimeout(() => {
        t.assert.ok(true);
      }, platformTimeout(50_000_000));
    };

    asyncActivity();
  });

  it(`planning with wait "options.wait : true" and passing`, async (t) => {
    t.plan(1, { wait: true });

    const asyncActivity = () => {
      setTimeout(() => {
        t.assert.ok(true);
      }, platformTimeout(250));
    };

    asyncActivity();
  });

  it(`planning with wait "options.wait : true" and failing`, async (t) => {
    t.plan(1, { wait: true });

    const asyncActivity = () => {
      setTimeout(() => {
        t.assert.ok(false);
      }, platformTimeout(250));
    };

    asyncActivity();
  });

  it(`planning with wait "options.wait : false" should not wait`, async (t) => {
    t.plan(1, { wait: false });

    const asyncActivity = () => {
      setTimeout(() => {
        t.assert.ok(true);
      }, platformTimeout(500_000));
    };

    asyncActivity();
  })
});
