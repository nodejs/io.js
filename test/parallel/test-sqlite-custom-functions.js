'use strict';
require('../common');
const assert = require('node:assert');
const { DatabaseSync } = require('node:sqlite');
const { suite, test } = require('node:test');

suite('DatabaseSync.prototype.function()', () => {
  suite('input validation', () => {
    const db = new DatabaseSync(':memory:');

    test('throws if name is not a string', () => {
      assert.throws(() => {
        db.function();
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "name" argument must be a string/,
      });
    });

    test('throws if function is not a function', () => {
      assert.throws(() => {
        db.function('foo');
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "function" argument must be a function/,
      });
    });

    test('throws if options is not an object', () => {
      assert.throws(() => {
        db.function('foo', null, () => {});
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "options" argument must be an object/,
      });
    });

    test('throws if options.useBigIntArguments is not a boolean', () => {
      assert.throws(() => {
        db.function('foo', { useBigIntArguments: null }, () => {});
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "options\.useBigIntArguments" argument must be a boolean/,
      });
    });

    test('throws if options.varargs is not a boolean', () => {
      assert.throws(() => {
        db.function('foo', { varargs: null }, () => {});
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "options\.varargs" argument must be a boolean/,
      });
    });

    test('throws if options.deterministic is not a boolean', () => {
      assert.throws(() => {
        db.function('foo', { deterministic: null }, () => {});
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "options\.deterministic" argument must be a boolean/,
      });
    });

    test('throws if options.directOnly is not a boolean', () => {
      assert.throws(() => {
        db.function('foo', { directOnly: null }, () => {});
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "options\.directOnly" argument must be a boolean/,
      });
    });
  });

  suite('useBigIntArguments', () => {
    test('converts arguments to BigInts when true', () => {
      const db = new DatabaseSync(':memory:');
      let value;
      const r = db.function('custom', { useBigIntArguments: true }, (arg) => {
        value = arg;
      });
      assert.strictEqual(r, undefined);
      db.prepare('SELECT custom(5) AS custom').get();
      assert.strictEqual(value, 5n);
    });

    test('uses number primitives when false', () => {
      const db = new DatabaseSync(':memory:');
      let value;
      const r = db.function('custom', { useBigIntArguments: false }, (arg) => {
        value = arg;
      });
      assert.strictEqual(r, undefined);
      db.prepare('SELECT custom(5) AS custom').get();
      assert.strictEqual(value, 5);
    });

    test('defaults to false', () => {
      const db = new DatabaseSync(':memory:');
      let value;
      const r = db.function('custom', (arg) => {
        value = arg;
      });
      assert.strictEqual(r, undefined);
      db.prepare('SELECT custom(5) AS custom').get();
      assert.strictEqual(value, 5);
    });

    test('throws if value cannot fit in a number', () => {
      const db = new DatabaseSync(':memory:');
      const value = Number.MAX_SAFE_INTEGER + 1;
      db.function('custom', (arg) => {});
      assert.throws(() => {
        db.prepare(`SELECT custom(${value}) AS custom`).get();
      }, {
        code: 'ERR_OUT_OF_RANGE',
        message: /Value is too large to be represented as a JavaScript number: 9007199254740992/,
      });
    });
  });

  suite('varargs', () => {
    test('supports variable number of arguments when true', () => {
      const db = new DatabaseSync(':memory:');
      let value;
      const r = db.function('custom', { varargs: true }, (...args) => {
        value = args;
      });
      assert.strictEqual(r, undefined);
      db.prepare('SELECT custom(5, 4, 3, 2, 1) AS custom').get();
      assert.deepStrictEqual(value, [5, 4, 3, 2, 1]);
    });

    test('uses function.length when false', () => {
      const db = new DatabaseSync(':memory:');
      let value;
      const r = db.function('custom', { varargs: false }, (a, b, c) => {
        value = [a, b, c];
      });
      assert.strictEqual(r, undefined);
      db.prepare('SELECT custom(1, 2, 3) AS custom').get();
      assert.deepStrictEqual(value, [1, 2, 3]);
    });

    test('defaults to false', () => {
      const db = new DatabaseSync(':memory:');
      let value;
      const r = db.function('custom', (a, b, c) => {
        value = [a, b, c];
      });
      assert.strictEqual(r, undefined);
      db.prepare('SELECT custom(7, 8, 9) AS custom').get();
      assert.deepStrictEqual(value, [7, 8, 9]);
    });

    test('throws if an incorrect number of arguments is provided', () => {
      const db = new DatabaseSync(':memory:');
      db.function('custom', (a, b, c, d) => {});
      assert.throws(() => {
        db.prepare('SELECT custom(1, 2, 3) AS custom').get();
      }, {
        code: 'ERR_SQLITE_ERROR',
        message: /wrong number of arguments to function custom\(\)/,
      });
    });
  });

  suite('deterministic', () => {
    test('creates a deterministic function when true', () => {
      const db = new DatabaseSync(':memory:');
      db.function('isDeterministic', { deterministic: true }, () => {
        return 42;
      });
      const r = db.exec(`
        CREATE TABLE t1 (
          a INTEGER PRIMARY KEY,
          b INTEGER GENERATED ALWAYS AS (isDeterministic()) VIRTUAL
        )
      `);
      assert.strictEqual(r, undefined);
    });

    test('creates a non-deterministic function when false', () => {
      const db = new DatabaseSync(':memory:');
      db.function('isNonDeterministic', { deterministic: false }, () => {
        return 42;
      });
      assert.throws(() => {
        db.exec(`
          CREATE TABLE t1 (
            a INTEGER PRIMARY KEY,
            b INTEGER GENERATED ALWAYS AS (isNonDeterministic()) VIRTUAL
          )
        `);
      }, {
        code: 'ERR_SQLITE_ERROR',
        message: /non-deterministic functions prohibited in generated columns/,
      });
    });

    test('deterministic defaults to false', () => {
      const db = new DatabaseSync(':memory:');
      db.function('isNonDeterministic', () => {
        return 42;
      });
      assert.throws(() => {
        db.exec(`
          CREATE TABLE t1 (
            a INTEGER PRIMARY KEY,
            b INTEGER GENERATED ALWAYS AS (isNonDeterministic()) VIRTUAL
          )
        `);
      }, {
        code: 'ERR_SQLITE_ERROR',
        message: /non-deterministic functions prohibited in generated columns/,
      });
    });
  });

  suite('directOnly', () => {
    test('sets SQLite direct only flag when true', () => {
      const db = new DatabaseSync(':memory:');
      db.function('fn', { deterministic: true, directOnly: true }, () => {
        return 42;
      });
      assert.throws(() => {
        db.exec(`
          CREATE TABLE t1 (
            a INTEGER PRIMARY KEY,
            b INTEGER GENERATED ALWAYS AS (fn()) VIRTUAL
          )
        `);
      }, {
        code: 'ERR_SQLITE_ERROR',
        message: /unsafe use of fn\(\)/
      });
    });

    test('does not set SQLite direct only flag when false', () => {
      const db = new DatabaseSync(':memory:');
      db.function('fn', { deterministic: true, directOnly: false }, () => {
        return 42;
      });
      const r = db.exec(`
        CREATE TABLE t1 (
          a INTEGER PRIMARY KEY,
          b INTEGER GENERATED ALWAYS AS (fn()) VIRTUAL
        )
      `);
      assert.strictEqual(r, undefined);
    });

    test('directOnly defaults to false', () => {
      const db = new DatabaseSync(':memory:');
      db.function('fn', { deterministic: true }, () => {
        return 42;
      });
      const r = db.exec(`
        CREATE TABLE t1 (
          a INTEGER PRIMARY KEY,
          b INTEGER GENERATED ALWAYS AS (fn()) VIRTUAL
        )
      `);
      assert.strictEqual(r, undefined);
    });
  });

  suite('return types', () => {
    test('supported return types', () => {
      const db = new DatabaseSync(':memory:');
      db.function('retUndefined', () => {});
      db.function('retNull', () => { return null; });
      db.function('retNumber', () => { return 3; });
      db.function('retString', () => { return 'foo'; });
      db.function('retBigInt', () => { return 5n; });
      db.function('retUint8Array', () => { return new Uint8Array([1, 2, 3]); });
      db.function('retArrayBufferView', () => {
        const arrayBuffer = new Uint8Array([1, 2, 3]).buffer;
        return new DataView(arrayBuffer);
      });
      const stmt = db.prepare(`SELECT
        retUndefined() AS retUndefined,
        retNull() AS retNull,
        retNumber() AS retNumber,
        retString() AS retString,
        retBigInt() AS retBigInt,
        retUint8Array() AS retUint8Array,
        retArrayBufferView() AS retArrayBufferView
      `);
      assert.deepStrictEqual(stmt.get(), {
        __proto__: null,
        retUndefined: null,
        retNull: null,
        retNumber: 3,
        retString: 'foo',
        retBigInt: 5,
        retUint8Array: new Uint8Array([1, 2, 3]),
        retArrayBufferView: new Uint8Array([1, 2, 3]),
      });
    });

    test('throws if returned BigInt is too large for SQLite', () => {
      const db = new DatabaseSync(':memory:');
      db.function('retBigInt', () => {
        return BigInt(Number.MAX_SAFE_INTEGER + 1);
      });
      const stmt = db.prepare('SELECT retBigInt() AS retBigInt');
      assert.throws(() => {
        stmt.get();
      }, {
        code: 'ERR_OUT_OF_RANGE',
      });
    });

    test('does not support Promise return values', () => {
      const db = new DatabaseSync(':memory:');
      db.function('retPromise', async () => {});
      const stmt = db.prepare('SELECT retPromise() AS retPromise');
      assert.throws(() => {
        stmt.get();
      }, {
        code: 'ERR_SQLITE_ERROR',
        message: /Asynchronous user-defined functions are not supported/,
      });
    });

    test('throws on unsupported return types', () => {
      const db = new DatabaseSync(':memory:');
      db.function('retFunction', () => {
        return () => {};
      });
      const stmt = db.prepare('SELECT retFunction() AS retFunction');
      assert.throws(() => {
        stmt.get();
      }, {
        code: 'ERR_SQLITE_ERROR',
        message: /Returned JavaScript value cannot be converted to a SQLite value/,
      });
    });
  });

  suite('handles conflicting errors from SQLite and JavaScript', () => {
    test('throws if value cannot fit in a number', () => {
      const db = new DatabaseSync(':memory:');
      const expected = { __proto__: null, id: 5, data: 'foo' };
      db.function('custom', (arg) => {});
      db.exec('CREATE TABLE test (id NUMBER NOT NULL PRIMARY KEY, data TEXT)');
      db.prepare('INSERT INTO test (id, data) VALUES (?, ?)').run(5, 'foo');
      assert.deepStrictEqual(db.prepare('SELECT * FROM test').get(), expected);
      assert.throws(() => {
        db.exec(`UPDATE test SET data = CUSTOM(${Number.MAX_SAFE_INTEGER + 1})`);
      }, {
        code: 'ERR_OUT_OF_RANGE',
        message: /Value is too large to be represented as a JavaScript number: 9007199254740992/,
      });
      assert.deepStrictEqual(db.prepare('SELECT * FROM test').get(), expected);
    });

    test('propagates JavaScript errors', () => {
      const db = new DatabaseSync(':memory:');
      const expected = { __proto__: null, id: 5, data: 'foo' };
      const err = new Error('boom');
      db.function('throws', () => {
        throw err;
      });
      db.exec('CREATE TABLE test (id NUMBER NOT NULL PRIMARY KEY, data TEXT)');
      db.prepare('INSERT INTO test (id, data) VALUES (?, ?)').run(5, 'foo');
      assert.deepStrictEqual(db.prepare('SELECT * FROM test').get(), expected);
      assert.throws(() => {
        db.exec('UPDATE test SET data = THROWS()');
      }, err);
      assert.deepStrictEqual(db.prepare('SELECT * FROM test').get(), expected);
    });
  });

  test('supported argument types', () => {
    const db = new DatabaseSync(':memory:');
    db.function('arguments', (i, f, s, n, b) => {
      assert.strictEqual(i, 5);
      assert.strictEqual(f, 3.14);
      assert.strictEqual(s, 'foo');
      assert.strictEqual(n, null);
      assert.deepStrictEqual(b, new Uint8Array([254]));
      return 42;
    });
    const stmt = db.prepare(
      'SELECT arguments(5, 3.14, \'foo\', null, x\'fe\') as result'
    );
    assert.deepStrictEqual(stmt.get(), { __proto__: null, result: 42 });
  });

  test('propagates thrown errors', () => {
    const db = new DatabaseSync(':memory:');
    const err = new Error('boom');
    db.function('throws', () => {
      throw err;
    });
    const stmt = db.prepare('SELECT throws()');
    assert.throws(() => {
      stmt.get();
    }, err);
  });

  test('throws if database is not open', () => {
    const db = new DatabaseSync(':memory:', { open: false });
    assert.throws(() => {
      db.function('foo', () => {});
    }, {
      code: 'ERR_INVALID_STATE',
      message: /database is not open/,
    });
  });
});
