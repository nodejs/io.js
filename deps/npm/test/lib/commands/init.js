const t = require('tap')
const fs = require('node:fs/promises')
const nodePath = require('node:path')
const { resolve, basename } = require('node:path')
const _mockNpm = require('../../fixtures/mock-npm')
const { cleanTime } = require('../../fixtures/clean-snapshot')

t.cleanSnapshot = cleanTime

const mockNpm = async (t, { noLog, libnpmexec, initPackageJson, ...opts } = {}) => {
  const res = await _mockNpm(t, {
    ...opts,
    mocks: {
      ...(libnpmexec ? { libnpmexec } : {}),
      ...(initPackageJson ? { 'init-package-json': initPackageJson } : {}),
    },
    globals: {
      // init-package-json prints directly to console.log
      // this avoids poluting test output with those logs
      ...(noLog ? { 'console.log': () => {} } : {}),
    },
  })

  return res
}

t.test('displays output', async t => {
  const { npm, joinedOutput } = await mockNpm(t, {
    initPackageJson: async () => {},
  })

  await npm.exec('init', [])
  t.matchSnapshot(joinedOutput(), 'displays helper info')
})

t.test('classic npm init -y', async t => {
  const { npm, prefix } = await mockNpm(t, {
    config: { yes: true },
    noLog: true,
  })

  await npm.exec('init', [])

  const pkg = require(resolve(prefix, 'package.json'))
  t.equal(pkg.version, '1.0.0')
  t.equal(pkg.license, 'ISC')
})

t.test('classic interactive npm init', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    initPackageJson: async (path) => {
      t.equal(
        path,
        resolve(npm.localPrefix),
        'should start init package.json in expected path'
      )
    },
  })

  await npm.exec('init', [])
})

t.test('npm init <arg>', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['create-react-app@*'],
        'should npx with listed packages'
      )
    },
  })

  await npm.exec('init', ['react-app'])
})

t.test('npm init <arg> -- other-args', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['create-react-app@*', 'my-path', '--some-option', 'some-value'],
        'should npm exec with expected args'
      )
    },

  })

  await npm.exec('init', ['react-app', 'my-path', '--some-option', 'some-value'])
})

t.test('npm init @scope/name', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['@npmcli/create-something@*'],
        'should npx with scoped packages'
      )
    },
  })

  await npm.exec('init', ['@npmcli/something'])
})

t.test('npm init @scope@spec', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['@npmcli/create@foo'],
        'should npx with scoped packages'
      )
    },
  })

  await npm.exec('init', ['@npmcli@foo'])
})

t.test('npm init @scope/name@spec', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['@npmcli/create-something@foo'],
        'should npx with scoped packages'
      )
    },
  })

  await npm.exec('init', ['@npmcli/something@foo'])
})

t.test('npm init git spec', async t => {
  t.plan(1)
  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['npm/create-something'],
        'should npx with git-spec packages'
      )
    },
  })

  await npm.exec('init', ['npm/something'])
})

t.test('npm init @scope', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['@npmcli/create'],
        'should npx with @scope/create pkgs'
      )
    },
  })

  await npm.exec('init', ['@npmcli'])
})

t.test('npm init tgz', async t => {
  const { npm } = await mockNpm(t)

  await t.rejects(
    npm.exec('init', ['something.tgz']),
    /Unrecognized initializer: something.tgz/,
    'should throw error when using an unsupported spec'
  )
})

t.test('npm init <arg>@next', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: ({ args }) => {
      t.same(
        args,
        ['create-something@next'],
        'should npx with something@next'
      )
    },
  })

  await npm.exec('init', ['something@next'])
})

t.test('npm init exec error', async t => {
  const { npm } = await mockNpm(t, {
    libnpmexec: async () => {
      throw new Error('ERROR')
    },
  })

  await t.rejects(
    npm.exec('init', ['something@next']),
    /ERROR/,
    'should exit with exec error'
  )
})

t.test('should not rewrite flatOptions', async t => {
  t.plan(1)

  const { npm } = await mockNpm(t, {
    libnpmexec: async ({ args }) => {
      t.same(
        args,
        ['create-react-app@*', 'my-app'],
        'should npx with extra args'
      )
    },
  })

  await npm.exec('init', ['react-app', 'my-app'])
})

t.test('npm init cancel', async t => {
  const { npm, logs } = await mockNpm(t, {
    initPackageJson: async () => {
      throw new Error('canceled')
    },
  })

  await npm.exec('init', [])

  t.equal(logs.warn[0], 'init canceled', 'should have init title and canceled')
})

t.test('npm init error', async t => {
  const { npm } = await mockNpm(t, {
    initPackageJson: async () => {
      throw new Error('Unknown Error')
    },
  })

  await t.rejects(
    npm.exec('init', []),
    /Unknown Error/,
    'should throw error'
  )
})

t.test('workspaces', async t => {
  await t.test('no args -- yes', async t => {
    const { npm, prefix, joinedOutput } = await mockNpm(t, {
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'top-level',
        }),
      },
      config: { workspace: 'a', yes: true },
      noLog: true,
    })

    await npm.exec('init', [])

    const pkg = require(resolve(prefix, 'a/package.json'))
    t.equal(pkg.name, 'a')
    t.equal(pkg.version, '1.0.0')
    t.equal(pkg.license, 'ISC')

    t.matchSnapshot(joinedOutput(), 'should print helper info')

    const lock = require(resolve(prefix, 'package-lock.json'))
    t.ok(lock.packages.a)
  })

  await t.test('no args, existing folder', async t => {
    const { npm, prefix } = await mockNpm(t, {
      prefixDir: {
        packages: {
          a: {
            'package.json': JSON.stringify({
              name: 'a',
              version: '2.0.0',
            }),
          },
        },
        'package.json': JSON.stringify({
          name: 'top-level',
          workspaces: ['packages/a'],
        }),
      },
      config: { workspace: 'packages/a', yes: true },
      noLog: true,
    })

    await npm.exec('init', [])

    const pkg = require(resolve(prefix, 'packages/a/package.json'))
    t.equal(pkg.name, 'a')
    t.equal(pkg.version, '2.0.0')
    t.equal(pkg.license, 'ISC')
  })

  await t.test('fail parsing top-level package.json to set workspace', async t => {
    const { npm } = await mockNpm(t, {
      prefixDir: {
        'package.json': 'not json[',
      },
      config: { workspace: 'a', yes: true },
      noLog: true,
    })

    await t.rejects(
      npm.exec('init', []),
      { code: 'EJSONPARSE' }
    )
  })

  await t.test('missing top-level package.json when settting workspace', async t => {
    const { npm, logs } = await mockNpm(t, {
      config: { workspace: 'a' },
    })

    await t.rejects(
      npm.exec('init', []),
      { code: 'ENOENT' },
      'should exit with missing package.json file error'
    )

    t.equal(logs.warn[0], 'init Missing package.json. Try with `--include-workspace-root`.')
  })

  await t.test('bad package.json when settting workspace', async t => {
    const { npm, logs } = await mockNpm(t, {
      prefixDir: {
        'package.json': '{{{{',
      },
      config: { workspace: 'a' },
    })

    await t.rejects(
      npm.exec('init', []),
      { code: 'EJSONPARSE' },
      'should exit with parse file error'
    )

    t.strictSame(logs.warn, [])
  })

  await t.test('using args - no package.json', async t => {
    const { npm, prefix } = await mockNpm(t, {
      prefixDir: {
        b: {
          'package.json': JSON.stringify({
            name: 'b',
          }),
        },
        'package.json': JSON.stringify({
          name: 'top-level',
          workspaces: ['b'],
        }),
      },
      // Important: exec did not write a package.json here
      libnpmexec: async () => {},
      config: { workspace: 'a', yes: true },
    })

    await npm.exec('init', ['react-app'])

    const pkg = require(resolve(prefix, 'package.json'))
    t.strictSame(pkg.workspaces, ['b'], 'pkg workspaces did not get updated')
  })

  await t.test('init template - bad package.json', async t => {
    const { npm, prefix } = await mockNpm(t, {
      prefixDir: {
        b: {
          'package.json': JSON.stringify({
            name: 'b',
          }),
        },
        'package.json': JSON.stringify({
          name: 'top-level',
          workspaces: ['b'],
        }),
      },
      initPackageJson: async (...args) => {
        const [dir] = args
        if (dir.endsWith('c')) {
          await fs.writeFile(resolve(dir, 'package.json'), JSON.stringify({
            name: basename(dir),
          }), 'utf-8')
        }
      },
      config: { yes: true, workspace: ['a', 'c'] },
    })

    await npm.exec('init', [])

    const pkg = require(resolve(prefix, 'package.json'))
    t.strictSame(pkg.workspaces, ['b', 'c'])

    const lock = require(resolve(prefix, 'package-lock.json'))
    t.notOk(lock.packages.a)
  })

  t.test('workspace root', async t => {
    const { npm } = await mockNpm(t, {
      config: { workspace: 'packages/a', 'include-workspace-root': true, yes: true },
      noLog: true,
    })

    await npm.exec('init', [])

    const pkg = require(resolve(npm.localPrefix, 'package.json'))
    t.equal(pkg.version, '1.0.0')
    t.equal(pkg.license, 'ISC')
    t.strictSame(pkg.workspaces, ['packages/a'])

    const ws = require(resolve(npm.localPrefix, 'packages/a/package.json'))
    t.equal(ws.version, '1.0.0')
    t.equal(ws.license, 'ISC')
  })
  t.test('init pkg - installed workspace package', async t => {
    const { npm } = await mockNpm(t, {
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'init-ws-test',
          dependencies: {
            '@npmcli/create': '1.0.0',
          },
          workspaces: ['test/workspace-init-a'],
        }),
        'test/workspace-init-a': {
          'package.json': JSON.stringify({
            version: '1.0.0',
            name: '@npmcli/create',
            bin: { 'init-create': 'index.js' },
          }),
          'index.js': `#!/usr/bin/env node
    require('fs').writeFileSync('npm-init-test-success', '')
    console.log('init-create ran')`,
        },
      },
    })
    await npm.exec('install', []) // reify
    npm.config.set('workspace', ['test/workspace-init-b'])
    await npm.exec('init', ['@npmcli'])
    const exists = await fs.stat(nodePath.join(
      npm.prefix, 'test/workspace-init-b', 'npm-init-test-success'))
    t.ok(exists.isFile(), 'bin ran, creating file inside workspace')
  })
})
