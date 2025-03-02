// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

utils.load('test/inspector/wasm-inspector-test.js');

let {session, contextGroup, Protocol} = InspectorTest.start('Tests breakable locations in wasm');

var builder = new WasmModuleBuilder();

// clang-format off
var func_idx = builder.addFunction('helper', kSig_v_v)
    .addLocals(kWasmI32, 1 )
    .addBody([
        kExprNop,
        kExprI32Const, 12,
        kExprLocalSet, 0,
    ]).index;

builder.addFunction('main', kSig_v_i)
    .addBody([
        kExprLocalGet, 0,
        kExprIf, kWasmVoid,
        kExprBlock, kWasmVoid,
        kExprCallFunction, func_idx,
        kExprEnd,
        kExprEnd
    ]).exportAs('main');
// clang-format on

var module_bytes = builder.toArray();

Protocol.Runtime.enable();
Protocol.Debugger.enable();
Protocol.Debugger.onScriptParsed(handleScriptParsed);

InspectorTest.runAsyncTestSuite([
  async function test() {
    InspectorTest.log('Running testFunction...');
    await WasmInspectorTest.instantiate(module_bytes);
    await getBreakableLocationsForAllWasmScripts();
    await setAllBreakableLocations();
    InspectorTest.log('Running wasm code...');
    // Begin executing code:
    var promise = WasmInspectorTest.evalWithUrl('instance.exports.main(1)', 'runWasm');
    await waitForAllPauses();
    // Code should now complete
    await promise;
  }
]);

var allBreakableLocations = [];

var urls = {};
var numScripts = 0;
var wasmScripts = [];
function handleScriptParsed(messageObject) {
  var scriptId = messageObject.params.scriptId;
  var url = messageObject.params.url;
  urls[scriptId] = url;
  InspectorTest.log('Script nr ' + numScripts + ' parsed. URL: ' + url);
  ++numScripts;

  if (url.startsWith('wasm://')) {
    // Only want the full module script, not the function specific ones.
    if (url.split('/').length == 4) {
      InspectorTest.log('This is a wasm script (nr ' + wasmScripts.length + ').');
      wasmScripts.push(scriptId);
    }
  }
}

function printFailure(message) {
  if (!message.result) {
    InspectorTest.logMessage(message);
  }
}

function printBreakableLocations(message, expectedScriptId, bytecode) {
  var locations = message.result.locations;
  InspectorTest.log(locations.length + ' breakable location(s):');
  for (var i = 0; i < locations.length; ++i) {
    if (locations[i].scriptId != expectedScriptId) {
      InspectorTest.log(
          'SCRIPT ID MISMATCH!! ' + locations[i].scriptId + ' != ' +
          expectedScriptId);
    }
    if (locations[i].lineNumber != 0) {
      InspectorTest.log(`Unexpected line number: ${bytecode[locations[i].lineNumber]}`);
    }
    var line = `byte=${bytecode[locations[i].columnNumber]}`;
    InspectorTest.log(
        '[' + i + '] ' + locations[i].lineNumber + ':' +
        locations[i].columnNumber + ' || ' + line);
  }
}

function checkModuleBytes(encoded, bytecode) {
  // Encode bytecode as base64.
  var BASE = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var PAD = '=';
  var ret = '';
  var leftchar = 0;
  var leftbits = 0;
  for (var i = 0; i < bytecode.length; i++) {
    leftchar = (leftchar << 8) | bytecode[i];
    leftbits += 8;
    while (leftbits >= 6) {
      var curr = (leftchar >> (leftbits-6)) & 0x3f;
      leftbits -= 6;
      ret += BASE[curr];
    }
  }
  if (leftbits == 2) {
    ret += BASE[(leftchar&3) << 4];
    ret += PAD + PAD;
  } else if (leftbits == 4) {
    ret += BASE[(leftchar&0xf) << 2];
    ret += PAD;
  }

  // Check the base64 strings match.
  if(ret == encoded) {
    InspectorTest.log('Bytecode matches!');
  } else {
    InspectorTest.log('Original: ' + ret);
    InspectorTest.log('Returned by API: ' + encoded);
  }
}

async function checkGetBreakableLocations(wasmScriptNr) {
  InspectorTest.log(
    'Requesting all breakable locations in wasm script ' + wasmScriptNr);
  var scriptId = wasmScripts[wasmScriptNr];

  var msg = await Protocol.Debugger.getScriptSource({scriptId: scriptId});
  checkModuleBytes(msg.result.bytecode, module_bytes);

  msg = await Protocol.Debugger.getPossibleBreakpoints(
      {start: {lineNumber: 0, columnNumber: 0, scriptId: scriptId}});
  printFailure(msg);
  allBreakableLocations.push(...msg.result.locations);
  printBreakableLocations(msg, scriptId, module_bytes);

  InspectorTest.log('Requesting breakable locations in offsets [0,45)');
  msg = await Protocol.Debugger.getPossibleBreakpoints({
      start: {lineNumber: 0, columnNumber: 0, scriptId: scriptId},
      end: {lineNumber: 0, columnNumber: 45, scriptId: scriptId}});
  printFailure(msg);
  printBreakableLocations(msg, scriptId, module_bytes);

  InspectorTest.log('Requesting breakable locations in offsets [50,60)');
  msg = await Protocol.Debugger.getPossibleBreakpoints({
      start: {lineNumber: 0, columnNumber: 50, scriptId: scriptId},
      end: {lineNumber: 0, columnNumber: 60, scriptId: scriptId}});
  printFailure(msg);
  printBreakableLocations(msg, scriptId, module_bytes);
}

async function getBreakableLocationsForAllWasmScripts() {
  InspectorTest.log('Querying breakable locations for all wasm scripts now...');
  for (var wasmScriptNr = 0; wasmScriptNr < wasmScripts.length;
       ++wasmScriptNr) {
    await checkGetBreakableLocations(wasmScriptNr);
  }
}

function locationMatches(loc1, loc2) {
  return loc1.scriptId == loc2.scriptId && loc1.lineNumber == loc2.lineNumber &&
      loc1.columnNumber == loc2.columnNumber;
}

function locationStr(loc) {
  return urls[loc.scriptId] + ':' + loc.lineNumber + ':' + loc.columnNumber;
}

async function setBreakpoint(loc) {
  InspectorTest.log('Setting at ' + locationStr(loc));

  var msg = await Protocol.Debugger.setBreakpoint({ 'location': loc });
  printFailure(msg);

  if (locationMatches(loc, msg.result.actualLocation)) {
    InspectorTest.log("Success!");
  } else {
    InspectorTest.log("Mismatch!");
    InspectorTest.logMessage(msg);
  }
}

async function setAllBreakableLocations() {
  InspectorTest.log('Setting a breakpoint on each breakable location...');
  for (var loc of allBreakableLocations) {
    await setBreakpoint(loc);
  }
}

function recordPausedLocation(msg) {
  var topLocation = msg.params.callFrames[0].location;
  InspectorTest.log('Stopped at ' + locationStr(topLocation));
  for (var i = 0; i < allBreakableLocations.length; ++i) {
    if (locationMatches(topLocation, allBreakableLocations[i])) {
      allBreakableLocations.splice(i, 1);
      break;
    }
  }
}

async function waitForAllPauses() {
  var remaining = allBreakableLocations.length;
  InspectorTest.log('Missing breakpoints: ' + remaining);
  while (remaining > 0) {
    recordPausedLocation(await Protocol.Debugger.oncePaused());
    await Protocol.Debugger.resume();
    remaining--;
    InspectorTest.log('Missing breakpoints: ' + remaining);
  }
  if (allBreakableLocations.length != 0) {
    InspectorTest.log('Did not hit all breakable locations: '
                      + JSON.stringify(allBreakableLocations));
  }
}
