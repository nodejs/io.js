// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start('Test for Debugger.getPossibleBreakpoints');

Protocol.Runtime.enable();
Protocol.Debugger.enable();

InspectorTest.runTestSuite([

  function getPossibleBreakpointsInRange(next) {
    var source = 'function foo(){ return Promise.resolve(); }\nfunction boo(){ return Promise.resolve().then(() => 42); }\n\n';
    var scriptId;
    compileScript(source)
      .then(id => scriptId = id)
      .then(() => InspectorTest.log('Test start.scriptId != end.scriptId.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }, end: { lineNumber: 0, columnNumber: 0, scriptId: scriptId + '0' }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test not existing scriptId.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: '-1' }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test end < start.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 1, columnNumber: 0, scriptId: scriptId }, end: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test empty range in first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 16, scriptId: scriptId }, end: { lineNumber: 0, columnNumber: 16, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test one character range in first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 16, scriptId: scriptId }, end: { lineNumber: 0, columnNumber: 17, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test empty range in not first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 1, columnNumber: 16, scriptId: scriptId }, end: { lineNumber: 1, columnNumber: 16, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test one character range in not first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 1, columnNumber: 16, scriptId: scriptId }, end: { lineNumber: 1, columnNumber: 17, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test end is undefined'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test end.lineNumber > scripts.lineCount()'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }, end: { lineNumber: 5, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test one string'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }, end: { lineNumber: 1, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(() => InspectorTest.log('Test end.columnNumber > end.line.length(), should be the same as previous.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }, end: { lineNumber: 0, columnNumber: 256, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(next);
  },

  function getPossibleBreakpointsInArrow(next) {
    var source = 'function foo() { return Promise.resolve().then(() => 239).then(() => 42).then(() => () => 42) }';
    var scriptId;
    compileScript(source)
      .then(id => scriptId = id)
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source))
      .then(next);
  },

  function arrowFunctionFirstLine(next) {
    Protocol.Debugger.onPaused(message => dumpBreakLocationInSourceAndResume(message, source));

    var source = `function foo1() { Promise.resolve().then(() => 42) }
function foo2() { Promise.resolve().then(() => 42) }`;
    waitForPossibleBreakpoints(source, { lineNumber: 0, columnNumber: 0 }, { lineNumber: 1, columnNumber: 0 })
      .then(message => dumpAllLocations(message, source))
      .then(setAllBreakpoints)
      .then(() => Protocol.Runtime.evaluate({ expression: 'foo1(); foo2()'}))
      .then(next);
  },

  function arrowFunctionOnPause(next) {
    var source = `debugger; function foo3() { Promise.resolve().then(() => 42) }
function foo4() { Promise.resolve().then(() => 42) };\nfoo3();\nfoo4();`;
    waitForPossibleBreakpointsOnPause(source, { lineNumber: 0, columnNumber: 0 }, undefined, next)
      .then(message => dumpAllLocations(message, source))
      .then(setAllBreakpoints)
      .then(() => Protocol.Debugger.onPaused(message => dumpBreakLocationInSourceAndResume(message, source)))
      .then(() => Protocol.Debugger.resume());
  },

  function getPossibleBreakpointsInRangeWithOffset(next) {
    var source = 'function foo(){ return Promise.resolve(); }\nfunction boo(){ return Promise.resolve().then(() => 42); }\n\n';
    var scriptId;
    compileScript(source, { name: 'with-offset.js', line_offset: 1, column_offset: 1 })
      .then(id => scriptId = id)
      .then(() => InspectorTest.log('Test empty range in first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 1, columnNumber: 17, scriptId: scriptId }, end: { lineNumber: 1, columnNumber: 17, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(() => InspectorTest.log('Test one character range in first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 1, columnNumber: 17, scriptId: scriptId }, end: { lineNumber: 1, columnNumber: 18, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(() => InspectorTest.log('Test empty range in not first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 2, columnNumber: 16, scriptId: scriptId }, end: { lineNumber: 2, columnNumber: 16, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(() => InspectorTest.log('Test one character range in not first line.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 2, columnNumber: 16, scriptId: scriptId }, end: { lineNumber: 2, columnNumber: 17, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(() => InspectorTest.log('Test end is undefined'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(() => InspectorTest.log('Test end.lineNumber > scripts.lineCount()'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 0, columnNumber: 0, scriptId: scriptId }, end: { lineNumber: 5, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(() => InspectorTest.log('Test one string'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 1, columnNumber: 1, scriptId: scriptId }, end: { lineNumber: 2, columnNumber: 0, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(() => InspectorTest.log('Test end.columnNumber > end.line.length(), should be the same as previous.'))
      .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: { lineNumber: 1, columnNumber: 1, scriptId: scriptId }, end: { lineNumber: 1, columnNumber: 256, scriptId: scriptId }}))
      .then(message => dumpAllLocations(message, source, 1, 1))
      .then(next);
  },

  function withOffset(next) {
    Protocol.Debugger.onPaused(message => dumpBreakLocationInSourceAndResume(message, source, 3, 18));

    var source = `function foo5() { Promise.resolve().then(() => 42) }
function foo6() { Promise.resolve().then(() => 42) }
`;
    waitForPossibleBreakpoints(source, { lineNumber: 0, columnNumber: 0 }, undefined, { name: 'with-offset.js', line_offset: 3, column_offset: 18 })
      .then(message => dumpAllLocations(message, source, 3, 18))
      .then(setAllBreakpoints)
      .then(() => Protocol.Runtime.evaluate({ expression: 'foo5(); foo6()'}))
      .then(next);
  },

  function arrowFunctionReturn(next) {
    function checkSource(source, location) {
      return waitForPossibleBreakpoints(source, location)
        .then(message => dumpAllLocations(message, source));
    }

    checkSource('function foo() { function boo() { return 239 }  }\n', { lineNumber: 0, columnNumber: 0 })
      .then(() => checkSource('function foo() { function boo() { return 239 }; boo }\n', { lineNumber: 0, columnNumber: 0 }))
      .then(() => checkSource('function foo() { let boo = function() { return 239 }; }\n', { lineNumber: 0, columnNumber: 0 }))
      .then(() => checkSource('var foo = () => { 239 }\n', { lineNumber: 0, columnNumber: 0 }))
      .then(() => checkSource('function foo() { 239 }\n', { lineNumber: 0, columnNumber: 0 }))
      // TODO(kozyatinskiy): lineNumber for return position should be only 9, not 8.
      .then(() => checkSource('var foo = () => 239', { lineNumber: 0, columnNumber: 0 }))
      .then(() => checkSource('var foo = () => { return 239 }', { lineNumber: 0, columnNumber: 0 }))
      .then(next);
  },

  function argumentsAsCalls(next) {
    var source = 'function foo(){}\nfunction boo(){}\nfunction main(f1,f2){}\nmain(foo(), boo());\n';
    waitForPossibleBreakpoints(source, { lineNumber: 0, columnNumber: 0 })
      .then(message => dumpAllLocations(message, source))
      .then(next);
  }
]);

function compileScript(source, origin) {
  var promise = Protocol.Debugger.onceScriptParsed().then(message => message.params.scriptId);
  if (!origin) origin = { name: '', line_offset: 0, column_offset: 0 };
  contextGroup.addScript(source, origin.line_offset, origin.column_offset, origin.name);
  return promise;
}

function waitForPossibleBreakpoints(source, start, end, origin) {
  return compileScript(source, origin)
    .then(scriptId => { (start || {}).scriptId = scriptId; (end || {}).scriptId = scriptId })
    .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: start, end: end }));
}

function waitForPossibleBreakpointsOnPause(source, start, end, next) {
  var promise = Protocol.Debugger.oncePaused()
    .then(msg => { (start || {}).scriptId = msg.params.callFrames[0].location.scriptId; (end || {}).scriptId = msg.params.callFrames[0].location.scriptId })
    .then(() => Protocol.Debugger.getPossibleBreakpoints({ start: start, end: end }));
  Protocol.Runtime.evaluate({ expression: source }).then(next);
  return promise;
}

function setAllBreakpoints(message) {
  var promises = [];
  for (var location of message.result.locations)
    promises.push(Protocol.Debugger.setBreakpoint({ location: location }).then(checkBreakpoint));
  return Promise.all(promises);
}

function checkBreakpoint(message) {
  if (message.error) {
    InspectorTest.log('FAIL: error in setBreakpoint');
    InspectorTest.logMessage(message);
    return;
  }
  var id_data = message.result.breakpointId.split(':');
  if (parseInt(id_data[1]) !== message.result.actualLocation.lineNumber || parseInt(id_data[2]) !== message.result.actualLocation.columnNumber) {
    InspectorTest.log('FAIL: possible breakpoint was resolved in another location');
  }
}

function dumpAllLocations(message, source, lineOffset, columnOffset) {
  if (message.error) {
    InspectorTest.logMessage(message);
    return;
  }

  lineOffset = lineOffset || 0;
  columnOffset = columnOffset || 0;

  var sourceLines = source.split('\n')
  var lineOffsets = Array(sourceLines.length).fill(0);
  for (var location of message.result.locations) {
    var lineNumber = location.lineNumber - lineOffset;
    var columnNumber = lineNumber !== 0 ? location.columnNumber : location.columnNumber - columnOffset;
    var line = sourceLines[lineNumber] || '';
    var offset = lineOffsets[lineNumber];
    line = line.slice(0, columnNumber + offset) + '#' + line.slice(columnNumber + offset);
    ++lineOffsets[lineNumber];
    sourceLines[lineNumber] = line;
  }
  InspectorTest.log(sourceLines.join('\n'));
  return message;
}

function dumpBreakLocationInSourceAndResume(message, source, lineOffset, columnOffset) {
  lineOffset = lineOffset || 0;
  columnOffset = columnOffset || 0;

  InspectorTest.log('paused in ' + message.params.callFrames[0].functionName);
  var location = message.params.callFrames[0].location;
  var sourceLines = source.split('\n')

  var lineNumber = location.lineNumber - lineOffset;
  var columnNumber = lineNumber !== 0 ? location.columnNumber : location.columnNumber - columnOffset;

  var line = sourceLines[lineNumber];
  line = line.slice(0, columnNumber) + '^' + line.slice(columnNumber);
  sourceLines[lineNumber] = line;
  InspectorTest.log(sourceLines.join('\n'));
  Protocol.Debugger.resume();
}
