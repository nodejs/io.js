'use strict';

// Flags: --experimental-vm-modules

const common = require('../common');

const assert = require('assert');

const { SourceTextModule, createContext, Module } = require('vm');

async function createEmptyLinkedModule() {
  const m = new SourceTextModule('');
  await m.link(common.mustNotCall());
  return m;
}

async function checkArgType() {
  assert.throws(() => {
    new SourceTextModule();
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError'
  });

  for (const invalidOptions of [
    0, 1, null, true, 'str', () => {}, { identifier: 0 }, Symbol.iterator,
    { context: null }, { context: 'hucairz' }, { context: {} },
  ]) {
    assert.throws(() => {
      new SourceTextModule('', invalidOptions);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    });
  }

  for (const invalidLinker of [
    0, 1, undefined, null, true, 'str', {}, Symbol.iterator,
  ]) {
    await assert.rejects(async () => {
      const m = new SourceTextModule('');
      await m.link(invalidLinker);
    }, {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    });
  }
}

// Check methods/properties can only be used under a specific state.
async function checkModuleState() {
  await assert.rejects(async () => {
    const m = new SourceTextModule('');
    await m.link(common.mustNotCall());
    assert.strictEqual(m.status, 'linked');
    await m.link(common.mustNotCall());
  }, {
    code: 'ERR_VM_MODULE_ALREADY_LINKED'
  });

  await assert.rejects(async () => {
    const m = new SourceTextModule('');
    m.link(common.mustNotCall());
    assert.strictEqual(m.status, 'linking');
    await m.link(common.mustNotCall());
  }, {
    code: 'ERR_VM_MODULE_STATUS'
  });

  await assert.rejects(async () => {
    const m = new SourceTextModule('');
    await m.evaluate();
  }, {
    code: 'ERR_VM_MODULE_STATUS',
    message: 'Module status must be one of linked, evaluated, or errored'
  });

  await assert.rejects(async () => {
    const m = new SourceTextModule('');
    await m.evaluate(false);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "options" argument must be of type object. ' +
             'Received type boolean (false)'
  });

  assert.throws(() => {
    const m = new SourceTextModule('');
    m.error; // eslint-disable-line no-unused-expressions
  }, {
    code: 'ERR_VM_MODULE_STATUS',
    message: 'Module status must be errored'
  });

  await assert.rejects(async () => {
    const m = await createEmptyLinkedModule();
    await m.evaluate();
    m.error; // eslint-disable-line no-unused-expressions
  }, {
    code: 'ERR_VM_MODULE_STATUS',
    message: 'Module status must be errored'
  });

  assert.throws(() => {
    const m = new SourceTextModule('');
    m.namespace; // eslint-disable-line no-unused-expressions
  }, {
    code: 'ERR_VM_MODULE_STATUS',
    message: 'Module status must not be unlinked or linking'
  });
}

// Check link() fails when the returned module is not valid.
async function checkLinking() {
  await assert.rejects(async () => {
    const m = new SourceTextModule('import "foo";');
    try {
      await m.link(common.mustCall(() => ({})));
    } catch (err) {
      assert.strictEqual(m.status, 'errored');
      throw err;
    }
  }, {
    code: 'ERR_VM_MODULE_NOT_MODULE'
  });

  await assert.rejects(async () => {
    const c = createContext({ a: 1 });
    const foo = new SourceTextModule('', { context: c });
    await foo.link(common.mustNotCall());
    const bar = new SourceTextModule('import "foo";');
    try {
      await bar.link(common.mustCall(() => foo));
    } catch (err) {
      assert.strictEqual(bar.status, 'errored');
      throw err;
    }
  }, {
    code: 'ERR_VM_MODULE_DIFFERENT_CONTEXT'
  });

  const error = new Error();
  await assert.rejects(async () => {
    globalThis.error = error;
    const erroredModule = new SourceTextModule('throw error;');
    await erroredModule.link(common.mustNotCall());
    try {
      await erroredModule.evaluate();
    } catch {
      // ignored
    }
    delete globalThis.error;

    assert.strictEqual(erroredModule.status, 'errored');

    const rootModule = new SourceTextModule('import "errored";');
    await rootModule.link(common.mustCall(() => erroredModule));
  }, {
    code: 'ERR_VM_MODULE_LINK_FAILURE',
    cause: error,
  });
}

assert.throws(() => {
  new SourceTextModule('', {
    importModuleDynamically: 'hucairz'
  });
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError',
  message: 'The "options.importModuleDynamically" property must be of type ' +
    "function. Received type string ('hucairz')"
});

// Check the JavaScript engine deals with exceptions correctly
async function checkExecution() {
  await (async () => {
    const m = new SourceTextModule('import { nonexistent } from "module";');

    // There is no code for this exception since it is thrown by the JavaScript
    // engine.
    await assert.rejects(() => {
      return m.link(common.mustCall(() => new SourceTextModule('')));
    }, SyntaxError);
  })();

  await (async () => {
    const m = new SourceTextModule('throw new Error();');
    await m.link(common.mustNotCall());
    try {
      await m.evaluate();
    } catch (err) {
      assert.strictEqual(m.error, err);
      assert.strictEqual(m.status, 'errored');
      return;
    }
    assert.fail('Missing expected exception');
  })();
}

// Check for error thrown when breakOnSigint is not a boolean for evaluate()
async function checkInvalidOptionForEvaluate() {
  await assert.rejects(async () => {
    const m = new SourceTextModule('export const a = 1; export let b = 2');
    await m.evaluate({ breakOnSigint: 'a-string' });
  }, {
    name: 'TypeError',
    message:
      'The "options.breakOnSigint" property must be of type boolean. ' +
      "Received type string ('a-string')",
    code: 'ERR_INVALID_ARG_TYPE'
  });

  {
    ['link', 'evaluate'].forEach(async (method) => {
      await assert.rejects(async () => {
        await Module.prototype[method]();
      }, {
        code: 'ERR_INVALID_ARG_TYPE',
        message: /The "this" argument must be an instance of Module/
      });
    });
  }
}

function checkInvalidCachedData() {
  [true, false, 'foo', {}, Array, function() {}].forEach((invalidArg) => {
    const message = 'The "options.cachedData" property must be an ' +
                    'instance of Buffer, TypedArray, or DataView.' +
                    common.invalidArgTypeHelper(invalidArg);
    assert.throws(
      () => new SourceTextModule('import "foo";', { cachedData: invalidArg }),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError',
        message,
      }
    );
  });
}

function checkGettersErrors() {
  const expectedError = {
    code: 'ERR_INVALID_ARG_TYPE',
    message: /The "this" argument must be an instance of (?:Module|SourceTextModule)/,
  };
  const getters = ['identifier', 'context', 'namespace', 'status', 'error'];
  getters.forEach((getter) => {
    assert.throws(() => {
      // eslint-disable-next-line no-unused-expressions
      Module.prototype[getter];
    }, expectedError);
    assert.throws(() => {
      // eslint-disable-next-line no-unused-expressions
      SourceTextModule.prototype[getter];
    }, expectedError);
  });
  // `dependencySpecifiers` getter is just part of SourceTextModule
  assert.throws(() => {
    // eslint-disable-next-line no-unused-expressions
    SourceTextModule.prototype.dependencySpecifiers;
  }, expectedError);
}

const finished = common.mustCall();

(async function main() {
  await checkArgType();
  await checkModuleState();
  await checkLinking();
  await checkExecution();
  await checkInvalidOptionForEvaluate();
  checkInvalidCachedData();
  checkGettersErrors();
  finished();
})().then(common.mustCall());
