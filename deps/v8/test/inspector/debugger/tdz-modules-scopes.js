// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --no-always-turbofan

let {session, contextGroup, Protocol} =
    InspectorTest.start('Test module scope with variables in TDZ.');


Protocol.Debugger.enable();

contextGroup.addModule(`
debugger;
(function() { debugger })();
let moduleLet = 3;
const moduleConst = 4;
export let exportedModuleLet = 1;
export const exportedModuleConst = 2;
export default 42
debugger;
(function() { debugger })();
`, "module1");

contextGroup.addModule(`
debugger;
(function() { debugger })();
import { exportedModuleLet, exportedModuleConst } from 'module1';
let moduleLet = 5;
const moduleConst = 6;
debugger;
(function() { debugger })();
`, "module2");

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
