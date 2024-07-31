const t = require('tap')
const { readdirSync } = require('node:fs')
const { dirname } = require('node:path')
const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const tmock = require('../../fixtures/tmock.js')
const validateEngines = require('../../../lib/cli/validate-engines.js')

const cliMock = async (t, opts) => {
  let exitHandlerArgs = null
  let npm = null

  const { Npm, ...mock } = await loadMockNpm(t, { ...opts, init: false })
  const cli = tmock(t, '{LIB}/cli/entry.js', {
    '{LIB}/npm.js': Npm,
    '{LIB}/cli/exit-handler.js': class MockExitHandler {
      exit (...args) {
        exitHandlerArgs = args
        npm.unload()
      }

      registerUncaughtHandlers () {}

      setNpm (_npm) {
        npm = _npm
      }
    },
  })

  return {
    ...mock,
    Npm,
    cli: (p) => validateEngines(p, () => cli),
    exitHandlerCalled: () => exitHandlerArgs,
    exitHandlerNpm: () => npm,
  }
}

t.test('print the version, and treat npm_g as npm -g', async t => {
  const { logs, cli, Npm, outputs, exitHandlerCalled } = await cliMock(t, {
    globals: { 'process.argv': ['node', 'npm_g', 'root'] },
  })
  await cli(process)

  t.strictSame(process.argv, ['node', 'npm', '-g', 'root'], 'system process.argv was rewritten')
  t.strictSame(logs.verbose.byTitle('cli'), ['cli node npm'])
  t.strictSame(logs.verbose.byTitle('title'), ['title npm root'])
  t.match(logs.verbose.byTitle('argv'), ['argv "--global" "root"'])
  t.strictSame(logs.info, [`using npm@${Npm.version}`, `using node@${process.version}`])
  t.equal(outputs.length, 1)
  t.match(outputs[0], dirname(process.cwd()))
  t.strictSame(exitHandlerCalled(), [])
})

t.test('calling with --versions calls npm version with no args', async t => {
  const { logs, cli, outputs, exitHandlerCalled } = await cliMock(t, {
    globals: {
      'process.argv': ['node', 'npm', 'install', 'or', 'whatever', '--versions', '--json'],
    },
  })
  await cli(process)

  t.equal(process.title, 'npm install or whatever')
  t.strictSame(logs.verbose.byTitle('cli'), ['cli node npm'])
  t.strictSame(logs.verbose.byTitle('title'), ['title npm install or whatever'])
  t.match(logs.verbose.byTitle('argv'), ['argv "install" "or" "whatever" "--versions"'])
  t.equal(outputs.length, 1)
  t.match(JSON.parse(outputs[0]), { npm: String, node: String, v8: String })
  t.strictSame(exitHandlerCalled(), [])
})

t.test('logged argv is sanitized', async t => {
  const { logs, cli } = await cliMock(t, {
    globals: {
      'process.argv': [
        'node',
        'npm',
        'version',
        '--registry',
        'https://u:password@npmjs.org/password',
      ],
    },
  })

  await cli(process)
  t.equal(process.title, 'npm version')
  t.strictSame(logs.verbose.byTitle('cli'), ['cli node npm'])
  t.strictSame(logs.verbose.byTitle('title'), ['title npm version'])
  t.match(logs.verbose.byTitle('argv'),
    ['argv "version" "--registry" "https://u:***@npmjs.org/password"'])
})

t.test('logged argv is sanitized with equals', async t => {
  const { logs, cli } = await cliMock(t, {
    globals: {
      'process.argv': [
        'node',
        'npm',
        'version',
        '--registry=https://u:password@npmjs.org',
      ],
    },
  })
  await cli(process)

  t.match(logs.verbose.byTitle('argv'), ['argv "version" "--registry" "https://u:***@npmjs.org"'])
})

t.test('print usage if no params provided', async t => {
  const { cli, outputs, exitHandlerCalled, exitHandlerNpm } = await cliMock(t, {
    globals: {
      'process.argv': ['node', 'npm'],
    },
  })
  await cli(process)

  t.match(outputs[0], 'Usage:', 'outputs npm usage')
  t.match(exitHandlerCalled(), [], 'should call exitHandler with no args')
  t.ok(exitHandlerNpm(), 'exitHandler npm is set')
  t.match(process.exitCode, 1)
})

t.test('load error calls error handler', async t => {
  const err = new Error('test load error')
  const { cli, exitHandlerCalled } = await cliMock(t, {
    mocks: {
      '@npmcli/config': class BadConfig {
        async load () {
          throw err
        }
      },
    },
    globals: {
      'process.argv': ['node', 'npm', 'asdf'],
    },
  })
  await cli(process)
  t.strictSame(exitHandlerCalled(), [err])
})

t.test('unsupported node version', async t => {
  const { cli, logs } = await cliMock(t, {
    globals: {
      'process.version': '12.6.0',
    },
  })
  await cli(process)
  t.match(
    logs.warn[0],
    /npm v.* does not support Node\.js 12\.6\.0\./
  )
})

t.test('non-ascii dash', async t => {
  const { cli, logs } = await cliMock(t, {
    globals: {
      'process.argv': ['node', 'npm', 'scope', '\u2010not-a-dash'],
    },
  })
  await cli(process)
  t.equal(
    logs.error[0],
    'arg Argument starts with non-ascii dash, this is probably invalid: \u2010not-a-dash'
  )
})

t.test('exit early for --version', async t => {
  const { cli, outputs, Npm, cache } = await cliMock(t, {
    globals: {
      'process.argv': ['node', 'npm', '-v'],
    },
  })
  await cli(process)
  t.strictSame(readdirSync(cache), [], 'nothing created in cache')
  t.equal(outputs[0], Npm.version)
})
