'use strict';
const common = require('../common');
const assert = require('assert');
const { execFileSync } = require('child_process');

// system-icu should not be tested
const hasBuiltinICU = process.config.variables.icu_gyp_path === 'tools/icu/icu-generic.gyp';
if (!hasBuiltinICU)
  common.skip('system ICU');

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
    [
      'Fri Jul 25 1980 01:35:33 GMT+0100 (Central European Standard Time)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (中欧标准时间)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (मध्य यूरोपीय मानक समय)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (hora estándar de Europa central)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (heure normale d’Europe centrale)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (توقيت وسط أوروبا الرسمي)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (মধ্য ইউরোপীয় মানক সময়)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (Центральная Европа, стандартное время)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (Horário Padrão da Europa Central)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (وسطی یورپ کا معیاری وقت)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (Waktu Standar Eropa Tengah)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (Mitteleuropäische Normalzeit)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (中央ヨーロッパ標準時)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (Mídúl Yúrop Fíksd Taim)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (मध्‍य युरोपियन प्रमाण वेळ)',
      'Fri Jul 25 1980 01:35:33 GMT+0100 (సెంట్రల్ యూరోపియన్ ప్రామాణిక సమయం)',
    ]
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG, TZ: 'Europe/Zurich' }, 'new Date(333333333333).toLocaleString()')),
    [
      '7/25/1980, 1:35:33 AM',
      '1980/7/25 01:35:33',
      '25/7/1980, 1:35:33 am',
      '25/7/1980, 1:35:33',
      '25/07/1980 01:35:33',
      '25‏/7‏/1980، 1:35:33 ص',
      '২৫/৭/১৯৮০, ১:৩৫:৩৩ AM',
      '25.07.1980, 01:35:33',
      '25/07/1980, 01:35:33',
      '25/7/1980، 1:35:33 AM',
      '25/7/1980, 01.35.33',
      '25.7.1980, 01:35:33',
      '1980/7/25 1:35:33',
      '25/7/1980 01:35:33',
      '२५/७/१९८०, १:३५:३३ AM',
      '25/7/1980 1:35:33 AM',
    ]
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
    [
      '7/25/1980', '1980/7/25',
      '25/7/1980', '25/7/1980',
      '25/07/1980', '25‏/7‏/1980',
      '২৫/৭/১৯৮০', '25.07.1980',
      '25/07/1980', '25/7/1980',
      '25/7/1980', '25.7.1980',
      '1980/7/25', '25/7/1980',
      '२५/७/१९८०', '25/7/1980',
    ]
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.DisplayNames(undefined, { type: "region" }).of("CH")')),
    [
      'Switzerland', '瑞士',
      'स्विट्ज़रलैंड', 'Suiza',
      'Suisse', 'سويسرا',
      'সুইজারল্যান্ড', 'Швейцария',
      'Suíça', 'سوئٹزر لینڈ',
      'Swiss', 'Schweiz',
      'スイス', 'Swítsaland',
      'स्वित्झर्लंड', 'స్విట్జర్లాండ్',
    ]
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.NumberFormat().format(275760.913)')),
    [
      '275,760.913', '275,760.913',
      '2,75,760.913', '275.760,913',
      '275 760,913', '275,760.913',
      '২,৭৫,৭৬০.৯১৩', '275 760,913',
      '275.760,913', '275,760.913',
      '275.760,913', '275.760,913',
      '275,760.913', '275,760.913',
      '२,७५,७६०.९१३', '2,75,760.913',
    ]
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.PluralRules().select(0)')),
    [
      'other', 'other', 'one', 'other',
      'one', 'zero', 'one', 'many',
      'one', 'other', 'other', 'other',
      'other', 'one', 'other', 'other',
    ]
  );
  assert.deepStrictEqual(
    locales.map((LANG) => runEnvOutside({ LANG }, 'new Intl.RelativeTimeFormat().format(-586920.617, "hour")')),
    [
      '586,920.617 hours ago',
      '586,920.617小时前',
      '5,86,920.617 घंटे पहले',
      'hace 586.920,617 horas',
      'il y a 586 920,617 heures',
      'قبل 586,920.617 ساعة',
      '৫,৮৬,৯২০.৬১৭ ঘন্টা আগে',
      '586 920,617 часа назад',
      'há 586.920,617 horas',
      '586,920.617 گھنٹے پہلے',
      '586.920,617 jam yang lalu',
      'vor 586.920,617 Stunden',
      '586,920.617 時間前',
      '586,920.617 áwa wé dọ́n pas',
      '५,८६,९२०.६१७ तासांपूर्वी',
      '5,86,920.617 గంటల క్రితం',
    ]
  );
}


// Tests with process.env mutated inside
{
  // process.env.TZ is not intercepted in Workers
  if (common.isMainThread) {
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
