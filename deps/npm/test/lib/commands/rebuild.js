const t = require('tap')
const fs = require('node:fs')
const { resolve } = require('node:path')
const setupMockNpm = require('../../fixtures/mock-npm')

t.test('no args', async t => {
  const { npm, joinedOutput, prefix: path } = await setupMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
            bin: 'cwd',
            scripts: {
              preinstall: "node -e \"require('fs').writeFileSync('cwd', '')\"",
            },
          }),
        },
        b: {
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
            bin: 'cwd',
            scripts: {
              preinstall: "node -e \"require('fs').writeFileSync('cwd', '')\"",
            },
          }),
        },
      },
    },
  })

  const aBuildFile = resolve(path, 'node_modules/a/cwd')
  const bBuildFile = resolve(path, 'node_modules/b/cwd')
  const aBinFile = resolve(path, 'node_modules/.bin/a')
  const bBinFile = resolve(path, 'node_modules/.bin/b')
  t.throws(() => fs.statSync(aBuildFile))
  t.throws(() => fs.statSync(bBuildFile))
  t.throws(() => fs.statSync(aBinFile))
  t.throws(() => fs.statSync(bBinFile))

  await npm.exec('rebuild', [])

  t.ok(() => fs.statSync(aBuildFile))
  t.ok(() => fs.statSync(bBuildFile))
  t.ok(() => fs.statSync(aBinFile))
  t.ok(() => fs.statSync(bBinFile))

  t.equal(
    joinedOutput(),
    'rebuilt dependencies successfully',
    'should output success msg'
  )
})

t.test('filter by pkg name', async t => {
  const { npm, prefix: path } = await setupMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          'index.js': '',
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
            bin: 'index.js',
          }),
        },
        b: {
          'index.js': '',
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
            bin: 'index.js',
          }),
        },
      },
    },
  })

  const aBinFile = resolve(path, 'node_modules/.bin/a')
  const bBinFile = resolve(path, 'node_modules/.bin/b')
  t.throws(() => fs.statSync(aBinFile))
  t.throws(() => fs.statSync(bBinFile))

  await npm.exec('rebuild', ['b'])

  t.throws(() => fs.statSync(aBinFile), 'should not link a bin')
  t.ok(() => fs.statSync(bBinFile), 'should link filtered pkg bin')
})

t.test('filter by pkg@<range>', async t => {
  const { npm, prefix: path } = await setupMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          'index.js': '',
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
            bin: 'index.js',
          }),
          node_modules: {
            b: {
              'index.js': '',
              'package.json': JSON.stringify({
                name: 'b',
                version: '2.0.0',
                bin: 'index.js',
              }),
            },
          },
        },
        b: {
          'index.js': '',
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
            bin: 'index.js',
          }),
        },
      },
    },
  })

  const bBinFile = resolve(path, 'node_modules/.bin/b')
  const nestedBinFile = resolve(path, 'node_modules/a/node_modules/.bin/b')

  await npm.exec('rebuild', ['b@2'])

  t.throws(() => fs.statSync(bBinFile), 'should not link b bin')
  t.ok(() => fs.statSync(nestedBinFile), 'should link filtered pkg bin')
})

t.test('filter by directory', async t => {
  const { npm, prefix: path } = await setupMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          'index.js': '',
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
            bin: 'index.js',
          }),
        },
        b: {
          'index.js': '',
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
            bin: 'index.js',
          }),
        },
      },
    },
  })

  const aBinFile = resolve(path, 'node_modules/.bin/a')
  const bBinFile = resolve(path, 'node_modules/.bin/b')
  t.throws(() => fs.statSync(aBinFile))
  t.throws(() => fs.statSync(bBinFile))

  await npm.exec('rebuild', ['file:node_modules/b'])

  t.throws(() => fs.statSync(aBinFile), 'should not link a bin')
  t.ok(() => fs.statSync(bBinFile), 'should link filtered pkg bin')
})

t.test('filter must be a semver version/range, or directory', async t => {
  const { npm } = await setupMockNpm(t)

  await t.rejects(
    npm.exec('rebuild', ['git+ssh://github.com/npm/arborist']),
    /`npm rebuild` only supports SemVer version\/range specifiers/,
    'should throw type error'
  )
})

t.test('global prefix', async t => {
  const { npm, globalPrefix, joinedOutput } = await setupMockNpm(t, {
    config: {
      global: true,
    },
    globalPrefixDir: {
      node_modules: {
        a: {
          'index.js': '',
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
            bin: 'index.js',
          }),
        },
      },
    },
  })

  await npm.exec('rebuild', [])
  t.ok(() => fs.statSync(resolve(globalPrefix, 'lib/node_modules/.bin/a')))

  t.equal(
    joinedOutput(),
    'rebuilt dependencies successfully',
    'should output success msg'
  )
})
