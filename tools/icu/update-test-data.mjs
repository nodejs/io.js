/*
 * This script updates the `test/fixtures/icu/localizationData.json` data
 * used by `test/parallel/test-icu-env.js` test.
 * Run this script after an ICU update if locale-specific output changes are
 * causing the test to fail.
 * Typically, only a few strings change with each ICU update. If this script
 * suddenly generates identical values for all locales, it indicates a bug.
 * Note that Node.js must be built with either `--with-intl=full-icu` after
 * updating ICU, or with `--with-intl=system-icu` if system version matches.
 * Wrong version or small-icu might produce wrong values.
 * Manually editing the json file is fine, too.
 */

import { execFileSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';

const locales = [
  'en', 'zh', 'hi', 'es',
  'fr', 'ar', 'bn', 'ru',
  'pt', 'ur', 'id', 'de',
  'ja', 'pcm', 'mr', 'te',
];

const outputFilePath = new URL(`../../test/fixtures/icu/localizationData-v${process.versions.icu}.json`, import.meta.url);

const runEnvCommand = (envVars, code) =>
  execFileSync(
    process.execPath,
    ['-e', `process.stdout.write(String(${code}));`],
    { env: { ...process.env, ...envVars }, encoding: 'utf8' },
  );

// Generate the localization data for all locales
const localizationData = locales.reduce((acc, locale) => {
  acc.dateStrings[locale] = runEnvCommand(
    { LANG: locale, TZ: 'Europe/Zurich' },
    `new Date(333333333333).toString()`,
  );

  acc.dateTimeFormats[locale] = runEnvCommand(
    { LANG: locale, TZ: 'Europe/Zurich' },
    `new Date(333333333333).toLocaleString()`,
  );

  acc.dateFormats[locale] = runEnvCommand(
    { LANG: locale, TZ: 'Europe/Zurich' },
    `new Intl.DateTimeFormat().format(333333333333)`,
  );

  acc.displayNames[locale] = runEnvCommand(
    { LANG: locale },
    `new Intl.DisplayNames(undefined, { type: "region" }).of("CH")`,
  );

  acc.numberFormats[locale] = runEnvCommand(
    { LANG: locale },
    `new Intl.NumberFormat().format(275760.913)`,
  );

  acc.pluralRules[locale] = runEnvCommand(
    { LANG: locale },
    `new Intl.PluralRules().select(0)`,
  );

  acc.relativeTime[locale] = runEnvCommand(
    { LANG: locale },
    `new Intl.RelativeTimeFormat().format(-586920.617, "hour")`,
  );

  return acc;
}, {
  dateStrings: {},
  dateTimeFormats: {},
  dateFormats: {},
  displayNames: {},
  numberFormats: {},
  pluralRules: {},
  relativeTime: {},
});

writeFileSync(outputFilePath, JSON.stringify(localizationData, null, 2) + '\n');
