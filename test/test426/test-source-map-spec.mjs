import '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';

import assert from 'node:assert';
import test from 'node:test';
import { SourceMap } from 'node:module';
import { readFileSync } from 'node:fs';

const specJson = fixtures.readSync('test426/source-map-spec-tests.json', 'utf8');
const spec = JSON.parse(specJson);

const kStatus = JSON.parse(readFileSync(new URL('./status/source-map-spec-tests.json', import.meta.url), 'utf8'));

const kActionRunner = {
  checkMapping,
};

spec.tests.forEach((testSpec) => {
  const sourceMapJson = fixtures.readSync(['test426/resources', testSpec.sourceMapFile], 'utf8');
  const sourceMapPayload = JSON.parse(sourceMapJson);

  test(testSpec.name, async (t) => {
    let sourceMap;
    try {
      sourceMap = new SourceMap(sourceMapPayload);
    } catch {
      // Be lenient with invalid source maps. Some source maps are invalid spec-wise,
      // let's try the best to parse them. Maybe a strict parsing mode can be introduced
      // for future-proof source map format.
      assert.ok(!testSpec.sourceMapIsValid);
      return;
    }
    assert.ok(sourceMap);

    const subtests = (testSpec.testActions ?? []).map((action, idx) => {
      const testRunner = kActionRunner[action.actionType];
      const testName = `action#${idx} - ${action.actionType}`;
      if (testRunner == null) {
        return t.test(testName, {
          todo: true,
        });
      }
      const skip = kStatus[testSpec.name]?.testActions?.skip || kStatus[testSpec.name]?.testActions?.[idx]?.skip;
      return t.test(testName, {
        skip,
      }, testRunner.bind(null, sourceMap, action));
    });

    await Promise.all(subtests);
  });
});

function checkMapping(sourceMap, action) {
  const result = sourceMap.findEntry(action.generatedLine, action.generatedColumn);
  assert.strictEqual(result.originalSource, action.originalSource);
  assert.strictEqual(result.originalLine, action.originalLine);
  assert.strictEqual(result.originalColumn, action.originalColumn);
}
