const t = require('tap')
const fs = require('node:fs')
const { resolve } = require('node:path')
const _mockNpm = require('../../fixtures/mock-npm')

const mockNpm = async (t, opts = {}) => {
  const res = await _mockNpm(t, {
    ...opts,
    mocks: {
      ...opts.mocks,
      '{LIB}/utils/reify-finish.js': async () => {},
    },
  })

  return {
    ...res,
    uninstall: (args) => res.npm.exec('uninstall', args),
  }
}

t.test('remove single installed lib', async t => {
  const { uninstall, prefix } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'test-rm-single-lib',
        version: '1.0.0',
        dependencies: {
          a: '*',
          b: '*',
        },
      }),
      node_modules: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
        b: {
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
          }),
        },
      },
      'package-lock.json': JSON.stringify({
        name: 'test-rm-single-lib',
        version: '1.0.0',
        lockfileVersion: 2,
        requires: true,
        packages: {
          '': {
            name: 'test-rm-single-lib',
            version: '1.0.0',
            dependencies: {
              a: '*',
            },
          },
          'node_modules/a': {
            version: '1.0.0',
          },
          'node_modules/b': {
            version: '1.0.0',
          },
        },
        dependencies: {
          a: {
            version: '1.0.0',
          },
          b: {
            version: '1.0.0',
          },
        },
      }),
    },
  })

  const b = resolve(prefix, 'node_modules/b')
  t.ok(fs.statSync(b))

  await uninstall(['b'])

  t.throws(() => fs.statSync(b), 'should have removed package from npm')
})

t.test('remove multiple installed libs', async t => {
  const { uninstall, prefix } = await mockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
        b: {
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
          }),
        },
      },
      'package-lock.json': JSON.stringify({
        name: 'test-rm-single-lib',
        version: '1.0.0',
        lockfileVersion: 2,
        requires: true,
        packages: {
          '': {
            name: 'test-rm-single-lib',
            version: '1.0.0',
            dependencies: {
              a: '*',
            },
          },
          'node_modules/a': {
            version: '1.0.0',
          },
          'node_modules/b': {
            version: '1.0.0',
          },
        },
        dependencies: {
          a: {
            version: '1.0.0',
          },
          b: {
            version: '1.0.0',
          },
        },
      }),
    },
  })

  const a = resolve(prefix, 'node_modules/a')
  const b = resolve(prefix, 'node_modules/b')
  t.ok(fs.statSync(a))
  t.ok(fs.statSync(b))

  await uninstall(['b'])

  t.throws(() => fs.statSync(a), 'should have removed a package from nm')
  t.throws(() => fs.statSync(b), 'should have removed b package from nm')
})

t.test('no args local', async t => {
  const { uninstall } = await mockNpm(t)

  await t.rejects(
    uninstall([]),
    /Must provide a package name to remove/,
    'should throw package name required error'
  )
})

t.test('no args global', async t => {
  const { uninstall, npm } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'a',
        version: '1.0.0',
      }),
    },
    globalPrefixDir: {
      node_modules: {
        a: t.fixture('symlink', '../../prefix'),
      },
    },
    config: { global: true },
  })

  const a = resolve(npm.globalDir, 'a')
  t.ok(fs.statSync(a))

  await uninstall([])

  t.throws(() => fs.statSync(a), 'should have removed global nm symlink')
})

t.test('no args global but no package.json', async t => {
  const { uninstall } = await mockNpm(t, {
    config: { global: true },
  })

  await t.rejects(
    uninstall([]),
    /npm uninstall/
  )
})

t.test('non ENOENT error reading from localPrefix package.json', async t => {
  const { uninstall } = await mockNpm(t, {
    config: { global: true },
    prefixDir: { 'package.json': 'not[json]' },
  })

  await t.rejects(
    uninstall([]),
    { code: 'EJSONPARSE' },
    'should throw non ENOENT error'
  )
})
