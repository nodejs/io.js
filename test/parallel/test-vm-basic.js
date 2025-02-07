// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';
const common = require('../common');
const assert = require('assert');
const vm = require('vm');

// vm.runInNewContext
{
  const sandbox = {};
  const result = vm.runInNewContext(
    'foo = "bar"; this.typeofProcess = typeof process; typeof Object;',
    sandbox
  );
  assert.deepStrictEqual(sandbox, {
    foo: 'bar',
    typeofProcess: 'undefined',
  });
  assert.strictEqual(result, 'function');
}

// vm.runInContext
{
  const sandbox = { foo: 'bar' };
  const context = vm.createContext(sandbox);
  const result = vm.runInContext(
    'baz = foo; this.typeofProcess = typeof process; typeof Object;',
    context
  );
  assert.deepStrictEqual(sandbox, {
    foo: 'bar',
    baz: 'bar',
    typeofProcess: 'undefined'
  });
  assert.strictEqual(result, 'function');
}

// vm.runInThisContext
{
  const result = vm.runInThisContext(
    'vmResult = "foo"; Object.prototype.toString.call(process);'
  );
  assert.strictEqual(globalThis.vmResult, 'foo');
  assert.strictEqual(result, '[object process]');
  delete globalThis.vmResult;
}

// vm.runInNewContext
{
  const result = vm.runInNewContext(
    'vmResult = "foo"; typeof process;'
  );
  assert.strictEqual(globalThis.vmResult, undefined);
  assert.strictEqual(result, 'undefined');
}

// vm.createContext
{
  const sandbox = {};
  const context = vm.createContext(sandbox);
  assert.strictEqual(sandbox, context);
}

// Run script with filename
{
  const script = 'throw new Error("boom")';
  const filename = 'test-boom-error';
  const context = vm.createContext();

  function checkErr(err) {
    return err.stack.startsWith('test-boom-error:1');
  }

  assert.throws(() => vm.runInContext(script, context, filename), checkErr);
  assert.throws(() => vm.runInNewContext(script, context, filename), checkErr);
  assert.throws(() => vm.runInThisContext(script, filename), checkErr);
}

// Invalid arguments
[null, 'string'].forEach((input) => {
  assert.throws(() => {
    vm.createContext({}, input);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The "options" argument must be of type object.' +
             common.invalidArgTypeHelper(input)
  });
});

['name', 'origin'].forEach((propertyName) => {
  assert.throws(() => {
    vm.createContext({}, { [propertyName]: null });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: `The "options.${propertyName}" property must be of type string. ` +
             'Received null'
  });
});

['contextName', 'contextOrigin'].forEach((propertyName) => {
  assert.throws(() => {
    vm.runInNewContext('', {}, { [propertyName]: null });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: `The "options.${propertyName}" property must be of type string. ` +
             'Received null'
  });
});

// vm.compileFunction
{
  assert.strictEqual(
    vm.compileFunction('console.log("Hello, World!")').toString(),
    'function () {\nconsole.log("Hello, World!")\n}'
  );

  assert.strictEqual(
    vm.compileFunction(
      'return p + q + r + s + t',
      ['p', 'q', 'r', 's', 't']
    )('ab', 'cd', 'ef', 'gh', 'ij'),
    'abcdefghij'
  );

  vm.compileFunction('return'); // Should not throw on 'return'

  assert.throws(() => {
    vm.compileFunction(
      '});\n\n(function() {\nconsole.log(1);\n})();\n\n(function() {'
    );
  }, {
    name: 'SyntaxError',
    message: "Unexpected token '}'"
  });

  // Tests for failed argument validation
  assert.throws(() => vm.compileFunction(), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "code" argument must be of type string. ' +
      'Received undefined'
  });

  vm.compileFunction(''); // Should pass without params or options

  assert.throws(() => vm.compileFunction('', null), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "params" argument must be an instance of Array. ' +
      'Received null'
  });

  // Test for invalid options type
  assert.throws(() => {
    vm.compileFunction('', [], null);
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options" argument must be of type object. Received null'
  });

  assert.throws(() => {
    vm.compileFunction('', [], 'string');
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options" argument must be of type object. Received type string (\'string\')'
  });

  assert.throws(() => {
    vm.compileFunction('', [], 123);
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options" argument must be of type object. Received type number (123)'
  });

  const optionTypes = {
    'filename': 'string',
    'columnOffset': 'number',
    'lineOffset': 'number',
    'cachedData': 'Buffer, TypedArray, or DataView',
    'produceCachedData': 'boolean',
  };

  for (const option in optionTypes) {
    const typeErrorMessage = `The "options.${option}" property must be ` +
      (option === 'cachedData' ? 'an instance of' : 'of type');
    assert.throws(() => {
      vm.compileFunction('', undefined, { [option]: null });
    }, {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message: typeErrorMessage +
        ` ${optionTypes[option]}. Received null`
    });
  }

  // Testing for context-based failures
  [Boolean(), Number(), null, String(), Symbol(), {}].forEach(
    (value) => {
      assert.throws(() => {
        vm.compileFunction('', undefined, { parsingContext: value });
      }, {
        name: 'TypeError',
        code: 'ERR_INVALID_ARG_TYPE',
        message: 'The "options.parsingContext" property must be an instance ' +
          `of Context.${common.invalidArgTypeHelper(value)}`
      });
    }
  );

  // Testing for non Array type-based failures
  [Boolean(), Number(), null, Object(), Symbol(), {}].forEach(
    (value) => {
      assert.throws(() => {
        vm.compileFunction('', value);
      }, {
        name: 'TypeError',
        code: 'ERR_INVALID_ARG_TYPE',
        message: 'The "params" argument must be an instance of Array.' +
          common.invalidArgTypeHelper(value)
      });
    }
  );

  assert.strictEqual(
    vm.compileFunction(
      'return a;',
      undefined,
      { contextExtensions: [{ a: 5 }] }
    )(),
    5
  );

  assert.throws(() => {
    vm.compileFunction('', undefined, { contextExtensions: null });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options.contextExtensions" property must be an instance of' +
       ' Array. Received null'
  });

  assert.throws(() => {
    vm.compileFunction('', undefined, { contextExtensions: [0] });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options.contextExtensions[0]" property must be of type ' +
       'object. Received type number (0)'
  });

  const oldLimit = Error.stackTraceLimit;
  // Setting value to run the last three tests
  Error.stackTraceLimit = 1;

  assert.throws(() => {
    vm.compileFunction('throw new Error("Sample Error")')();
  }, {
    message: 'Sample Error',
    stack: 'Error: Sample Error\n    at <anonymous>:1:7'
  });

  assert.throws(() => {
    vm.compileFunction(
      'throw new Error("Sample Error")',
      [],
      { lineOffset: 3 }
    )();
  }, {
    message: 'Sample Error',
    stack: 'Error: Sample Error\n    at <anonymous>:4:7'
  });

  assert.throws(() => {
    vm.compileFunction(
      'throw new Error("Sample Error")',
      [],
      { columnOffset: 3 }
    )();
  }, {
    message: 'Sample Error',
    stack: 'Error: Sample Error\n    at <anonymous>:1:10'
  });

  assert.strictEqual(
    vm.compileFunction(
      'return varInContext',
      [],
      {
        parsingContext: vm.createContext({ varInContext: 'abc' })
      }
    )(),
    'abc'
  );

  assert.throws(() => {
    vm.compileFunction(
      'return varInContext',
      []
    )();
  }, {
    message: 'varInContext is not defined',
    stack: 'ReferenceError: varInContext is not defined\n    at <anonymous>:1:1'
  });

  assert.notDeepStrictEqual(
    vm.compileFunction(
      'return global',
      [],
      {
        parsingContext: vm.createContext({ global: {} })
      }
    )(),
    global
  );

  assert.deepStrictEqual(
    vm.compileFunction(
      'return global',
      []
    )(),
    global
  );

  {
    const source = 'console.log("Hello, World!")';
    // Test compileFunction produceCachedData option
    const result = vm.compileFunction(source, [], {
      produceCachedData: true,
    });

    assert.ok(result.cachedDataProduced);
    assert.ok(result.cachedData.length > 0);

    // Test compileFunction cachedData consumption
    const result2 = vm.compileFunction(source, [], {
      cachedData: result.cachedData
    });
    assert.strictEqual(result2.cachedDataRejected, false);

    const result3 = vm.compileFunction('console.log("wrong source")', [], {
      cachedData: result.cachedData
    });
    assert.strictEqual(result3.cachedDataRejected, true);
  }

  // Resetting value
  Error.stackTraceLimit = oldLimit;
}
