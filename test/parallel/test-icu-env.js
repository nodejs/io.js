'use strict';
const common = require('../common');
const assert = require('assert');
const { execFileSync } = require('child_process');
const { readFileSync, globSync } = require('fs');
const { path } = require('../common/fixtures');
const { isMainThread } = require('worker_threads');

// This test checks for regressions in environment variable handling and
// caching, but the localization data originated from ICU might change
// over time.
//
// The json file can be updated using `tools/icu/update-test-data.js`
// whenever ICU is updated. Run the update script if this test fails after
// an ICU update, and verify that only expected values are updated.
// Typically, only a few strings change with each ICU update. If this script
// suddenly generates identical values for all locales, it indicates a bug.
// Editing json file manually is also fine.
const localizationDataFile = path(`icu/localizationData-v${process.versions.icu}.json`);

let localizationData;
try {
  localizationData = JSON.parse(readFileSync(localizationDataFile));
} catch ({ code }) {
  assert.strictEqual(code, 'ENOENT');

  // No data for current version, try latest known version
  const [ latestVersion ] = globSync('test/fixtures/icu/localizationData-*.json')
    .map((file) => file.match(/localizationData-v(.*)\.json/)[1])
    .sort((a, b) => b.localeCompare(a, undefined, { numeric: true }));
  console.log(`The ICU is v${process.versions.icu}, but there is no fixture for this version. ` +
  `Trying the latest known version: v${latestVersion}. If this test fails with a few strings changed ` +
  `after ICU update, run this: \n${process.argv[0]} tools/icu/update-test-data.mjs\n`);
  localizationData = JSON.parse(readFileSync(path(`icu/localizationData-v${latestVersion}.json`)));
}


// small-icu doesn't support non-English locales
const hasFullICU = (() => {
  try {
    const january = new Date(9e8);
    const spanish = new Intl.DateTimeFormat('es', { month: 'long' });
    return spanish.format(january) === 'enero';
  } catch {
    return false;
  }
})();
if (!hasFullICU)
  common.skip('small ICU');

const icuVersionMajor = Number(process.config.variables.icu_ver_major ?? 0);
if (icuVersionMajor < 71)
  common.skip('ICU too old');


function runEnvOutside(addEnv, code, ...args) {
  return execFileSync(
    process.execPath,
    ['-e', `process.stdout.write(String(${code}));`],
    { env: { ...process.env, ...addEnv }, encoding: 'utf8' }
  );
}

function runEnvInside(addEnv, func, ...args) {
  Object.assign(process.env, addEnv); // side effects!
  return func(...args);
}

function isPack(array) {
  const firstItem = array[0];
  return array.every((item) => item === firstItem);
}

function isSet(array) {
  const deduped = new Set(array);
  return array.length === deduped.size;
}


const localesISO639 = [
  'eng', 'cmn', 'hin', 'spa',
  'fra', 'arb', 'ben', 'rus',
  'por', 'urd', 'ind', 'deu',
  'jpn', 'pcm', 'mar', 'tel',
];

const locales = [
  'en', 'zh', 'hi', 'es',
  'fr', 'ar', 'bn', 'ru',
  'pt', 'ur', 'id', 'de',
  'ja', 'pcm', 'mr', 'te',
];

// These must not overlap
const zones = [
  'America/New_York',
  'UTC',
  'Asia/Irkutsk',
  'Australia/North',
  'Antarctica/South_Pole',
];


assert.deepStrictEqual(Intl.getCanonicalLocales(localesISO639), locales);


// On some platforms these keep original locale (for example, 'January')
const enero = runEnvOutside(
  { LANG: 'es' },
  'new Intl.DateTimeFormat(undefined, { month: "long" } ).format(new Date(9e8))'
);
const janvier = runEnvOutside(
  { LANG: 'fr' },
  'new Intl.DateTimeFormat(undefined, { month: "long" } ).format(new Date(9e8))'
);
const isMockable = enero !== janvier;

// Tests with mocked env
if (isMockable) {
  assert.strictEqual(
    isSet(zones.map((TZ) => runEnvOutside({ TZ }, 'new Date(333333333333).toString()'))),
    true
  );
  assert.strictEqual(
    isSet(zones.map((TZ) => runEnvOutside({ TZ }, 'new Date(333333333333).toLocaleString()'))),
    true
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG, TZ: 'Europe/Zurich' }, 'new Date(333333333333).toString()')),
    Object.values(localizationData.dateStrings)
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG, TZ: 'Europe/Zurich' }, 'new Date(333333333333).toLocaleString()')),
    Object.values(localizationData.dateTimeFormats)
  );
  assert.strictEqual(
    runEnvOutside({ LANG: 'en' }, '["z", "ä"].sort(new Intl.Collator().compare)'),
    'ä,z'
  );
  assert.strictEqual(
    runEnvOutside({ LANG: 'sv' }, '["z", "ä"].sort(new Intl.Collator().compare)'),
    'z,ä'
  );
  assert.deepStrictEqual(
    locales.map(
      (LANG) => runEnvOutside({ LANG, TZ: 'Europe/Zurich' }, 'new Intl.DateTimeFormat().format(333333333333)')
    ),
    Object.values(localizationData.dateFormats)
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.DisplayNames(undefined, { type: "region" }).of("CH")')),
    Object.values(localizationData.displayNames)
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.NumberFormat().format(275760.913)')),
    Object.values(localizationData.numberFormats)
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.PluralRules().select(0)')),
    Object.values(localizationData.pluralRules)
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.RelativeTimeFormat().format(-586920.617, "hour")')),
    Object.values(localizationData.relativeTime)
  );
}


// Tests with process.env mutated inside
{
  // process.env.TZ is not intercepted in Workers
  if (isMainThread) {
    assert.strictEqual(
      isSet(zones.map((TZ) => runEnvInside({ TZ }, () => new Date(333333333333).toString()))),
      true
    );
    assert.strictEqual(
      isSet(zones.map((TZ) => runEnvInside({ TZ }, () => new Date(333333333333).toLocaleString()))),
      true
    );
  } else {
    assert.strictEqual(
      isPack(zones.map((TZ) => runEnvInside({ TZ }, () => new Date(333333333333).toString()))),
      true
    );
    assert.strictEqual(
      isPack(zones.map((TZ) => runEnvInside({ TZ }, () => new Date(333333333333).toLocaleString()))),
      true
    );
  }

  assert.strictEqual(
    isPack(locales.map((LANG) => runEnvInside({ LANG, TZ: 'Europe/Zurich' }, () => new Date(333333333333).toString()))),
    true
  );
  assert.strictEqual(
    isPack(locales.map(
      (LANG) => runEnvInside({ LANG, TZ: 'Europe/Zurich' }, () => new Date(333333333333).toLocaleString())
    )),
    true
  );
  assert.deepStrictEqual(
    runEnvInside({ LANG: 'en' }, () => ['z', 'ä'].sort(new Intl.Collator().compare)),
    runEnvInside({ LANG: 'sv' }, () => ['z', 'ä'].sort(new Intl.Collator().compare))
  );
  assert.strictEqual(
    isPack(locales.map(
      (LANG) => runEnvInside({ LANG, TZ: 'Europe/Zurich' }, () => new Intl.DateTimeFormat().format(333333333333))
    )),
    true
  );
  assert.strictEqual(
    isPack(locales.map(
      (LANG) => runEnvInside({ LANG }, () => new Intl.DisplayNames(undefined, { type: 'region' }).of('CH'))
    )),
    true
  );
  assert.strictEqual(
    isPack(locales.map((LANG) => runEnvInside({ LANG }, () => new Intl.NumberFormat().format(275760.913)))),
    true
  );
  assert.strictEqual(
    isPack(locales.map((LANG) => runEnvInside({ LANG }, () => new Intl.PluralRules().select(0)))),
    true
  );
  assert.strictEqual(
    isPack(locales.map(
      (LANG) => runEnvInside({ LANG }, () => new Intl.RelativeTimeFormat().format(-586920.617, 'hour'))
    )),
    true
  );
}
