// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/private-class-member-inspector-test.js');

let {session, contextGroup, Protocol} =
    InspectorTest.start('Test static private class accessors');

const script = `
function run() {
  class A {
    static test() {
      debugger;
    }
    static #field = 2;

    get #instanceAccessor() { return this.#field; }  // should not show up
    set #instanceAccessor(val) { this.#field = val; }  // should not show up

    static set #writeOnly(val) { this.#field = val; }
    static get #readOnly() { return this.#field; }
    static get #accessor() { return this.#field; }
    static set #accessor(val) { this.#field = val; }
  };
  A.test();

  class B extends A {
    static get #accessor() { return 'subclassAccessor'; }
    static test() {
      debugger;
    }
  }

  B.test();
}`;

contextGroup.addScript(script);

InspectorTest.runAsyncTestSuite([async function testScopesPaused() {
  Protocol.Debugger.enable();

  // Do not await here, instead oncePaused should be awaited.
  Protocol.Runtime.evaluate({expression: 'run()'});

  InspectorTest.log('private members on the base class');
  let {params: {callFrames}} =
      await Protocol.Debugger.oncePaused();  // inside A.test()
  let frame = callFrames[0];

  await printPrivateMembers(
      Protocol, InspectorTest, {objectId: frame.this.objectId});

  Protocol.Debugger.resume();
  ({params: {callFrames}} = await Protocol.Debugger.oncePaused());  // B.test();
  frame = callFrames[0];

  InspectorTest.log('private members on the subclass');
  await printPrivateMembers(
      Protocol, InspectorTest, {objectId: frame.this.objectId});

  Protocol.Debugger.resume();
  Protocol.Debugger.disable();
}]);
