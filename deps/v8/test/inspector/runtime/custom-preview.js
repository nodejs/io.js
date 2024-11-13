// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} =
    InspectorTest.start('RemoteObject.CustomPreview');

(async function test() {
  contextGroup.addScript(`
    var a = {name: 'a'};
    var b = {name: 'b'};
    var c = {name: 'c'};
    a.formattableBy1 = true;
    b.formattableBy2 = true;
    c.formattableBy1 = true;
    c.formattableBy2 = true;
    var formatter1 = {
      header: (x) => x.formattableBy1 ? ['span', {}, 'Header formatted by 1 ', x.name] : null,
      hasBody: () => true,
      body: (x) => ['span', {}, 'Body formatted by 1 ', x.name, ['object', {object: {}}]]
    };
    var formatter2 = {
      header: (x) => x.formattableBy2 ? ['span', {}, 'Header formatted by 2 ', x.name] : null,
      hasBody: (x) => true,
      body: (x) => ['span', {}, 'Body formatted by 2 ', x.name]
    };
    var configTest = {};
    var formatterWithConfig1 = {
      header: function(x, config) {
        if (x !== configTest || config)
          return null;
        return ['span', {}, 'Formatter with config ', ['object', {'object': x, 'config': {'info': 'additional info'}}]];
      },
      hasBody: (x) => false,
      body: (x) => { throw 'Unreachable'; }
    }
    var formatterWithConfig2 = {
      header: function(x, config) {
        if (x !== configTest || !config)
          return null;
        return ['span', {}, 'Header ', 'info: ', config.info];
      },
      hasBody: (x, config) => config && config.info,
      body: (x, config) => ['span', {}, 'body', 'info: ', config.info]
    }
    var nullBodyTest = {name: 'nullBodyTest'};
    var formatterWithNullBody = {
      header: (x) => x === nullBodyTest ? ['span', {}, 'Null body: ', x.name] : null,
      hasBody: (x) => true,
      body: (x) => null
    }
    this.devtoolsFormatters = [formatter1, formatter2, formatterWithConfig1, formatterWithConfig2, formatterWithNullBody];
  `);

  Protocol.Runtime.enable();
  Protocol.Runtime.setCustomObjectFormatterEnabled({enabled: true});

  Protocol.Runtime.onConsoleAPICalled(m => InspectorTest.logMessage(m));
  InspectorTest.log('Dump custom previews..');
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'a'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'b'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'c'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'configTest'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'nullBodyTest'}));
  InspectorTest.log('Change formatters order and dump again..');
  await Protocol.Runtime.evaluate({
    expression: 'this.devtoolsFormatters = [formatter2, formatter1, formatterWithConfig1, formatterWithConfig2, formatterWithNullBody]'
  });
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'a'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'b'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'c'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'configTest'}));
  await dumpCustomPreviewForEvaluate(await Protocol.Runtime.evaluate({expression: 'nullBodyTest'}));

  InspectorTest.log('Test Runtime.getProperties');
  const {result:{result:{objectId}}} = await Protocol.Runtime.evaluate({expression: '({a})'});
  const {result:{result}} = await Protocol.Runtime.getProperties({
    objectId, ownProperties: true, generatePreview: true});
  await dumpCustomPreview(result.find(value => value.name === 'a').value);

  InspectorTest.log('Try to break custom preview..');
  await Protocol.Runtime.evaluate({
    expression: `Object.defineProperty(this, 'devtoolsFormatters', {
      get: () => { throw 1; },
      configurable: true
    })`
  });
  Protocol.Runtime.evaluate({ expression: '({})', generatePreview: true });
  InspectorTest.logMessage(await Protocol.Runtime.onceConsoleAPICalled());

  await Protocol.Runtime.evaluate({
    expression: `Object.defineProperty(this, 'devtoolsFormatters', {
      get: () => {
        const arr = [1];
        Object.defineProperty(arr, 0, { get: () => { throw 2; }});
        return arr;
      },
      configurable: true
    })`
  });
  Protocol.Runtime.evaluate({ expression: '({})', generatePreview: true });
  InspectorTest.logMessage(await Protocol.Runtime.onceConsoleAPICalled());

  await Protocol.Runtime.evaluate({
    expression: `Object.defineProperty(this, 'devtoolsFormatters', {
      get: () => [{get header() { throw 3; }}],
      configurable: true
    })`
  });
  Protocol.Runtime.evaluate({ expression: '({})', generatePreview: true });
  InspectorTest.logMessage(await Protocol.Runtime.onceConsoleAPICalled());

  await Protocol.Runtime.evaluate({
    expression: `Object.defineProperty(this, 'devtoolsFormatters', {
      get: () => [{header: () => { throw 4; }}],
      configurable: true
    })`
  });
  Protocol.Runtime.evaluate({ expression: '({})', generatePreview: true });
  InspectorTest.logMessage(await Protocol.Runtime.onceConsoleAPICalled());

  InspectorTest.completeTest();
})()

function dumpCustomPreviewForEvaluate(result) {
  return dumpCustomPreview(result.result.result);
}

async function dumpCustomPreview(result) {
  const { objectId, customPreview } = result;
  if (customPreview.header)
    customPreview.header = JSON.parse(customPreview.header);
  InspectorTest.logMessage(customPreview);
  if (customPreview.bodyGetterId) {
    const body = await Protocol.Runtime.callFunctionOn({
      objectId,
      functionDeclaration: 'function(bodyGetter) { return bodyGetter.call(this); }',
      arguments: [ { objectId: customPreview.bodyGetterId } ],
      returnByValue: true
    });
    InspectorTest.logMessage(body);
  }
}
