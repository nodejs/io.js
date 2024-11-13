// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} =
  InspectorTest.start('Runtime.getProperties for objects with accessor');

(async function test() {
  let {result:{result:{objectId}}} = await Protocol.Runtime.evaluate({
    expression: 'inspector.createObjectWithNativeDataProperty(\'title\', true)'
  });
  let {result:{result}} = await Protocol.Runtime.getProperties({
    objectId,
    ownProperties: true
  });
  InspectorTest.log('title property with getter and setter:');
  InspectorTest.logMessage(result.find(property => property.name === 'title'));

  ({result:{result:{objectId}}} = await Protocol.Runtime.evaluate({
    expression: 'inspector.createObjectWithNativeDataProperty(\'title\', false)'
  }));
  ({result:{result}} = await Protocol.Runtime.getProperties({
    objectId,
    ownProperties: true
  }));
  InspectorTest.log('title property with getter only:');
  InspectorTest.logMessage(result.find(property => property.name === 'title'));
  InspectorTest.completeTest();
})()
