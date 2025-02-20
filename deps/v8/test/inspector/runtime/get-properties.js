// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

let {session, contextGroup, Protocol} = InspectorTest.start('Checks Runtime.getProperties method');

InspectorTest.runAsyncTestSuite([
  function testObject5() {
    return logExpressionProperties('(function(){var r = Object(5); r.foo = \'cat\';return r;})()');
  },

  function testNotOwn() {
    return logExpressionProperties('({ a: 2, set b(_) {}, get b() {return 5;}, __proto__: { a: 3, c: 4, get d() {return 6;} }})', { ownProperties: false });
  },

  function testNotOwnSet() {
    return logExpressionProperties('new Set([1, 2, 3])', { ownProperties: false });
  },

  function testAccessorsOnly() {
    return logExpressionProperties('({ a: 2, set b(_) {}, get b() {return 5;}, c: \'c\', set d(_){} })', { ownProperties: true, accessorPropertiesOnly: true});
  },

  function testArray() {
    return logExpressionProperties('[\'red\', \'green\', \'blue\']');
  },

  function testBound() {
    return logExpressionProperties('Number.bind({}, 5)');
  },

  function testObjectThrowsLength() {
    return logExpressionProperties('({get length() { throw \'Length called\'; }})');
  },

  function testTypedArrayWithoutLength() {
    return logExpressionProperties('({__proto__: Uint8Array.prototype})');
  },

  function testClassWithPrivateFields() {
    return logExpressionProperties('new class { #foo = 2; #bar = 3; baz = 4; }')
      .then(() => logExpressionProperties('new class extends class { #baz = 1 } { #foo = 2; #bar = 3; baz = 4; }'))
      .then(() => logExpressionProperties('new class extends class { #hidden = 1; constructor() { return new Proxy({}, {}); } } { #foo = 2; #bar = 3; baz = 4; }'));
  },

  async function testArrayBuffer() {
    let objectId = await evaluateToObjectId('new Uint8Array([1, 1, 1, 1, 1, 1, 1, 1]).buffer');
    let props = await Protocol.Runtime.getProperties({ objectId, ownProperties: true });
    for (let prop of props.result.result) {
      InspectorTest.log(prop.name);
      await logGetPropertiesResult(prop.value.objectId);
    }
    for (let prop of props.result.internalProperties) {
      InspectorTest.log(prop.name);
      if (prop.value.objectId)
        await logGetPropertiesResult(prop.value.objectId);
    }
  },

  async function testArrayBufferFromWebAssemblyMemory() {
    let objectId = await evaluateToObjectId('new WebAssembly.Memory({initial: 1}).buffer');
    let props = await Protocol.Runtime.getProperties({ objectId, ownProperties: true });
    for (let prop of props.result.result) {
      InspectorTest.log(prop.name);
      await logGetPropertiesResult(prop.value.objectId);
    }
    for (let prop of props.result.internalProperties) {
      InspectorTest.log(prop.name);
      // Skip printing the values of the virtual typed arrays.
      if (/\[\[.*Array\]\]/.test(prop.name))
        continue;
      if (prop.value.objectId)
        await logGetPropertiesResult(prop.value.objectId);
    }
  },

  async function testDetachedArrayBuffer() {
    await Protocol.Runtime.evaluate({ expression: 'var a = new ArrayBuffer(16)' });
    await Protocol.Runtime.evaluate({ expression: 'var b = new Uint32Array(a)' });
    let objectId = await evaluateToObjectId('a');
    await Protocol.Runtime.evaluate({ expression: '%ArrayBufferDetach(a)' });
    await Protocol.Runtime.evaluate({ expression: 'b', generatePreview: true })
    let props = await Protocol.Runtime.getProperties({ objectId, ownProperties: true });
    for (let prop of props.result.result) {
      InspectorTest.log(prop.name);
      await logGetPropertiesResult(prop.value.objectId);
    }
    for (let prop of props.result.internalProperties) {
      InspectorTest.log(prop.name + ' ' + prop.value.value);
    }
  },

  async function testArrayBufferWithBrokenUintCtor() {
    await evaluateToObjectId(`(function() {
      this.uint8array_old = this.Uint8Array;
      this.Uint8Array = 42;
    })()`);
    await logExpressionProperties('new Int8Array([1, 1, 1, 1, 1, 1, 1]).buffer');
    await evaluateToObjectId(`(function() {
      this.Uint8Array = this.uint8array_old;
      delete this.uint8array_old;
    })()`);
  },

  async function testObjectWithProtoProperty() {
    await logExpressionProperties('Object.defineProperty({}, "__proto__", {enumerable: true, value: {b:"aaa"}})');
  },

  function testArrayNonIndexedPropertiesOnly() {
    return logExpressionProperties('[1, 2]', {nonIndexedPropertiesOnly: true, ownProperties: true});
  },

  function testTypedArrayNonIndexedPropertiesOnly() {
    return logExpressionProperties('new Int8Array(1)', {nonIndexedPropertiesOnly: true, ownProperties: true});
  },

  function testWeakRef() {
    return logExpressionProperties('new WeakRef(globalThis)');
  }
]);

async function logExpressionProperties(expression, flags) {
  const objectId = await evaluateToObjectId(expression);
  return await logGetPropertiesResult(objectId, flags);
}

async function evaluateToObjectId(expression) {
  return (await Protocol.Runtime.evaluate({ expression })).result.result.objectId;
}

async function logGetPropertiesResult(objectId, flags = { ownProperties: true }) {
  function hasGetterSetter(property, fieldName) {
    var v = property[fieldName];
    if (!v) return false;
    return v.type !== "undefined"
  }

  flags.objectId = objectId;
  let props = await Protocol.Runtime.getProperties(flags);
  var propertyArray = props.result.result;
  propertyArray.sort(NamedThingComparator);
  for (var i = 0; i < propertyArray.length; i++) {
    var p = propertyArray[i];
    var v = p.value;
    var own = p.isOwn ? "own" : "inherited";
    if (v)
      InspectorTest.log(`  ${p.name} ${own} ${v.type} ${v.value}`);
    else
      InspectorTest.log("  " + p.name + " " + own + " no value" +
        (hasGetterSetter(p, "get") ? ", getter" : "") + (hasGetterSetter(p, "set") ? ", setter" : ""));
  }

  function printFields(type, array) {
    if (!array) { return; }
    InspectorTest.log(type);
    array.sort(NamedThingComparator);
    for (var i = 0; i < array.length; i++) {
      var p = array[i];
      var v = p.value;
      InspectorTest.log(`  ${p.name} ${v.type} ${v.value}`);
    }
  }

  printFields("Internal properties", props.result.internalProperties);
  printFields("Private properties", props.result.privateProperties);

  function NamedThingComparator(o1, o2) {
    return o1.name === o2.name ? 0 : (o1.name < o2.name ? -1 : 1);
  }
}
