// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

let {session, contextGroup, Protocol} =
    InspectorTest.start('Test scopes with unavailable values');

contextGroup.addScript(`
function tdz() {
  debugger;
  const x = 2 ?? y + x;
  let y = x + 1;
  return y;
}

function opt() {
  function optimizedOut(x, stop) {
    let y = x + 1;
    let z = y + 1;
    if (stop) {
      debugger;
    }
    return z;
  }

  %PrepareFunctionForOptimization(optimizedOut);
  optimizedOut(1, false);
  optimizedOut(2, false);
  %OptimizeFunctionOnNextCall(optimizedOut);
  optimizedOut(3, false);

  return optimizedOut(1, true);
}

function unusedValInTdz() {
  debugger;
  let y = 1;
}

function varStaysUndefined() {
  debugger;
  var y = 42;
}
`);

async function findLocalVariable(name, scopeChain) {
  for (const scope of scopeChain) {
    if (scope.type !== 'local') continue;
    const {result: {result: variables}} =
        await Protocol.Runtime.getProperties({objectId: scope.object.objectId});
    const variable = variables.find(variable => variable.name === name);
    if (!variable) {
      InspectorTest.log(`FAIL: variable ${name} not found in local scope`);
    }
    return variable;
  }
}

Protocol.Debugger.onPaused(async ({ params: { callFrames: [{ scopeChain }] } }) => {
  const variable = await findLocalVariable('y', scopeChain);
  if ('value' in variable || 'get' in variable || 'set' in variable) {
    InspectorTest.log(
        'FAIL: variable y was expected to be reported as <value_unavailable>');
  } else {
    InspectorTest.log(
        'variable y correctly reported as <value_unavailable>');
  }
  await Protocol.Debugger.resume();
});

InspectorTest.runAsyncTestSuite([
  async function testTemporalDeadZone() {
    await Promise.all([
      Protocol.Runtime.enable(),
      Protocol.Debugger.enable(),
    ]);
    await Protocol.Runtime.evaluate({expression: 'tdz()'});
    await Promise.all([
      Protocol.Runtime.disable(),
      Protocol.Debugger.disable(),
    ]);
  },

  async function testOptimizedOut() {
    await Promise.all([
      Protocol.Runtime.enable(),
      Protocol.Debugger.enable(),
    ]);
    await Protocol.Runtime.evaluate({expression: 'opt()'});
    await Promise.all([
      Protocol.Runtime.disable(),
      Protocol.Debugger.disable(),
    ]);
  },

  async function testUnusedValueInTdz() {
    await Promise.all([
      Protocol.Runtime.enable(),
      Protocol.Debugger.enable(),
    ]);
    await Protocol.Runtime.evaluate({expression: 'unusedValInTdz()'});
    await Promise.all([
      Protocol.Runtime.disable(),
      Protocol.Debugger.disable(),
    ]);
  },

  async function testVarStaysUndefined() {
    await Promise.all([
      Protocol.Runtime.enable(),
      Protocol.Debugger.enable(),
    ]);
    Protocol.Runtime.evaluate({ expression: 'varStaysUndefined()' });
    const { params: { callFrames: [{ scopeChain }] } } = await Protocol.Debugger.oncePaused();
    const variable = await findLocalVariable('y', scopeChain);
    if ('value' in variable && variable.value.type === 'undefined') {
      InspectorTest.log('variable y correctly reported as <undefined>');
    } else {
      InspectorTest.log('FAIL: variable y was expected to be reported as <undefined>');
    }
    await Protocol.Debugger.resume();
    await Promise.all([
      Protocol.Runtime.disable(),
      Protocol.Debugger.disable(),
    ]);
  },
]);
