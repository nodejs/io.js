// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Tests console.context');

InspectorTest.runAsyncTestSuite([
  async function testConsoleContextMethod() {
    InspectorTest.log('console.context description:');
    var {result:{result}} = await Protocol.Runtime.evaluate({
      expression: 'console.context'});
    InspectorTest.logMessage(result);

    // Enumerate the methods alpha-sorted to make the test
    // independent of the (unspecified) enumeration order
    // of console.context() methods.
    InspectorTest.log('console.context() methods:');
    var {result: {result: {value}}} = await Protocol.Runtime.evaluate({
      expression: 'Object.keys(console.context()).sort()',
      returnByValue: true
    });
    InspectorTest.logMessage(value);
  },

  async function testDefaultConsoleContext() {
    await Protocol.Runtime.enable();
    Protocol.Runtime.evaluate({expression: 'console.log(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    Protocol.Runtime.evaluate({expression: 'console.info(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    Protocol.Runtime.evaluate({expression: 'console.debug(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    await Protocol.Runtime.evaluate({expression: 'console.clear()'});
    await Protocol.Runtime.disable();
  },

  async function testAnonymousConsoleContext() {
    await Protocol.Runtime.enable();
    Protocol.Runtime.evaluate({expression: 'console.context().log(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    Protocol.Runtime.evaluate({expression: 'console.context().info(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    Protocol.Runtime.evaluate({expression: 'console.context().debug(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    await Protocol.Runtime.evaluate({expression: 'console.context().clear()'});
    await Protocol.Runtime.disable();
  },

  async function testNamedConsoleContext() {
    await Protocol.Runtime.enable();
    Protocol.Runtime.evaluate({expression: `
      var context = console.context('named-context');
      context.log(239);
      context.info(239);
      context.debug(239);
    `});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    await Protocol.Runtime.evaluate({expression: 'console.clear()'});
    await Protocol.Runtime.disable();
  },

  async function testTwoConsoleContextsWithTheSameName() {
    await Protocol.Runtime.enable();
    Protocol.Runtime.evaluate({expression: 'console.context(\'named-context\').log(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    Protocol.Runtime.evaluate({expression: 'console.context(\'named-context\').log(239)'});
    var {params:{context}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.log(context);
    await Protocol.Runtime.evaluate({expression: 'console.clear()'});
    await Protocol.Runtime.disable();
  },

  async function testConsoleCountInDifferentConsoleContexts() {
    await Protocol.Runtime.enable();
    Protocol.Runtime.evaluate({expression: 'console.context(\'named-context\').count(239)'});
    var {params:{args}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.logMessage(args);
    Protocol.Runtime.evaluate({expression: 'console.context(\'named-context\').count(239)'});
    var {params:{args}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.logMessage(args);
    await Protocol.Runtime.evaluate({expression: 'console.clear()'});
    await Protocol.Runtime.disable();
  },

  async function testConsoleCountForNamedConsoleContext() {
    await Protocol.Runtime.enable();
    Protocol.Runtime.evaluate({expression: `
      var context = console.context('named-context');
      context.count(239);
      context.count(239);
    `});
    var {params:{args}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.logMessage(args);
    var {params:{args}} = await Protocol.Runtime.onceConsoleAPICalled();
    InspectorTest.logMessage(args);
    await Protocol.Runtime.evaluate({expression: 'console.clear()'});
    await Protocol.Runtime.disable();
  },

  async function testConsoleTimeInDifferentConsoleContexts() {
    await Protocol.Runtime.enable();
    await Protocol.Runtime.evaluate({
      expression: `globalThis.context = console.context('named-context')`,
    });
    utils.setCurrentTimeMSForTest(0.0);
    await Protocol.Runtime.evaluate({
      expression: `context.time('foo'); console.time('foo');`,
    });
    utils.setCurrentTimeMSForTest(1.0);
    await Promise.all([
      Protocol.Runtime.evaluate({
        expression: `console.timeEnd('foo')`,
      }),
      Protocol.Runtime.onceConsoleAPICalled().then(
          ({params: {args}}) => InspectorTest.logMessage(args)),
    ]);
    utils.setCurrentTimeMSForTest(2.0);
    await Promise.all([
      Protocol.Runtime.evaluate({
        expression: `context.timeEnd('foo')`,
      }),
      Protocol.Runtime.onceConsoleAPICalled().then(
          ({params: {args}}) => InspectorTest.logMessage(args)),
    ]);
    await Protocol.Runtime.disable();
  }
]);
