import * as common from '../common/index.mjs';
import tmpdir from '../common/tmpdir.js';
import { resolve, dirname, sep, relative, join, isAbsolute } from 'node:path';
import { mkdir, writeFile, symlink, glob as asyncGlob } from 'node:fs/promises';
import { glob, globSync, Dirent } from 'node:fs';
import { test, describe } from 'node:test';
import { promisify } from 'node:util';
import assert from 'node:assert';

function assertDirents(dirents) {
  assert.ok(dirents.every((dirent) => dirent instanceof Dirent));
}

tmpdir.refresh();

const fixtureDir = tmpdir.resolve('fixtures');
const absDir = tmpdir.resolve('abs');

async function setup() {
  await mkdir(fixtureDir, { recursive: true });
  await mkdir(absDir, { recursive: true });
  const files = [
    'a/.abcdef/x/y/z/a',
    'a/abcdef/g/h',
    'a/abcfed/g/h',
    'a/b/c/d',
    'a/bc/e/f',
    'a/c/d/c/b',
    'a/cb/e/f',
    'a/x/.y/b',
    'a/z/.y/b',
  ].map((f) => resolve(fixtureDir, f));

  const symlinkTo = resolve(fixtureDir, 'a/symlink/a/b/c');
  const symlinkFrom = '../..';

  for (const file of files) {
    const f = resolve(fixtureDir, file);
    const d = dirname(f);
    await mkdir(d, { recursive: true });
    await writeFile(f, 'i like tests');
  }

  if (!common.isWindows) {
    const d = dirname(symlinkTo);
    await mkdir(d, { recursive: true });
    await symlink(symlinkFrom, symlinkTo, 'dir');
  }

  await Promise.all(['foo', 'bar', 'baz', 'asdf', 'quux', 'qwer', 'rewq'].map(async function(w) {
    await mkdir(resolve(absDir, w), { recursive: true });
  }));
}

await setup();

const patterns = {
  'a/c/d/*/b': ['a/c/d/c/b'],
  'a//c//d//*//b': ['a/c/d/c/b'],
  'a/*/d/*/b': ['a/c/d/c/b'],
  'a/*/+(c|g)/./d': ['a/b/c/d'],
  'a/**/[cg]/../[cg]': [
    'a/abcdef/g',
    'a/abcfed/g',
    'a/b/c',
    'a/c',
    'a/c/d/c',
    common.isWindows ? null : 'a/symlink/a/b/c',
  ],
  'a/{b,c,d,e,f}/**/g': [],
  'a/b/**': ['a/b', 'a/b/c', 'a/b/c/d'],
  'a/{b/**,b/c}': ['a/b', 'a/b/c', 'a/b/c/d'],
  './**/g': ['a/abcdef/g', 'a/abcfed/g'],
  'a/abc{fed,def}/g/h': ['a/abcdef/g/h', 'a/abcfed/g/h'],
  'a/abc{fed/g,def}/**/': ['a/abcdef', 'a/abcdef/g', 'a/abcfed/g'],
  'a/abc{fed/g,def}/**///**/': ['a/abcdef', 'a/abcdef/g', 'a/abcfed/g'],
  '**/a': common.isWindows ? ['a'] : ['a', 'a/symlink/a'],
  '**/a/**': [
    'a',
    'a/abcdef',
    'a/abcdef/g',
    'a/abcdef/g/h',
    'a/abcfed',
    'a/abcfed/g',
    'a/abcfed/g/h',
    'a/b',
    'a/b/c',
    'a/b/c/d',
    'a/bc',
    'a/bc/e',
    'a/bc/e/f',
    'a/c',
    'a/c/d',
    'a/c/d/c',
    'a/c/d/c/b',
    'a/cb',
    'a/cb/e',
    'a/cb/e/f',
    ...(common.isWindows ? [] : [
      'a/symlink',
      'a/symlink/a',
      'a/symlink/a/b',
      'a/symlink/a/b/c',
    ]),
    'a/x',
    'a/z',
  ],
  './**/a': common.isWindows ? ['a'] : ['a', 'a/symlink/a', 'a/symlink/a/b/c/a'],
  './**/a/**/': [
    'a',
    'a/abcdef',
    'a/abcdef/g',
    'a/abcfed',
    'a/abcfed/g',
    'a/b',
    'a/b/c',
    'a/bc',
    'a/bc/e',
    'a/c',
    'a/c/d',
    'a/c/d/c',
    'a/cb',
    'a/cb/e',
    ...(common.isWindows ? [] : [
      'a/symlink',
      'a/symlink/a',
      'a/symlink/a/b',
      'a/symlink/a/b/c',
      'a/symlink/a/b/c/a',
      'a/symlink/a/b/c/a/b',
      'a/symlink/a/b/c/a/b/c',
    ]),
    'a/x',
    'a/z',
  ],
  './**/a/**': [
    'a',
    'a/abcdef',
    'a/abcdef/g',
    'a/abcdef/g/h',
    'a/abcfed',
    'a/abcfed/g',
    'a/abcfed/g/h',
    'a/b',
    'a/b/c',
    'a/b/c/d',
    'a/bc',
    'a/bc/e',
    'a/bc/e/f',
    'a/c',
    'a/c/d',
    'a/c/d/c',
    'a/c/d/c/b',
    'a/cb',
    'a/cb/e',
    'a/cb/e/f',
    ...(common.isWindows ? [] : [
      'a/symlink',
      'a/symlink/a',
      'a/symlink/a/b',
      'a/symlink/a/b/c',
      'a/symlink/a/b/c/a',
      'a/symlink/a/b/c/a/b',
      'a/symlink/a/b/c/a/b/c',
    ]),
    'a/x',
    'a/z',
  ],
  './**/a/**/a/**/': common.isWindows ? [] : [
    'a/symlink/a',
    'a/symlink/a/b',
    'a/symlink/a/b/c',
    'a/symlink/a/b/c/a',
    'a/symlink/a/b/c/a/b',
    'a/symlink/a/b/c/a/b/c',
    'a/symlink/a/b/c/a/b/c/a',
    'a/symlink/a/b/c/a/b/c/a/b',
    'a/symlink/a/b/c/a/b/c/a/b/c',
  ],
  '+(a|b|c)/a{/,bc*}/**': [
    'a/abcdef',
    'a/abcdef/g',
    'a/abcdef/g/h',
    'a/abcfed',
    'a/abcfed/g',
    'a/abcfed/g/h',
  ],
  '*/*/*/f': ['a/bc/e/f', 'a/cb/e/f'],
  './**/f': ['a/bc/e/f', 'a/cb/e/f'],
  'a/symlink/a/b/c/a/b/c/a/b/c//a/b/c////a/b/c/**/b/c/**': common.isWindows ? [] : [
    'a/symlink/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c',
    'a/symlink/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c/a',
    'a/symlink/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c/a/b',
    'a/symlink/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c/a/b/c',
  ],
  [`{./*/*,${absDir}/*}`]: [
    `${absDir}/asdf`,
    `${absDir}/bar`,
    `${absDir}/baz`,
    `${absDir}/foo`,
    `${absDir}/quux`,
    `${absDir}/qwer`,
    `${absDir}/rewq`,
    'a/abcdef',
    'a/abcfed',
    'a/b',
    'a/bc',
    'a/c',
    'a/cb',
    common.isWindows ? null : 'a/symlink',
    'a/x',
    'a/z',
  ],
  [`{${absDir}/*,*}`]: [
    `${absDir}/asdf`,
    `${absDir}/bar`,
    `${absDir}/baz`,
    `${absDir}/foo`,
    `${absDir}/quux`,
    `${absDir}/qwer`,
    `${absDir}/rewq`,
    'a',
  ],
  'a/!(symlink)/**': [
    'a/abcdef',
    'a/abcdef/g',
    'a/abcdef/g/h',
    'a/abcfed',
    'a/abcfed/g',
    'a/abcfed/g/h',
    'a/b',
    'a/b/c',
    'a/b/c/d',
    'a/bc',
    'a/bc/e',
    'a/bc/e/f',
    'a/c',
    'a/c/d',
    'a/c/d/c',
    'a/c/d/c/b',
    'a/cb',
    'a/cb/e',
    'a/cb/e/f',
    'a/x',
    'a/z',
  ],
  'a/symlink/a/**/*': common.isWindows ? [] : [
    'a/symlink/a/b',
    'a/symlink/a/b/c',
    'a/symlink/a/b/c/a',
  ],
  'a/!(symlink)/**/..': [
    'a',
    'a/abcdef',
    'a/abcfed',
    'a/b',
    'a/bc',
    'a/c',
    'a/c/d',
    'a/cb',
  ],
  'a/!(symlink)/**/../': [
    'a',
    'a/abcdef',
    'a/abcfed',
    'a/b',
    'a/bc',
    'a/c',
    'a/c/d',
    'a/cb',
  ],
  'a/!(symlink)/**/../*': [
    'a/abcdef',
    'a/abcdef/g',
    'a/abcfed',
    'a/abcfed/g',
    'a/b',
    'a/b/c',
    'a/bc',
    'a/bc/e',
    'a/c',
    'a/c/d',
    'a/c/d/c',
    'a/cb',
    'a/cb/e',
    common.isWindows ? null : 'a/symlink',
    'a/x',
    'a/z',
  ],
  'a/!(symlink)/**/../*/*': [
    'a/abcdef/g',
    'a/abcdef/g/h',
    'a/abcfed/g',
    'a/abcfed/g/h',
    'a/b/c',
    'a/b/c/d',
    'a/bc/e',
    'a/bc/e/f',
    'a/c/d',
    'a/c/d/c',
    'a/c/d/c/b',
    'a/cb/e',
    'a/cb/e/f',
    common.isWindows ? null : 'a/symlink/a',
  ],
};

describe('glob', function() {
  const promisified = promisify(glob);
  for (const [pattern, expected] of Object.entries(patterns)) {
    test(pattern, async () => {
      const actual = (await promisified(pattern, { cwd: fixtureDir })).sort();
      const normalized = expected.filter(Boolean).map((item) => item.replaceAll('/', sep)).sort();
      assert.deepStrictEqual(actual, normalized);
    });
  }
});

describe('globSync', function() {
  for (const [pattern, expected] of Object.entries(patterns)) {
    test(pattern, () => {
      const actual = globSync(pattern, { cwd: fixtureDir }).sort();
      const normalized = expected.filter(Boolean).map((item) => item.replaceAll('/', sep)).sort();
      assert.deepStrictEqual(actual, normalized);
    });
  }
});

describe('fsPromises glob', function() {
  for (const [pattern, expected] of Object.entries(patterns)) {
    test(pattern, async () => {
      const actual = [];
      for await (const item of asyncGlob(pattern, { cwd: fixtureDir })) actual.push(item);
      actual.sort();
      const normalized = expected.filter(Boolean).map((item) => item.replaceAll('/', sep)).sort();
      assert.deepStrictEqual(actual, normalized);
    });
  }
});

const normalizeDirent = (dirent) => relative(fixtureDir, join(dirent.parentPath, dirent.name));
// The call to `join()` with only one argument is important, as
// it ensures that the proper path seperators are applied.
const normalizePath = (path) => (isAbsolute(path) ? relative(fixtureDir, path) : join(path));

describe('glob - withFileTypes', function() {
  const promisified = promisify(glob);
  for (const [pattern, expected] of Object.entries(patterns)) {
    test(pattern, async () => {
      const actual = await promisified(pattern, {
        cwd: fixtureDir,
        withFileTypes: true,
        exclude: (dirent) => assert.ok(dirent instanceof Dirent),
      });
      assertDirents(actual);
      assert.deepStrictEqual(actual.map(normalizeDirent).sort(), expected.filter(Boolean).map(normalizePath).sort());
    });
  }
});

describe('globSync - withFileTypes', function() {
  for (const [pattern, expected] of Object.entries(patterns)) {
    test(pattern, () => {
      const actual = globSync(pattern, {
        cwd: fixtureDir,
        withFileTypes: true,
        exclude: (dirent) => assert.ok(dirent instanceof Dirent),
      });
      assertDirents(actual);
      assert.deepStrictEqual(actual.map(normalizeDirent).sort(), expected.filter(Boolean).map(normalizePath).sort());
    });
  }
});

describe('fsPromises glob - withFileTypes', function() {
  for (const [pattern, expected] of Object.entries(patterns)) {
    test(pattern, async () => {
      const actual = [];
      for await (const item of asyncGlob(pattern, {
        cwd: fixtureDir,
        withFileTypes: true,
        exclude: (dirent) => assert.ok(dirent instanceof Dirent),
      })) actual.push(item);
      assertDirents(actual);
      assert.deepStrictEqual(actual.map(normalizeDirent).sort(), expected.filter(Boolean).map(normalizePath).sort());
    });
  }
});

// [pattern, exclude option, expected result]
const pattern2 = [
  ['a/{b,c}*', ['a/*c'], ['a/b', 'a/cb']],
  ['a/{a,b,c}*', ['a/*bc*', 'a/cb'], ['a/b', 'a/c']],
  ['a/**/[cg]', ['**/c'], ['a/abcdef/g', 'a/abcfed/g']],
  ['a/**/[cg]', ['./**/c'], ['a/abcdef/g', 'a/abcfed/g']],
  ['a/**/[cg]', ['a/**/[cg]/../c'], ['a/abcdef/g', 'a/abcfed/g']],
  ['a/*/+(c|g)/*', ['**/./h'], ['a/b/c/d']],
  [
    'a/**/[cg]/../[cg]',
    ['a/ab{cde,cfe}*'],
    [
      'a/b/c',
      'a/c',
      'a/c/d/c',
      ...(common.isWindows ? [] : ['a/symlink/a/b/c']),
    ],
  ],
  [
    `${absDir}/*`,
    [`${absDir}/asdf`, `${absDir}/ba*`],
    [`${absDir}/foo`, `${absDir}/quux`, `${absDir}/qwer`, `${absDir}/rewq`],
  ],
  [
    `${absDir}/*`,
    [`${absDir}/asdf`, `**/ba*`],
    [
      `${absDir}/bar`,
      `${absDir}/baz`,
      `${absDir}/foo`,
      `${absDir}/quux`,
      `${absDir}/qwer`,
      `${absDir}/rewq`,
    ],
  ],
  [
    [`${absDir}/*`, 'a/**/[cg]'],
    [`${absDir}/*{a,q}*`, './a/*{c,b}*/*'],
    [`${absDir}/foo`, 'a/c', ...(common.isWindows ? [] : ['a/symlink/a/b/c'])],
  ],
];

describe('globSync - exclude', function() {
  for (const [pattern, exclude] of Object.entries(patterns).map(([k, v]) => [k, v.filter(Boolean)])) {
    test(`${pattern} - exclude: ${exclude}`, () => {
      const actual = globSync(pattern, { cwd: fixtureDir, exclude }).sort();
      assert.strictEqual(actual.length, 0);
    });
  }
  for (const [pattern, exclude, expected] of pattern2) {
    test(`${pattern} - exclude: ${exclude}`, () => {
      const actual = globSync(pattern, { cwd: fixtureDir, exclude }).sort();
      const normalized = expected.filter(Boolean).map((item) => item.replaceAll('/', sep)).sort();
      assert.deepStrictEqual(actual, normalized);
    });
  }
});

describe('glob - exclude', function() {
  const promisified = promisify(glob);
  for (const [pattern, exclude] of Object.entries(patterns).map(([k, v]) => [k, v.filter(Boolean)])) {
    test(`${pattern} - exclude: ${exclude}`, async () => {
      const actual = (await promisified(pattern, { cwd: fixtureDir, exclude })).sort();
      assert.strictEqual(actual.length, 0);
    });
  }
  for (const [pattern, exclude, expected] of pattern2) {
    test(`${pattern} - exclude: ${exclude}`, async () => {
      const actual = (await promisified(pattern, { cwd: fixtureDir, exclude })).sort();
      const normalized = expected.filter(Boolean).map((item) => item.replaceAll('/', sep)).sort();
      assert.deepStrictEqual(actual, normalized);
    });
  }
});

describe('fsPromises glob - exclude', function() {
  for (const [pattern, exclude] of Object.entries(patterns).map(([k, v]) => [k, v.filter(Boolean)])) {
    test(`${pattern} - exclude: ${exclude}`, async () => {
      const actual = [];
      for await (const item of asyncGlob(pattern, { cwd: fixtureDir, exclude })) actual.push(item);
      actual.sort();
      assert.strictEqual(actual.length, 0);
    });
  }
  for (const [pattern, exclude, expected] of pattern2) {
    test(`${pattern} - exclude: ${exclude}`, async () => {
      const actual = [];
      for await (const item of asyncGlob(pattern, { cwd: fixtureDir, exclude })) actual.push(item);
      const normalized = expected.filter(Boolean).map((item) => item.replaceAll('/', sep)).sort();
      assert.deepStrictEqual(actual.sort(), normalized);
    });
  }
});
