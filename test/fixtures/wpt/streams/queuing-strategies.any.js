// META: global=window,worker,shadowrealm
'use strict';

const highWaterMarkConversions = new Map([
  [-Infinity, -Infinity],
  [-5, -5],
  [false, 0],
  [true, 1],
  [NaN, NaN],
  ['foo', NaN],
  ['0', 0],
  [{}, NaN],
  [() => {}, NaN]
]);

for (const QueuingStrategy of [CountQueuingStrategy, ByteLengthQueuingStrategy]) {
  test(() => {
    new QueuingStrategy({ highWaterMark: 4 });
  }, `${QueuingStrategy.name}: Can construct a with a valid high water mark`);

  test(() => {
    const highWaterMark = 1;
    const highWaterMarkObjectGetter = {
      get highWaterMark() { return highWaterMark; }
    };
    const error = new Error('wow!');
    const highWaterMarkObjectGetterThrowing = {
      get highWaterMark() { throw error; }
    };

    assert_throws_js(TypeError, () => new QueuingStrategy(), 'construction fails with undefined');
    assert_throws_js(TypeError, () => new QueuingStrategy(null), 'construction fails with null');
    assert_throws_js(TypeError, () => new QueuingStrategy(true), 'construction fails with true');
    assert_throws_js(TypeError, () => new QueuingStrategy(5), 'construction fails with 5');
    assert_throws_js(TypeError, () => new QueuingStrategy({}), 'construction fails with {}');
    assert_throws_exactly(error, () => new QueuingStrategy(highWaterMarkObjectGetterThrowing),
      'construction fails with an object with a throwing highWaterMark getter');

    assert_equals((new QueuingStrategy(highWaterMarkObjectGetter)).highWaterMark, highWaterMark);
  }, `${QueuingStrategy.name}: Constructor behaves as expected with strange arguments`);

  test(() => {
    for (const [input, output] of highWaterMarkConversions.entries()) {
      const strategy = new QueuingStrategy({ highWaterMark: input });
      assert_equals(strategy.highWaterMark, output, `${input} gets set correctly`);
    }
  }, `${QueuingStrategy.name}: highWaterMark constructor values are converted per the unrestricted double rules`);

  test(() => {
    const size1 = (new QueuingStrategy({ highWaterMark: 5 })).size;
    const size2 = (new QueuingStrategy({ highWaterMark: 10 })).size;

    assert_equals(size1, size2);
  }, `${QueuingStrategy.name}: size is the same function across all instances`);

  test(() => {
    const size = (new QueuingStrategy({ highWaterMark: 5 })).size;
    assert_equals(size.name, 'size');
  }, `${QueuingStrategy.name}: size should have the right name`);

  test(() => {
    class SubClass extends QueuingStrategy {
      size() {
        return 2;
      }

      subClassMethod() {
        return true;
      }
    }

    const sc = new SubClass({ highWaterMark: 77 });
    assert_equals(sc.constructor.name, 'SubClass', 'constructor.name should be correct');
    assert_equals(sc.highWaterMark, 77, 'highWaterMark should come from the parent class');
    assert_equals(sc.size(), 2, 'size() on the subclass should override the parent');
    assert_true(sc.subClassMethod(), 'subClassMethod() should work');
  }, `${QueuingStrategy.name}: subclassing should work correctly`);

  test(() => {
    const size = new QueuingStrategy({ highWaterMark: 5 }).size;
    assert_false('prototype' in size);
  }, `${QueuingStrategy.name}: size should not have a prototype property`);
}

test(() => {
  const size = new CountQueuingStrategy({ highWaterMark: 5 }).size;
  assert_throws_js(TypeError, () => new size());
}, `CountQueuingStrategy: size should not be a constructor`);

test(() => {
  const size = new ByteLengthQueuingStrategy({ highWaterMark: 5 }).size;
  assert_throws_js(TypeError, () => new size({ byteLength: 1024 }));
}, `ByteLengthQueuingStrategy: size should not be a constructor`);

test(() => {
  const size = (new CountQueuingStrategy({ highWaterMark: 5 })).size;
  assert_equals(size.length, 0);
}, 'CountQueuingStrategy: size should have the right length');

test(() => {
  const size = (new ByteLengthQueuingStrategy({ highWaterMark: 5 })).size;
  assert_equals(size.length, 1);
}, 'ByteLengthQueuingStrategy: size should have the right length');

test(() => {
  const size = 1024;
  const chunk = { byteLength: size };
  const chunkGetter = {
    get byteLength() { return size; }
  };
  const error = new Error('wow!');
  const chunkGetterThrowing = {
    get byteLength() { throw error; }
  };

  const sizeFunction = (new CountQueuingStrategy({ highWaterMark: 5 })).size;

  assert_equals(sizeFunction(), 1, 'size returns 1 with undefined');
  assert_equals(sizeFunction(null), 1, 'size returns 1 with null');
  assert_equals(sizeFunction('potato'), 1, 'size returns 1 with non-object type');
  assert_equals(sizeFunction({}), 1, 'size returns 1 with empty object');
  assert_equals(sizeFunction(chunk), 1, 'size returns 1 with a chunk');
  assert_equals(sizeFunction(chunkGetter), 1, 'size returns 1 with chunk getter');
  assert_equals(sizeFunction(chunkGetterThrowing), 1,
    'size returns 1 with chunk getter that throws');
}, 'CountQueuingStrategy: size behaves as expected with strange arguments');

test(() => {
  const size = 1024;
  const chunk = { byteLength: size };
  const chunkGetter = {
    get byteLength() { return size; }
  };
  const error = new Error('wow!');
  const chunkGetterThrowing = {
    get byteLength() { throw error; }
  };

  const sizeFunction = (new ByteLengthQueuingStrategy({ highWaterMark: 5 })).size;

  assert_throws_js(TypeError, () => sizeFunction(), 'size fails with undefined');
  assert_throws_js(TypeError, () => sizeFunction(null), 'size fails with null');
  assert_equals(sizeFunction('potato'), undefined, 'size succeeds with undefined with a random non-object type');
  assert_equals(sizeFunction({}), undefined, 'size succeeds with undefined with an object without hwm property');
  assert_equals(sizeFunction(chunk), size, 'size succeeds with the right amount with an object with a hwm');
  assert_equals(sizeFunction(chunkGetter), size,
    'size succeeds with the right amount with an object with a hwm getter');
  assert_throws_exactly(error, () => sizeFunction(chunkGetterThrowing),
    'size fails with the error thrown by the getter');
}, 'ByteLengthQueuingStrategy: size behaves as expected with strange arguments');
