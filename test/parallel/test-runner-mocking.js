'use strict';
const common = require('../common');
const assert = require('node:assert');
const { mock, test } = require('node:test');
test('spies on a function', (t) => {
  const sum = t.mock.fn((arg1, arg2) => {
    return arg1 + arg2;
  });

  assert.strictEqual(sum.mock.calls.length, 0);
  assert.strictEqual(sum(3, 4), 7);
  assert.strictEqual(sum.call(1000, 9, 1), 10);
  assert.strictEqual(sum.mock.calls.length, 2);

  let call = sum.mock.calls[0];
  assert.deepStrictEqual(call.arguments, [3, 4]);
  assert.strictEqual(call.error, undefined);
  assert.strictEqual(call.result, 7);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, undefined);

  call = sum.mock.calls[1];
  assert.deepStrictEqual(call.arguments, [9, 1]);
  assert.strictEqual(call.error, undefined);
  assert.strictEqual(call.result, 10);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, 1000);
});

test('spies on a bound function', (t) => {
  const bound = function(arg1, arg2) {
    return this + arg1 + arg2;
  }.bind(50);
  const sum = t.mock.fn(bound);

  assert.strictEqual(sum.mock.calls.length, 0);
  assert.strictEqual(sum(3, 4), 57);
  assert.strictEqual(sum(9, 1), 60);
  assert.strictEqual(sum.mock.calls.length, 2);

  let call = sum.mock.calls[0];
  assert.deepStrictEqual(call.arguments, [3, 4]);
  assert.strictEqual(call.result, 57);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, undefined);

  call = sum.mock.calls[1];
  assert.deepStrictEqual(call.arguments, [9, 1]);
  assert.strictEqual(call.result, 60);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, undefined);
});

test('spies on a constructor', (t) => {
  class ParentClazz {
    constructor(c) {
      this.c = c;
    }
  }

  class Clazz extends ParentClazz {
    #privateValue;

    constructor(a, b) {
      super(a + b);
      this.a = a;
      this.#privateValue = b;
    }

    getPrivateValue() {
      return this.#privateValue;
    }
  }

  const ctor = t.mock.fn(Clazz);
  const instance = new ctor(42, 85);

  assert(instance instanceof Clazz);
  assert(instance instanceof ParentClazz);
  assert.strictEqual(instance.a, 42);
  assert.strictEqual(instance.getPrivateValue(), 85);
  assert.strictEqual(instance.c, 127);
  assert.strictEqual(ctor.mock.calls.length, 1);

  const call = ctor.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [42, 85]);
  assert.strictEqual(call.error, undefined);
  assert.strictEqual(call.result, instance);
  assert.strictEqual(call.target, Clazz);
  assert.strictEqual(call.this, instance);
});

test('a no-op spy function is created by default', (t) => {
  const fn = t.mock.fn();

  assert.strictEqual(fn.mock.calls.length, 0);
  assert.strictEqual(fn(3, 4), undefined);
  assert.strictEqual(fn.mock.calls.length, 1);

  const call = fn.mock.calls[0];
  assert.deepStrictEqual(call.arguments, [3, 4]);
  assert.strictEqual(call.result, undefined);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, undefined);
});

test('internal no-op function can be reused', (t) => {
  const fn1 = t.mock.fn();
  fn1.prop = true;
  const fn2 = t.mock.fn();

  fn1(1);
  fn2(2);
  fn1(3);

  assert.notStrictEqual(fn1.mock, fn2.mock);
  assert.strictEqual(fn1.mock.calls.length, 2);
  assert.strictEqual(fn2.mock.calls.length, 1);
  assert.strictEqual(fn1.prop, true);
  assert.strictEqual(fn2.prop, undefined);
});

test('functions can be mocked multiple times at once', (t) => {
  function sum(a, b) {
    return a + b;
  }

  function difference(a, b) {
    return a - b;
  }

  function product(a, b) {
    return a * b;
  }

  const fn1 = t.mock.fn(sum, difference);
  const fn2 = t.mock.fn(sum, product);

  assert.strictEqual(fn1(5, 3), 2);
  assert.strictEqual(fn2(5, 3), 15);
  assert.strictEqual(fn2(4, 2), 8);
  assert(!('mock' in sum));
  assert(!('mock' in difference));
  assert(!('mock' in product));
  assert.notStrictEqual(fn1.mock, fn2.mock);
  assert.strictEqual(fn1.mock.calls.length, 1);
  assert.strictEqual(fn2.mock.calls.length, 2);
});

test('internal no-op function can be reused as methods', (t) => {
  const obj = {
    _foo: 5,
    _bar: 9,
    foo() {
      return this._foo;
    },
    bar() {
      return this._bar;
    },
  };

  t.mock.method(obj, 'foo');
  obj.foo.prop = true;
  t.mock.method(obj, 'bar');
  assert.strictEqual(obj.foo(), 5);
  assert.strictEqual(obj.bar(), 9);
  assert.strictEqual(obj.bar(), 9);
  assert.notStrictEqual(obj.foo.mock, obj.bar.mock);
  assert.strictEqual(obj.foo.mock.calls.length, 1);
  assert.strictEqual(obj.bar.mock.calls.length, 2);
  assert.strictEqual(obj.foo.prop, true);
  assert.strictEqual(obj.bar.prop, undefined);
});

test('methods can be mocked multiple times but not at the same time', (t) => {
  const obj = {
    offset: 3,
    sum(a, b) {
      return this.offset + a + b;
    },
  };

  function difference(a, b) {
    return this.offset + (a - b);
  }

  function product(a, b) {
    return this.offset + a * b;
  }

  const originalSum = obj.sum;
  const fn1 = t.mock.method(obj, 'sum', difference);

  assert.strictEqual(obj.sum(5, 3), 5);
  assert.strictEqual(obj.sum(5, 1), 7);
  assert.strictEqual(obj.sum, fn1);
  assert.notStrictEqual(fn1.mock, undefined);
  assert.strictEqual(originalSum.mock, undefined);
  assert.strictEqual(difference.mock, undefined);
  assert.strictEqual(product.mock, undefined);
  assert.strictEqual(fn1.mock.calls.length, 2);

  const fn2 = t.mock.method(obj, 'sum', product);

  assert.strictEqual(obj.sum(5, 3), 18);
  assert.strictEqual(obj.sum, fn2);
  assert.notStrictEqual(fn1, fn2);
  assert.strictEqual(fn2.mock.calls.length, 1);

  obj.sum.mock.restore();
  assert.strictEqual(obj.sum, fn1);
  obj.sum.mock.restore();
  assert.strictEqual(obj.sum, originalSum);
  assert.strictEqual(obj.sum.mock, undefined);
});

test('spies on an object method', (t) => {
  const obj = {
    prop: 5,
    method(a, b) {
      return a + b + this.prop;
    },
  };

  assert.strictEqual(obj.method(1, 3), 9);
  t.mock.method(obj, 'method');
  assert.strictEqual(obj.method.mock.calls.length, 0);
  assert.strictEqual(obj.method(1, 3), 9);

  const call = obj.method.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [1, 3]);
  assert.strictEqual(call.result, 9);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(obj.method.mock.restore(), undefined);
  assert.strictEqual(obj.method(1, 3), 9);
  assert.strictEqual(obj.method.mock, undefined);
});

test('spies on a getter', (t) => {
  const obj = {
    prop: 5,
    get method() {
      return this.prop;
    },
  };

  assert.strictEqual(obj.method, 5);

  const getter = t.mock.method(obj, 'method', { getter: true });

  assert.strictEqual(getter.mock.calls.length, 0);
  assert.strictEqual(obj.method, 5);

  const call = getter.mock.calls[0];

  assert.deepStrictEqual(call.arguments, []);
  assert.strictEqual(call.result, 5);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(getter.mock.restore(), undefined);
  assert.strictEqual(obj.method, 5);
});

test('spies on a setter', (t) => {
  const obj = {
    prop: 100,
    // eslint-disable-next-line accessor-pairs
    set method(val) {
      this.prop = val;
    },
  };

  assert.strictEqual(obj.prop, 100);
  obj.method = 88;
  assert.strictEqual(obj.prop, 88);

  const setter = t.mock.method(obj, 'method', { setter: true });

  assert.strictEqual(setter.mock.calls.length, 0);
  obj.method = 77;
  assert.strictEqual(obj.prop, 77);
  assert.strictEqual(setter.mock.calls.length, 1);

  const call = setter.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [77]);
  assert.strictEqual(call.result, undefined);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(setter.mock.restore(), undefined);
  assert.strictEqual(obj.prop, 77);
  obj.method = 65;
  assert.strictEqual(obj.prop, 65);
});

test('spy functions can be bound', (t) => {
  const sum = t.mock.fn(function(arg1, arg2) {
    return this + arg1 + arg2;
  });
  const bound = sum.bind(1000);

  assert.strictEqual(bound(9, 1), 1010);
  assert.strictEqual(sum.mock.calls.length, 1);

  const call = sum.mock.calls[0];
  assert.deepStrictEqual(call.arguments, [9, 1]);
  assert.strictEqual(call.result, 1010);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, 1000);

  assert.strictEqual(sum.mock.restore(), undefined);
  assert.strictEqual(sum.bind(0)(2, 11), 13);
});

test('mocks prototype methods on an instance', async (t) => {
  class Runner {
    async someTask(msg) {
      return Promise.resolve(msg);
    }

    async method(msg) {
      await this.someTask(msg);
      return msg;
    }
  }
  const msg = 'ok';
  const obj = new Runner();
  assert.strictEqual(await obj.method(msg), msg);

  t.mock.method(obj, obj.someTask.name);
  assert.strictEqual(obj.someTask.mock.calls.length, 0);

  assert.strictEqual(await obj.method(msg), msg);

  const call = obj.someTask.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [msg]);
  assert.strictEqual(await call.result, msg);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  const obj2 = new Runner();
  // Ensure that a brand new instance is not mocked
  assert.strictEqual(
    obj2.someTask.mock,
    undefined
  );

  assert.strictEqual(obj.someTask.mock.restore(), undefined);
  assert.strictEqual(await obj.method(msg), msg);
  assert.strictEqual(obj.someTask.mock, undefined);
  assert.strictEqual(Runner.prototype.someTask.mock, undefined);
});

test('spies on async static class methods', async (t) => {
  class Runner {
    static async someTask(msg) {
      return Promise.resolve(msg);
    }

    static async method(msg) {
      await this.someTask(msg);
      return msg;
    }
  }
  const msg = 'ok';
  assert.strictEqual(await Runner.method(msg), msg);

  t.mock.method(Runner, Runner.someTask.name);
  assert.strictEqual(Runner.someTask.mock.calls.length, 0);

  assert.strictEqual(await Runner.method(msg), msg);

  const call = Runner.someTask.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [msg]);
  assert.strictEqual(await call.result, msg);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, Runner);

  assert.strictEqual(Runner.someTask.mock.restore(), undefined);
  assert.strictEqual(await Runner.method(msg), msg);
  assert.strictEqual(Runner.someTask.mock, undefined);
  assert.strictEqual(Runner.prototype.someTask, undefined);

});

test('given null to a mock.method it throws a invalid argument error', (t) => {
  assert.throws(() => t.mock.method(null, {}), { code: 'ERR_INVALID_ARG_TYPE' });
});

test('it should throw given an inexistent property on a object instance', (t) => {
  assert.throws(() => t.mock.method({ abc: 0 }, 'non-existent'), {
    code: 'ERR_INVALID_ARG_VALUE'
  });
});

test('spy functions can be used on classes inheritance', (t) => {
  // Makes sure that having a null-prototype doesn't throw our system off
  class A extends null {
    static someTask(msg) {
      return msg;
    }
    static method(msg) {
      return this.someTask(msg);
    }
  }
  class B extends A {}
  class C extends B {}

  const msg = 'ok';
  assert.strictEqual(C.method(msg), msg);

  t.mock.method(C, C.someTask.name);
  assert.strictEqual(C.someTask.mock.calls.length, 0);

  assert.strictEqual(C.method(msg), msg);

  const call = C.someTask.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [msg]);
  assert.strictEqual(call.result, msg);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, C);

  assert.strictEqual(C.someTask.mock.restore(), undefined);
  assert.strictEqual(C.method(msg), msg);
  assert.strictEqual(C.someTask.mock, undefined);
});

test('spy functions don\'t affect the prototype chain', (t) => {

  class A {
    static someTask(msg) {
      return msg;
    }
  }
  class B extends A {}
  class C extends B {}

  const msg = 'ok';

  const ABeforeMockIsUnchanged = Object.getOwnPropertyDescriptor(A, A.someTask.name);
  const BBeforeMockIsUnchanged = Object.getOwnPropertyDescriptor(B, B.someTask.name);
  const CBeforeMockShouldNotHaveDesc = Object.getOwnPropertyDescriptor(C, C.someTask.name);

  t.mock.method(C, C.someTask.name);
  C.someTask(msg);
  const BAfterMockIsUnchanged = Object.getOwnPropertyDescriptor(B, B.someTask.name);

  const AAfterMockIsUnchanged = Object.getOwnPropertyDescriptor(A, A.someTask.name);
  const CAfterMockHasDescriptor = Object.getOwnPropertyDescriptor(C, C.someTask.name);

  assert.strictEqual(CBeforeMockShouldNotHaveDesc, undefined);
  assert.ok(CAfterMockHasDescriptor);

  assert.deepStrictEqual(ABeforeMockIsUnchanged, AAfterMockIsUnchanged);
  assert.strictEqual(BBeforeMockIsUnchanged, BAfterMockIsUnchanged);
  assert.strictEqual(BBeforeMockIsUnchanged, undefined);

  assert.strictEqual(C.someTask.mock.restore(), undefined);
  const CAfterRestoreKeepsDescriptor = Object.getOwnPropertyDescriptor(C, C.someTask.name);
  assert.ok(CAfterRestoreKeepsDescriptor);
});

test('mocked functions report thrown errors', (t) => {
  const testError = new Error('test error');
  const fn = t.mock.fn(() => {
    throw testError;
  });

  assert.throws(fn, /test error/);
  assert.strictEqual(fn.mock.calls.length, 1);

  const call = fn.mock.calls[0];

  assert.deepStrictEqual(call.arguments, []);
  assert.strictEqual(call.error, testError);
  assert.strictEqual(call.result, undefined);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, undefined);
});

test('mocked constructors report thrown errors', (t) => {
  const testError = new Error('test error');
  class Clazz {
    constructor() {
      throw testError;
    }
  }

  const ctor = t.mock.fn(Clazz);

  assert.throws(() => {
    new ctor();
  }, /test error/);
  assert.strictEqual(ctor.mock.calls.length, 1);

  const call = ctor.mock.calls[0];

  assert.deepStrictEqual(call.arguments, []);
  assert.strictEqual(call.error, testError);
  assert.strictEqual(call.result, undefined);
  assert.strictEqual(call.target, Clazz);
  assert.strictEqual(call.this, undefined);
});

test('mocks a function', (t) => {
  const sum = (arg1, arg2) => arg1 + arg2;
  const difference = (arg1, arg2) => arg1 - arg2;
  const fn = t.mock.fn(sum, difference);

  assert.strictEqual(fn.mock.calls.length, 0);
  assert.strictEqual(fn(3, 4), -1);
  assert.strictEqual(fn(9, 1), 8);
  assert.strictEqual(fn.mock.calls.length, 2);

  let call = fn.mock.calls[0];
  assert.deepStrictEqual(call.arguments, [3, 4]);
  assert.strictEqual(call.result, -1);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, undefined);

  call = fn.mock.calls[1];
  assert.deepStrictEqual(call.arguments, [9, 1]);
  assert.strictEqual(call.result, 8);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, undefined);

  assert.strictEqual(fn.mock.restore(), undefined);
  assert.strictEqual(fn(2, 11), 13);
});

test('mocks a constructor', (t) => {
  class ParentClazz {
    constructor(c) {
      this.c = c;
    }
  }

  class Clazz extends ParentClazz {
    #privateValue;

    constructor(a, b) {
      super(a + b);
      this.a = a;
      this.#privateValue = b;
    }

    getPrivateValue() {
      return this.#privateValue;
    }
  }

  class MockClazz {
    // eslint-disable-next-line no-unused-private-class-members
    #privateValue;

    constructor(z) {
      this.z = z;
    }
  }

  const ctor = t.mock.fn(Clazz, MockClazz);
  const instance = new ctor(42, 85);

  assert(!(instance instanceof MockClazz));
  assert(instance instanceof Clazz);
  assert(instance instanceof ParentClazz);
  assert.strictEqual(instance.a, undefined);
  assert.strictEqual(instance.c, undefined);
  assert.strictEqual(instance.z, 42);
  assert.strictEqual(ctor.mock.calls.length, 1);

  const call = ctor.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [42, 85]);
  assert.strictEqual(call.result, instance);
  assert.strictEqual(call.target, Clazz);
  assert.strictEqual(call.this, instance);
  assert.throws(() => {
    instance.getPrivateValue();
  }, /TypeError: Cannot read private member #privateValue /);
});

test('mocks an object method', (t) => {
  const obj = {
    prop: 5,
    method(a, b) {
      return a + b + this.prop;
    },
  };

  function mockMethod(a) {
    return a + this.prop;
  }

  assert.strictEqual(obj.method(1, 3), 9);
  t.mock.method(obj, 'method', mockMethod);
  assert.strictEqual(obj.method.mock.calls.length, 0);
  assert.strictEqual(obj.method(1, 3), 6);

  const call = obj.method.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [1, 3]);
  assert.strictEqual(call.result, 6);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(obj.method.mock.restore(), undefined);
  assert.strictEqual(obj.method(1, 3), 9);
  assert.strictEqual(obj.method.mock, undefined);
});

test('mocks a getter', (t) => {
  const obj = {
    prop: 5,
    get method() {
      return this.prop;
    },
  };

  function mockMethod() {
    return this.prop - 1;
  }

  assert.strictEqual(obj.method, 5);

  const getter = t.mock.method(obj, 'method', mockMethod, { getter: true });

  assert.strictEqual(getter.mock.calls.length, 0);
  assert.strictEqual(obj.method, 4);

  const call = getter.mock.calls[0];

  assert.deepStrictEqual(call.arguments, []);
  assert.strictEqual(call.result, 4);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(getter.mock.restore(), undefined);
  assert.strictEqual(obj.method, 5);
});

test('mocks a setter', (t) => {
  const obj = {
    prop: 100,
    // eslint-disable-next-line accessor-pairs
    set method(val) {
      this.prop = val;
    },
  };

  function mockMethod(val) {
    this.prop = -val;
  }

  assert.strictEqual(obj.prop, 100);
  obj.method = 88;
  assert.strictEqual(obj.prop, 88);

  const setter = t.mock.method(obj, 'method', mockMethod, { setter: true });

  assert.strictEqual(setter.mock.calls.length, 0);
  obj.method = 77;
  assert.strictEqual(obj.prop, -77);
  assert.strictEqual(setter.mock.calls.length, 1);

  const call = setter.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [77]);
  assert.strictEqual(call.result, undefined);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(setter.mock.restore(), undefined);
  assert.strictEqual(obj.prop, -77);
  obj.method = 65;
  assert.strictEqual(obj.prop, 65);
});

test('mocks a getter with syntax sugar', (t) => {
  const obj = {
    prop: 5,
    get method() {
      return this.prop;
    },
  };

  function mockMethod() {
    return this.prop - 1;
  }
  const getter = t.mock.getter(obj, 'method', mockMethod);
  assert.strictEqual(getter.mock.calls.length, 0);
  assert.strictEqual(obj.method, 4);

  const call = getter.mock.calls[0];

  assert.deepStrictEqual(call.arguments, []);
  assert.strictEqual(call.result, 4);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(getter.mock.restore(), undefined);
  assert.strictEqual(obj.method, 5);
});

test('mocks a setter with syntax sugar', (t) => {
  const obj = {
    prop: 100,
    // eslint-disable-next-line accessor-pairs
    set method(val) {
      this.prop = val;
    },
  };

  function mockMethod(val) {
    this.prop = -val;
  }

  assert.strictEqual(obj.prop, 100);
  obj.method = 88;
  assert.strictEqual(obj.prop, 88);

  const setter = t.mock.setter(obj, 'method', mockMethod);

  assert.strictEqual(setter.mock.calls.length, 0);
  obj.method = 77;
  assert.strictEqual(obj.prop, -77);
  assert.strictEqual(setter.mock.calls.length, 1);

  const call = setter.mock.calls[0];

  assert.deepStrictEqual(call.arguments, [77]);
  assert.strictEqual(call.result, undefined);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, obj);

  assert.strictEqual(setter.mock.restore(), undefined);
  assert.strictEqual(obj.prop, -77);
  obj.method = 65;
  assert.strictEqual(obj.prop, 65);
});

test('mocked functions match name and length', (t) => {
  function getNameAndLength(fn) {
    return {
      name: Object.getOwnPropertyDescriptor(fn, 'name'),
      length: Object.getOwnPropertyDescriptor(fn, 'length'),
    };
  }

  function func1() {}
  const func2 = function(a) {}; // eslint-disable-line func-style
  const arrow = (a, b, c) => {};
  const obj = { method(a, b) {} };

  assert.deepStrictEqual(
    getNameAndLength(func1),
    getNameAndLength(t.mock.fn(func1))
  );
  assert.deepStrictEqual(
    getNameAndLength(func2),
    getNameAndLength(t.mock.fn(func2))
  );
  assert.deepStrictEqual(
    getNameAndLength(arrow),
    getNameAndLength(t.mock.fn(arrow))
  );
  assert.deepStrictEqual(
    getNameAndLength(obj.method),
    getNameAndLength(t.mock.method(obj, 'method', func1))
  );
});

test('method() fails if method cannot be redefined', (t) => {
  const obj = {
    prop: 5,
  };

  Object.defineProperty(obj, 'method', {
    configurable: false,
    value(a, b) {
      return a + b + this.prop;
    }
  });

  function mockMethod(a) {
    return a + this.prop;
  }

  assert.throws(() => {
    t.mock.method(obj, 'method', mockMethod);
  }, /Cannot redefine property: method/);
  assert.strictEqual(obj.method(1, 3), 9);
  assert.strictEqual(obj.method.mock, undefined);
});

test('method() fails if field is a property instead of a method', (t) => {
  const obj = {
    prop: 5,
    method: 100,
  };

  function mockMethod(a) {
    return a + this.prop;
  }

  assert.throws(() => {
    t.mock.method(obj, 'method', mockMethod);
  }, /The argument 'methodName' must be a method/);
  assert.strictEqual(obj.method, 100);
  assert.strictEqual(obj.method.mock, undefined);
});

test('mocks can be auto-restored', (t) => {
  let cnt = 0;

  function addOne() {
    cnt++;
    return cnt;
  }

  function addTwo() {
    cnt += 2;
    return cnt;
  }

  const fn = t.mock.fn(addOne, addTwo, { times: 2 });

  assert.strictEqual(fn(), 2);
  assert.strictEqual(fn(), 4);
  assert.strictEqual(fn(), 5);
  assert.strictEqual(fn(), 6);
});

test('mock implementation can be changed dynamically', (t) => {
  let cnt = 0;

  function addOne() {
    cnt++;
    return cnt;
  }

  function addTwo() {
    cnt += 2;
    return cnt;
  }

  function addThree() {
    cnt += 3;
    return cnt;
  }

  const fn = t.mock.fn(addOne);

  assert.strictEqual(fn.mock.callCount(), 0);
  assert.strictEqual(fn(), 1);
  assert.strictEqual(fn(), 2);
  assert.strictEqual(fn(), 3);
  assert.strictEqual(fn.mock.callCount(), 3);

  fn.mock.mockImplementation(addTwo);
  assert.strictEqual(fn(), 5);
  assert.strictEqual(fn(), 7);
  assert.strictEqual(fn.mock.callCount(), 5);

  fn.mock.restore();
  assert.strictEqual(fn(), 8);
  assert.strictEqual(fn(), 9);
  assert.strictEqual(fn.mock.callCount(), 7);

  assert.throws(() => {
    fn.mock.mockImplementationOnce(common.mustNotCall(), 6);
  }, /The value of "onCall" is out of range\. It must be >= 7/);

  fn.mock.mockImplementationOnce(addThree, 7);
  fn.mock.mockImplementationOnce(addTwo, 8);
  assert.strictEqual(fn(), 12);
  assert.strictEqual(fn(), 14);
  assert.strictEqual(fn(), 15);
  assert.strictEqual(fn.mock.callCount(), 10);
  fn.mock.mockImplementationOnce(addThree);
  assert.strictEqual(fn(), 18);
  assert.strictEqual(fn(), 19);
  assert.strictEqual(fn.mock.callCount(), 12);
});

test('local mocks are auto restored after the test finishes', async (t) => {
  const obj = {
    foo() {},
    bar() {},
  };
  const originalFoo = obj.foo;
  const originalBar = obj.bar;

  assert.strictEqual(originalFoo, obj.foo);
  assert.strictEqual(originalBar, obj.bar);

  const mockFoo = t.mock.method(obj, 'foo');

  assert.strictEqual(mockFoo, obj.foo);
  assert.notStrictEqual(originalFoo, obj.foo);
  assert.strictEqual(originalBar, obj.bar);

  t.beforeEach(() => {
    assert.strictEqual(mockFoo, obj.foo);
    assert.strictEqual(originalBar, obj.bar);
  });

  t.afterEach(() => {
    assert.strictEqual(mockFoo, obj.foo);
    assert.notStrictEqual(originalBar, obj.bar);
  });

  await t.test('creates mocks that are auto restored', (t) => {
    const mockBar = t.mock.method(obj, 'bar');

    assert.strictEqual(mockFoo, obj.foo);
    assert.strictEqual(mockBar, obj.bar);
    assert.notStrictEqual(originalBar, obj.bar);
  });

  assert.strictEqual(mockFoo, obj.foo);
  assert.strictEqual(originalBar, obj.bar);
});

test('reset mock calls', (t) => {
  const sum = (arg1, arg2) => arg1 + arg2;
  const difference = (arg1, arg2) => arg1 - arg2;
  const fn = t.mock.fn(sum, difference);

  assert.strictEqual(fn(1, 2), -1);
  assert.strictEqual(fn(2, 1), 1);
  assert.strictEqual(fn.mock.calls.length, 2);
  assert.strictEqual(fn.mock.callCount(), 2);

  fn.mock.resetCalls();
  assert.strictEqual(fn.mock.calls.length, 0);
  assert.strictEqual(fn.mock.callCount(), 0);

  assert.strictEqual(fn(3, 2), 1);
});

test('uses top level mock', () => {
  function sum(a, b) {
    return a + b;
  }

  function difference(a, b) {
    return a - b;
  }

  const fn = mock.fn(sum, difference);

  assert.strictEqual(fn.mock.calls.length, 0);
  assert.strictEqual(fn(3, 4), -1);
  assert.strictEqual(fn.mock.calls.length, 1);
  mock.reset();
  assert.strictEqual(fn(3, 4), 7);
  assert.strictEqual(fn.mock.calls.length, 2);
});

test('the getter and setter options cannot be used together', (t) => {
  assert.throws(() => {
    t.mock.method({}, 'method', { getter: true, setter: true });
  }, /The property 'options\.setter' cannot be used with 'options\.getter'/);
});

test('method names must be strings or symbols', (t) => {
  const symbol = Symbol();
  const obj = {
    method() {},
    [symbol]() {},
  };

  t.mock.method(obj, 'method');
  t.mock.method(obj, symbol);

  assert.throws(() => {
    t.mock.method(obj, {});
  }, /The "methodName" argument must be one of type string or symbol/);
});

test('the times option must be an integer >= 1', (t) => {
  assert.throws(() => {
    t.mock.fn({ times: null });
  }, /The "options\.times" property must be of type number/);

  assert.throws(() => {
    t.mock.fn({ times: 0 });
  }, /The value of "options\.times" is out of range/);

  assert.throws(() => {
    t.mock.fn(() => {}, { times: 3.14159 });
  }, /The value of "options\.times" is out of range/);
});

test('spies on a class prototype method', (t) => {
  class Clazz {
    constructor(c) {
      this.c = c;
    }

    getC() {
      return this.c;
    }
  }

  const instance = new Clazz(85);

  assert.strictEqual(instance.getC(), 85);
  t.mock.method(Clazz.prototype, 'getC');

  assert.strictEqual(instance.getC.mock.calls.length, 0);
  assert.strictEqual(instance.getC(), 85);
  assert.strictEqual(instance.getC.mock.calls.length, 1);
  assert.strictEqual(Clazz.prototype.getC.mock.calls.length, 1);

  const call = instance.getC.mock.calls[0];
  assert.deepStrictEqual(call.arguments, []);
  assert.strictEqual(call.result, 85);
  assert.strictEqual(call.error, undefined);
  assert.strictEqual(call.target, undefined);
  assert.strictEqual(call.this, instance);
});

test('getter() fails if getter options set to false', (t) => {
  assert.throws(() => {
    t.mock.getter({}, 'method', { getter: false });
  }, /The property 'options\.getter' cannot be false/);
});

test('setter() fails if setter options set to false', (t) => {
  assert.throws(() => {
    t.mock.setter({}, 'method', { setter: false });
  }, /The property 'options\.setter' cannot be false/);
});

test('getter() fails if setter options is true', (t) => {
  assert.throws(() => {
    t.mock.getter({}, 'method', { setter: true });
  }, /The property 'options\.setter' cannot be used with 'options\.getter'/);
});

test('setter() fails if getter options is true', (t) => {
  assert.throws(() => {
    t.mock.setter({}, 'method', { getter: true });
  }, /The property 'options\.setter' cannot be used with 'options\.getter'/);
});
