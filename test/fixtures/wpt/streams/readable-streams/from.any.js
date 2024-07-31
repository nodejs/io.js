// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
'use strict';

const iterableFactories = [
  ['an array of values', () => {
    return ['a', 'b'];
  }],

  ['an array of promises', () => {
    return [
      Promise.resolve('a'),
      Promise.resolve('b')
    ];
  }],

  ['an array iterator', () => {
    return ['a', 'b'][Symbol.iterator]();
  }],

  ['a string', () => {
    // This iterates over the code points of the string.
    return 'ab';
  }],

  ['a Set', () => {
    return new Set(['a', 'b']);
  }],

  ['a Set iterator', () => {
    return new Set(['a', 'b'])[Symbol.iterator]();
  }],

  ['a sync generator', () => {
    function* syncGenerator() {
      yield 'a';
      yield 'b';
    }

    return syncGenerator();
  }],

  ['an async generator', () => {
    async function* asyncGenerator() {
      yield 'a';
      yield 'b';
    }

    return asyncGenerator();
  }],

  ['a sync iterable of values', () => {
    const chunks = ['a', 'b'];
    const it = {
      next() {
        return {
          done: chunks.length === 0,
          value: chunks.shift()
        };
      },
      [Symbol.iterator]: () => it
    };
    return it;
  }],

  ['a sync iterable of promises', () => {
    const chunks = ['a', 'b'];
    const it = {
      next() {
        return chunks.length === 0 ? { done: true } : {
          done: false,
          value: Promise.resolve(chunks.shift())
        };
      },
      [Symbol.iterator]: () => it
    };
    return it;
  }],

  ['an async iterable', () => {
    const chunks = ['a', 'b'];
    const it = {
      next() {
        return Promise.resolve({
          done: chunks.length === 0,
          value: chunks.shift()
        })
      },
      [Symbol.asyncIterator]: () => it
    };
    return it;
  }],

  ['a ReadableStream', () => {
    return new ReadableStream({
      start(c) {
        c.enqueue('a');
        c.enqueue('b');
        c.close();
      }
    });
  }],

  ['a ReadableStream async iterator', () => {
    return new ReadableStream({
      start(c) {
        c.enqueue('a');
        c.enqueue('b');
        c.close();
      }
    })[Symbol.asyncIterator]();
  }]
];

for (const [label, factory] of iterableFactories) {
  promise_test(async () => {

    const iterable = factory();
    const rs = ReadableStream.from(iterable);
    assert_equals(rs.constructor, ReadableStream, 'from() should return a ReadableStream');

    const reader = rs.getReader();
    assert_object_equals(await reader.read(), { value: 'a', done: false }, 'first read should be correct');
    assert_object_equals(await reader.read(), { value: 'b', done: false }, 'second read should be correct');
    assert_object_equals(await reader.read(), { value: undefined, done: true }, 'third read should be done');
    await reader.closed;

  }, `ReadableStream.from accepts ${label}`);
}

const badIterables = [
  ['null', null],
  ['undefined', undefined],
  ['0', 0],
  ['NaN', NaN],
  ['true', true],
  ['{}', {}],
  ['Object.create(null)', Object.create(null)],
  ['a function', () => 42],
  ['a symbol', Symbol()],
  ['an object with a non-callable @@iterator method', {
    [Symbol.iterator]: 42
  }],
  ['an object with a non-callable @@asyncIterator method', {
    [Symbol.asyncIterator]: 42
  }],
];

for (const [label, iterable] of badIterables) {
  test(() => {
    assert_throws_js(TypeError, () => ReadableStream.from(iterable), 'from() should throw a TypeError')
  }, `ReadableStream.from throws on invalid iterables; specifically ${label}`);
}

test(() => {
  const theError = new Error('a unique string');
  const iterable = {
    [Symbol.iterator]() {
      throw theError;
    }
  };

  assert_throws_exactly(theError, () => ReadableStream.from(iterable), 'from() should re-throw the error');
}, `ReadableStream.from re-throws errors from calling the @@iterator method`);

test(() => {
  const theError = new Error('a unique string');
  const iterable = {
    [Symbol.asyncIterator]() {
      throw theError;
    }
  };

  assert_throws_exactly(theError, () => ReadableStream.from(iterable), 'from() should re-throw the error');
}, `ReadableStream.from re-throws errors from calling the @@asyncIterator method`);

test(t => {
  const theError = new Error('a unique string');
  const iterable = {
    [Symbol.iterator]: t.unreached_func('@@iterator should not be called'),
    [Symbol.asyncIterator]() {
      throw theError;
    }
  };

  assert_throws_exactly(theError, () => ReadableStream.from(iterable), 'from() should re-throw the error');
}, `ReadableStream.from ignores @@iterator if @@asyncIterator exists`);

promise_test(async () => {

  const iterable = {
    async next() {
      return { value: undefined, done: true };
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  const reader = rs.getReader();

  const read = await reader.read();
  assert_object_equals(read, { value: undefined, done: true }, 'first read should be done');

  await reader.closed;

}, `ReadableStream.from accepts an empty iterable`);

promise_test(async t => {

  const theError = new Error('a unique string');

  const iterable = {
    async next() {
      throw theError;
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  const reader = rs.getReader();

  await Promise.all([
    promise_rejects_exactly(t, theError, reader.read()),
    promise_rejects_exactly(t, theError, reader.closed)
  ]);

}, `ReadableStream.from: stream errors when next() rejects`);

promise_test(async t => {

  const iterable = {
    next() {
      return new Promise(() => {});
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  const reader = rs.getReader();

  await Promise.race([
    reader.read().then(t.unreached_func('read() should not resolve'), t.unreached_func('read() should not reject')),
    reader.closed.then(t.unreached_func('closed should not resolve'), t.unreached_func('closed should not reject')),
    flushAsyncEvents()
  ]);

}, 'ReadableStream.from: stream stalls when next() never settles');

promise_test(async () => {

  let nextCalls = 0;
  let nextArgs;
  const iterable = {
    async next(...args) {
      nextCalls += 1;
      nextArgs = args;
      return { value: 'a', done: false };
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  const reader = rs.getReader();

  await flushAsyncEvents();
  assert_equals(nextCalls, 0, 'next() should not be called yet');

  const read = await reader.read();
  assert_object_equals(read, { value: 'a', done: false }, 'first read should be correct');
  assert_equals(nextCalls, 1, 'next() should be called after first read()');
  assert_array_equals(nextArgs, [], 'next() should be called with no arguments');

}, `ReadableStream.from: calls next() after first read()`);

promise_test(async t => {

  const theError = new Error('a unique string');

  let returnCalls = 0;
  let returnArgs;
  let resolveReturn;
  const iterable = {
    next: t.unreached_func('next() should not be called'),
    throw: t.unreached_func('throw() should not be called'),
    async return(...args) {
      returnCalls += 1;
      returnArgs = args;
      await new Promise(r => resolveReturn = r);
      return { done: true };
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  const reader = rs.getReader();
  assert_equals(returnCalls, 0, 'return() should not be called yet');

  let cancelResolved = false;
  const cancelPromise = reader.cancel(theError).then(() => {
    cancelResolved = true;
  });

  await flushAsyncEvents();
  assert_equals(returnCalls, 1, 'return() should be called');
  assert_array_equals(returnArgs, [theError], 'return() should be called with cancel reason');
  assert_false(cancelResolved, 'cancel() should not resolve while promise from return() is pending');

  resolveReturn();
  await Promise.all([
    cancelPromise,
    reader.closed
  ]);

}, `ReadableStream.from: cancelling the returned stream calls and awaits return()`);

promise_test(async t => {

  let nextCalls = 0;
  let returnCalls = 0;

  const iterable = {
    async next() {
      nextCalls += 1;
      return { value: undefined, done: true };
    },
    throw: t.unreached_func('throw() should not be called'),
    async return() {
      returnCalls += 1;
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  const reader = rs.getReader();

  const read = await reader.read();
  assert_object_equals(read, { value: undefined, done: true }, 'first read should be done');
  assert_equals(nextCalls, 1, 'next() should be called once');

  await reader.closed;
  assert_equals(returnCalls, 0, 'return() should not be called');

}, `ReadableStream.from: return() is not called when iterator completes normally`);

promise_test(async t => {

  const theError = new Error('a unique string');

  const iterable = {
    next: t.unreached_func('next() should not be called'),
    throw: t.unreached_func('throw() should not be called'),
    async return() {
      return 42;
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  const reader = rs.getReader();

  await promise_rejects_js(t, TypeError, reader.cancel(theError), 'cancel() should reject with a TypeError');

  await reader.closed;

}, `ReadableStream.from: cancel() rejects when return() fulfills with a non-object`);

promise_test(async () => {

  let nextCalls = 0;
  let reader;
  let values = ['a', 'b', 'c'];

  const iterable = {
    async next() {
      nextCalls += 1;
      if (nextCalls === 1) {
        reader.read();
      }
      return { value: values.shift(), done: false };
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  reader = rs.getReader();

  const read1 = await reader.read();
  assert_object_equals(read1, { value: 'a', done: false }, 'first read should be correct');
  await flushAsyncEvents();
  assert_equals(nextCalls, 2, 'next() should be called two times');

  const read2 = await reader.read();
  assert_object_equals(read2, { value: 'c', done: false }, 'second read should be correct');
  assert_equals(nextCalls, 3, 'next() should be called three times');

}, `ReadableStream.from: reader.read() inside next()`);

promise_test(async () => {

  let nextCalls = 0;
  let returnCalls = 0;
  let reader;

  const iterable = {
    async next() {
      nextCalls++;
      await reader.cancel();
      assert_equals(returnCalls, 1, 'return() should be called once');
      return { value: 'something else', done: false };
    },
    async return() {
      returnCalls++;
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  reader = rs.getReader();

  const read = await reader.read();
  assert_object_equals(read, { value: undefined, done: true }, 'first read should be done');
  assert_equals(nextCalls, 1, 'next() should be called once');

  await reader.closed;

}, `ReadableStream.from: reader.cancel() inside next()`);

promise_test(async t => {

  let returnCalls = 0;
  let reader;

  const iterable = {
    next: t.unreached_func('next() should not be called'),
    async return() {
      returnCalls++;
      await reader.cancel();
      return { done: true };
    },
    [Symbol.asyncIterator]: () => iterable
  };

  const rs = ReadableStream.from(iterable);
  reader = rs.getReader();

  await reader.cancel();
  assert_equals(returnCalls, 1, 'return() should be called once');

  await reader.closed;

}, `ReadableStream.from: reader.cancel() inside return()`);

promise_test(async t => {

  let array = ['a', 'b'];

  const rs = ReadableStream.from(array);
  const reader = rs.getReader();

  const read1 = await reader.read();
  assert_object_equals(read1, { value: 'a', done: false }, 'first read should be correct');
  const read2 = await reader.read();
  assert_object_equals(read2, { value: 'b', done: false }, 'second read should be correct');

  array.push('c');

  const read3 = await reader.read();
  assert_object_equals(read3, { value: 'c', done: false }, 'third read after push() should be correct');
  const read4 = await reader.read();
  assert_object_equals(read4, { value: undefined, done: true }, 'fourth read should be done');

  await reader.closed;

}, `ReadableStream.from(array), push() to array while reading`);
