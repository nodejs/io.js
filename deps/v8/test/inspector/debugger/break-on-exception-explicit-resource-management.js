// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --js-staging

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Checks if catch prediction works on new `using` and `await using` syntax.');

Protocol.Debugger.enable();
Protocol.Debugger.onPaused(({params: {data}}) => {
  InspectorTest.log('paused on exception:');
  InspectorTest.logMessage(data);
  Protocol.Debugger.resume();
});

contextGroup.addInlineScript(
    `
function disposalUncaughtUsingSyntax() {
    using x = {
      value: 1,
      [Symbol.dispose]() {
    throw 1;
    }
    };
}

async function disposalUncaughtAwaitUsingSyntax() {
    await using y = {
      value: 2,
      [Symbol.asyncDispose]() {
    throw 2;
    }
    };
}

function disposalUncaughtSuppressedError() {
  using x = {
    value: 1,
    [Symbol.dispose]() {
  throw 3;
  }
  };
  using y = {
    value: 2,
    [Symbol.dispose]() {
  throw 4;
  }
  };
}

function disposalCaughtUsingSyntax() {
  try {
    using x = {
      value: 1,
      [Symbol.dispose]() {
    throw 5;
    }
    };
  } catch (e) {
  }
}

async function disposalCaughtAwaitUsingSyntax() {
  try {
    await using y = {
      value: 2,
      [Symbol.asyncDispose]() {
    throw 6;
    }
    };
  } catch (e) {
  }
}

function disposalCaughtSuppressedError() {
  try {
    using x = {
      value: 1,
      [Symbol.dispose]() {
    throw 7;
    }
    };
    using y = {
      value: 2,
      [Symbol.dispose]() {
    throw 8;
    }
    };
  } catch (e) {
  }
}
`,
    'test.js');

InspectorTest.runAsyncTestSuite([
  async function testPauseOnInitialState() {
    await evaluate('disposalUncaughtUsingSyntax()');
    await evaluate('disposalUncaughtAwaitUsingSyntax()');
    await evaluate('disposalUncaughtSuppressedError()');
    await evaluate('disposalCaughtUsingSyntax()');
    await evaluate('disposalCaughtAwaitUsingSyntax()');
    await evaluate('disposalCaughtSuppressedError()');
  },

  async function testPauseOnExceptionOff() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
    await evaluate('disposalUncaughtUsingSyntax()');
    await evaluate('disposalUncaughtAwaitUsingSyntax()');
    await evaluate('disposalUncaughtSuppressedError()');
    await evaluate('disposalCaughtUsingSyntax()');
    await evaluate('disposalCaughtAwaitUsingSyntax()');
    await evaluate('disposalCaughtSuppressedError()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnCaughtException() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'caught'});
    await evaluate('disposalUncaughtUsingSyntax()');
    await evaluate('disposalUncaughtAwaitUsingSyntax()');
    await evaluate('disposalUncaughtSuppressedError()');
    await evaluate('disposalCaughtUsingSyntax()');
    await evaluate('disposalCaughtAwaitUsingSyntax()');
    await evaluate('disposalCaughtSuppressedError()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnUncaughtException() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'uncaught'});
    await evaluate('disposalUncaughtUsingSyntax()');
    await evaluate('disposalUncaughtAwaitUsingSyntax()');
    await evaluate('disposalUncaughtSuppressedError()');
    await evaluate('disposalCaughtUsingSyntax()');
    await evaluate('disposalCaughtAwaitUsingSyntax()');
    await evaluate('disposalCaughtSuppressedError()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnAll() {
    await Protocol.Debugger.setPauseOnExceptions({state: 'all'});
    await evaluate('disposalUncaughtUsingSyntax()');
    await evaluate('disposalUncaughtAwaitUsingSyntax()');
    await evaluate('disposalUncaughtSuppressedError()');
    await evaluate('disposalCaughtUsingSyntax()');
    await evaluate('disposalCaughtAwaitUsingSyntax()');
    await evaluate('disposalCaughtSuppressedError()');
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  },

  async function testBreakOnExceptionInSilentMode(next) {
    await Protocol.Debugger.setPauseOnExceptions({state: 'all'});
    InspectorTest.log(`evaluate 'disposalUncaughtUsingSyntax()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalUncaughtUsingSyntax()', silent: true});
    InspectorTest.log(`evaluate 'disposalUncaughtAwaitUsingSyntax()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalUncaughtAwaitUsingSyntax()', silent: true});
    InspectorTest.log(`evaluate 'disposalUncaughtSuppressedError()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalUncaughtSuppressedError()', silent: true});
    InspectorTest.log(`evaluate 'disposalCaughtUsingSyntax()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalCaughtUsingSyntax()', silent: true});
    InspectorTest.log(`evaluate 'disposalCaughtAwaitUsingSyntax()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalCaughtAwaitUsingSyntax()', silent: true});
    InspectorTest.log(`evaluate 'disposalCaughtSuppressedError()'`);
    await Protocol.Runtime.evaluate(
        {expression: 'disposalCaughtSuppressedError()', silent: true});
    await Protocol.Debugger.setPauseOnExceptions({state: 'none'});
  }
]);

async function evaluate(expression) {
  InspectorTest.log(`\nevaluate '${expression}'..`);
  contextGroup.addInlineScript(expression);
  await InspectorTest.waitForPendingTasks();
}
