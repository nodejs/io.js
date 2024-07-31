// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start("Check internal properties reported in object preview.");

Protocol.Debugger.enable();
Protocol.Runtime.enable();
Protocol.Runtime.onConsoleAPICalled(dumpInternalPropertiesAndEntries);

contextGroup.setupInjectedScriptEnvironment();

InspectorTest.runTestSuite([
  function boxedObjects(next)
  {
    checkExpression("new Number(239)")
      .then(() => checkExpression("new Boolean(false)"))
      .then(() => checkExpression("new String(\"abc\")"))
      .then(() => checkExpression("Object(Symbol(42))"))
      .then(() => checkExpression("Object(BigInt(2))"))
      .then(next);
  },

  function promise(next)
  {
    checkExpression("Promise.resolve(42)")
      .then(() => checkExpression("new Promise(() => undefined)"))
      .then(next);
  },

  function generatorObject(next)
  {
    checkExpression("(function* foo() { yield 1 })()")
      .then(next);
  },

  function entriesInMapAndSet(next)
  {
    checkExpression("new Map([[1,2]])")
      .then(() => checkExpression("new Set([1])"))
      .then(() => checkExpression("new WeakMap([[{}, 42]])"))
      .then(() => checkExpression("new WeakSet([{}])"))
      .then(next);
  },

  function symbolsAsKeysInEntries(next)
  {
    checkExpression("new Map([[Symbol('key1'), 1]])")
      .then(() => checkExpression("new Set([Symbol('key2')])"))
      .then(() => checkExpression("new WeakMap([[Symbol('key3'), 2]])"))
      .then(() => checkExpression("new WeakSet([Symbol('key4')])"))
      .then(next);
  },

  function iteratorObject(next)
  {
    checkExpression("(new Map([[1,2]])).entries()")
      .then(() => checkExpression("(new Set([1,2])).entries()"))
      .then(next);
  },

  function noPreviewForFunctionObject(next)
  {
    var expression = "(function foo(){})";
    InspectorTest.log(expression);
    Protocol.Runtime.evaluate({ expression: expression, generatePreview: true})
      .then(message => InspectorTest.logMessage(message))
      .then(next);
  },

  function otherObjects(next)
  {
    checkExpression("[1,2,3]")
      .then(() => checkExpression("/123/"))
      .then(() => checkExpression("({})"))
      .then(() => checkExpression("Boolean.prototype"))
      .then(next);
  },

  function overridenArrayGetter(next)
  {
    Protocol.Runtime.evaluate({ expression: "Array.prototype.__defineGetter__(\"0\",() => { throw new Error() }) "})
      .then(() => checkExpression("Promise.resolve(42)"))
      .then(next);
  },

  function privateNames(next)
  {
    checkExpression("new class { #foo = 1; #bar = 2; baz = 3;}")
      .then(() => checkExpression("new class extends class { #baz = 3; } { #foo = 1; #bar = 2; }"))
      .then(() => checkExpression("new class extends class { constructor() { return new Proxy({}, {}); } } { #foo = 1; #bar = 2; }"))
      .then(next);
  },

  function functionProxy(next)
  {
    checkExpression("new Proxy(() => {}, { get: () => x++ })")
      .then(next);
  }
]);

function checkExpression(expression)
{
  InspectorTest.log(`expression: ${expression}`);
  // console.table has higher limits for internal properties amount in preview.
  return Protocol.Runtime.evaluate({ expression: `console.table(${expression})`, generatePreview: true });
}

function dumpInternalPropertiesAndEntries(message)
{
  var properties;
  var entries;
  try {
    var preview = message.params.args[0].preview;
    properties = preview.properties;
    entries = preview.entries;
  } catch (e) {
    InspectorTest.logMessage(message);
    return;
  }
  if (!properties) {
    InspectorTest.logMessage(message);
    return;
  }
  for (var property of properties)
    InspectorTest.logMessage(property);
  if (entries) {
    InspectorTest.log("[[Entries]]:");
    InspectorTest.logMessage(entries);
  }
  InspectorTest.log("");
}
