'use strict';
const { test } = require('node:test');
const { Readable } = require('node:stream');

test('test planning basic', (t) => {
  t.plan(2);
  t.assert.ok(true);
  t.assert.ok(true);
});

test('less assertions than planned', (t) => {
  t.plan(1);
});

test('more assertions than planned', (t) => {
  t.plan(1);
  t.assert.ok(true);
  t.assert.ok(true);
});

test('subtesting', (t) => {
  t.plan(1);
  t.test('subtest', () => { });
});

test('subtesting correctly', (t) => {
  t.plan(2);
  t.assert.ok(true);
  t.test('subtest', (st) => {
    st.plan(1);
    st.assert.ok(true);
  });
});

test('correctly ignoring subtesting plan', (t) => {
  t.plan(1);
  t.test('subtest', (st) => {
    st.plan(1);
    st.assert.ok(true);
  });
});

test('failing planning by options', { plan: 1 }, () => {
});

test('not failing planning by options', { plan: 1 }, (t) => {
  t.assert.ok(true);
});

test('subtest planning by options', (t) => {
  t.test('subtest', { plan: 1 }, (st) => {
    st.assert.ok(true);
  });
});

test('failing more assertions than planned', (t) => {
  t.plan(2);
  t.assert.ok(true);
  t.assert.ok(true);
  t.assert.ok(true);
});

test('planning with streams', (t, done) => {
  function* generate() {
    yield 'a';
    yield 'b';
    yield 'c';
  }
  const expected = ['a', 'b', 'c'];
  t.plan(expected.length);
  const stream = Readable.from(generate());
  stream.on('data', (chunk) => {
    t.assert.strictEqual(chunk, expected.shift());
  });

  stream.on('end', () => {
    done();
  });
})
