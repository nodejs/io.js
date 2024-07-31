'use strict';
const common = require('../../../common');
const assert = require('assert');
const { test, describe, it, before, after, beforeEach, afterEach } = require('node:test');
const { setTimeout } = require('node:timers/promises');

before((t) => t.diagnostic('before 1 called'));
after((t) => t.diagnostic('after 1 called'));

describe('describe hooks', () => {
  const testArr = [];
  before(function() {
    testArr.push('before ' + this.name);
  });
  after(common.mustCall(function() {
    testArr.push('after ' + this.name);
    assert.deepStrictEqual(testArr, [
      'before describe hooks',
      'beforeEach 1', '1', 'afterEach 1',
      'beforeEach 2', '2', 'afterEach 2',
      'before nested',
      'beforeEach nested 1', '+beforeEach nested 1', 'nested 1', '+afterEach nested 1', 'afterEach nested 1',
      'beforeEach nested 2', '+beforeEach nested 2', 'nested 2', '+afterEach nested 2', 'afterEach nested 2',
      'after nested',
      'after describe hooks',
    ]);
  }));
  beforeEach(function() {
    testArr.push('beforeEach ' + this.name);
  });
  afterEach(function() {
    testArr.push('afterEach ' + this.name);
  });

  it('1', () => testArr.push('1'));
  test('2', () => testArr.push('2'));

  describe('nested', () => {
    before(function() {
      testArr.push('before ' + this.name);
    });
    after(function() {
      testArr.push('after ' + this.name);
    });
    beforeEach(function() {
      testArr.push('+beforeEach ' + this.name);
    });
    afterEach(function() {
      testArr.push('+afterEach ' + this.name);
    });
    it('nested 1', () => testArr.push('nested 1'));
    test('nested 2', () => testArr.push('nested 2'));
  });
});

describe('describe hooks - no subtests', () => {
  const testArr = [];
  before(function() {
    testArr.push('before ' + this.name);
  });
  after(common.mustCall(function() {
    testArr.push('after ' + this.name);
    assert.deepStrictEqual(testArr, [
      'before describe hooks - no subtests',
      'after describe hooks - no subtests',
    ]);
  }));
  beforeEach(common.mustNotCall());
  afterEach(common.mustNotCall());
});

describe('before throws', () => {
  before(() => { throw new Error('before'); });
  it('1', () => {});
  test('2', () => {});
});

describe('before throws - no subtests', () => {
  before(() => { throw new Error('before'); });
  after(common.mustCall());
});

describe('after throws', () => {
  after(() => { throw new Error('after'); });
  it('1', () => {});
  test('2', () => {});
});

describe('after throws - no subtests', () => {
  after(() => { throw new Error('after'); });
});

describe('beforeEach throws', () => {
  beforeEach(() => { throw new Error('beforeEach'); });
  it('1', () => {});
  test('2', () => {});
});

describe('afterEach throws', () => {
  afterEach(() => { throw new Error('afterEach'); });
  it('1', () => {});
  test('2', () => {});
});

describe('afterEach when test fails', () => {
  afterEach(common.mustCall(2));
  it('1', () => { throw new Error('test'); });
  test('2', () => {});
});

describe('afterEach throws and test fails', () => {
  afterEach(() => { throw new Error('afterEach'); });
  it('1', () => { throw new Error('test'); });
  test('2', () => {});
});

test('test hooks', async (t) => {
  const testArr = [];

  t.before((t) => testArr.push('before ' + t.name));
  t.after(common.mustCall((t) => testArr.push('after ' + t.name)));
  t.beforeEach((t) => testArr.push('beforeEach ' + t.name));
  t.afterEach((t) => testArr.push('afterEach ' + t.name));
  await t.test('1', () => testArr.push('1'));
  await t.test('2', () => testArr.push('2'));

  await t.test('nested', async (t) => {
    t.before((t) => testArr.push('nested before ' + t.name));
    t.after((t) => testArr.push('nested after ' + t.name));
    t.beforeEach((t) => testArr.push('nested beforeEach ' + t.name));
    t.afterEach((t) => testArr.push('nested afterEach ' + t.name));
    await t.test('nested 1', () => testArr.push('nested1'));
    await t.test('nested 2', () => testArr.push('nested 2'));
  });

  t.after(common.mustCall(() => {
    assert.deepStrictEqual(testArr, [
      'before test hooks',
      'beforeEach 1', '1', 'afterEach 1',
      'beforeEach 2', '2', 'afterEach 2',
      'beforeEach nested',
      'nested before nested',
      'beforeEach nested 1', 'nested beforeEach nested 1', 'nested1', 'nested afterEach nested 1', 'afterEach nested 1',
      'beforeEach nested 2', 'nested beforeEach nested 2', 'nested 2', 'nested afterEach nested 2', 'afterEach nested 2',
      'afterEach nested',
      'nested after nested',
      'after test hooks',
    ]);
  }));
});

test('test hooks - no subtests', async (t) => {
  const testArr = [];

  t.before((t) => testArr.push('before ' + t.name));
  t.after(common.mustCall((t) => testArr.push('after ' + t.name)));
  t.beforeEach(common.mustNotCall());
  t.afterEach(common.mustNotCall());

  t.after(common.mustCall(() => {
    assert.deepStrictEqual(testArr, [
      'before test hooks - no subtests',
      'after test hooks - no subtests',
    ]);
  }));
});

test('t.before throws', async (t) => {
  t.after(common.mustCall());
  t.before(() => { throw new Error('before'); });
  await t.test('1', () => {});
  await t.test('2', () => {});
});

test('t.before throws - no subtests', async (t) => {
  t.after(common.mustCall());
  t.before(() => { throw new Error('before'); });
});

test('t.after throws', async (t) => {
  t.before(common.mustCall());
  t.after(() => { throw new Error('after'); });
  await t.test('1', () => {});
  await t.test('2', () => {});
});

test('t.after throws - no subtests', async (t) => {
  t.before(common.mustCall());
  t.after(() => { throw new Error('after'); });
});


test('t.beforeEach throws', async (t) => {
  t.after(common.mustCall());
  t.beforeEach(() => { throw new Error('beforeEach'); });
  await t.test('1', () => {});
  await t.test('2', () => {});
});

test('t.afterEach throws', async (t) => {
  t.after(common.mustCall());
  t.afterEach(() => { throw new Error('afterEach'); });
  await t.test('1', () => {});
  await t.test('2', () => {});
});


test('afterEach when test fails', async (t) => {
  t.after(common.mustCall());
  t.afterEach(common.mustCall(2));
  await t.test('1', () => { throw new Error('test'); });
  await t.test('2', () => {});
});

test('afterEach context when test passes', async (t) => {
  t.afterEach(common.mustCall((ctx) => {
    assert.strictEqual(ctx.name, '1');
    assert.strictEqual(ctx.passed, true);
    assert.strictEqual(ctx.error, null);
  }));
  await t.test('1', () => {});
});

test('afterEach context when test fails', async (t) => {
  const err = new Error('test');
  t.afterEach(common.mustCall((ctx) => {
    assert.strictEqual(ctx.name, '1');
    assert.strictEqual(ctx.passed, false);
    assert.strictEqual(ctx.error, err);
  }));
  await t.test('1', () => { throw err });
});

test('afterEach throws and test fails', async (t) => {
  t.after(common.mustCall());
  t.afterEach(() => { throw new Error('afterEach'); });
  await t.test('1', () => { throw new Error('test'); });
  await t.test('2', () => {});
});

test('t.after() is called if test body throws', (t) => {
  t.after(() => {
    t.diagnostic('- after() called');
  });
  throw new Error('bye');
});

describe('run after when before throws', () => {
  after(common.mustCall(() => {
    console.log("- after() called")
  }));
  before(() => { throw new Error('before')});
  it('1', () => {});
});


test('test hooks - async', async (t) => {
  const testArr = [];

  t.before(async (t) => {
    testArr.push('before starting ' + t.name);
    await setTimeout(10);
    testArr.push('before ending ' + t.name);
  });
  t.after(async (t) => {
    testArr.push('after starting ' + t.name);
    await setTimeout(10);
    testArr.push('after ending ' + t.name);
  });
  t.beforeEach(async (t) => {
    testArr.push('beforeEach starting ' + t.name);
    await setTimeout(10);
    testArr.push('beforeEach ending ' + t.name);
  });
  t.afterEach(async (t) => {
    testArr.push('afterEach starting ' + t.name);
    await setTimeout(10);
    testArr.push('afterEach ending ' + t.name);
  });
  await t.test('1', async () => {
    testArr.push('1 starting');
    await setTimeout(10);
    testArr.push('1 ending');
  });
  await t.test('2', async () => {
    testArr.push('2 starting');
    await setTimeout(10);
    testArr.push('2 ending');
  });

  t.after(common.mustCall(() => {
    assert.deepStrictEqual(testArr, [
      'before starting test hooks - async', 'before ending test hooks - async',
      'beforeEach starting 1', 'beforeEach ending 1',
      '1 starting', '1 ending',
      'afterEach starting 1', 'afterEach ending 1',
      'beforeEach starting 2', 'beforeEach ending 2',
      '2 starting', '2 ending',
      'afterEach starting 2', 'afterEach ending 2',
      'after starting test hooks - async', 'after ending test hooks - async',
    ]);
  }));
});

before((t) => t.diagnostic('before 2 called'));
after((t) => t.diagnostic('after 2 called'));
