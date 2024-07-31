// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-always-turbofan

let {session, contextGroup, Protocol} =
    InspectorTest.start('Test scopes with variables in TDZ.');


Protocol.Debugger.enable();

contextGroup.addScript(`
{
  debugger;
  (function() { debugger })();
  let blockLet = 1;
  const blockConst = 2;
  let contextBlockLet = 3;
  let contextBlockConst = 4;
  _ => contextBlockConst + contextBlockLet;
  debugger;
  (function() { debugger })();
}
debugger;
(function() { debugger })();
let scriptLet = 1;
const scriptConst = 2;
debugger;
(function() { debugger })();
`);

(async function() {
for (let i =0; i < 8; i++) {
  let message = await Protocol.Debugger.oncePaused();
  let scopeChain = message.params.callFrames[0].scopeChain;
  let evalScopeObjectIds = [];
  InspectorTest.log("Debug break");
  for (let scope of scopeChain) {
    if (scope.type == "global") continue;
    InspectorTest.log(`  Scope type: ${scope.type}`);
    let { result: { result: locals }} = await Protocol.Runtime.getProperties({ "objectId" : scope.object.objectId });
    for (let local of locals) {
      if ('value' in local) {
        InspectorTest.log(`    ${local.name} : ${local.value.value}`);
      } else {
        InspectorTest.log(`    ${local.name} : <value_unavailable>`);
      }
    }
  }
  await Protocol.Debugger.resume();
}
InspectorTest.completeTest();
})();
