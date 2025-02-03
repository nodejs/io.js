const t = require('tap')
const path = require('node:path')
const tspawk = require('../../fixtures/tspawk')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')

const spawk = tspawk(t)

const npmConfig = {
  config: {
    'ignore-scripts': false,
    editor: 'testeditor',
    'script-shell': process.platform === 'win32' ? process.env.COMSPEC : 'sh',
  },
  prefixDir: {
    node_modules: {
      semver: {
        'package.json': JSON.stringify({
          scripts: {
            install: 'testinstall',
          },
        }),
        node_modules: {
          abbrev: {},
        },
      },
      '@npmcli': {
        'scoped-package': {},
      },
    },
  },
}

const isCmdRe = /(?:^|\\)cmd(?:\.exe)?$/i

t.test('npm edit', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, npmConfig)

  const semverPath = path.resolve(npm.prefix, 'node_modules', 'semver')
  spawk.spawn('testeditor', [semverPath])

  const scriptShell = npm.config.get('script-shell')
  const scriptArgs = isCmdRe.test(scriptShell)
    ? ['/d', '/s', '/c', 'testinstall']
    : ['-c', 'testinstall']
  spawk.spawn(scriptShell, scriptArgs, { cwd: semverPath })

  await npm.exec('edit', ['semver'])
  t.match(joinedOutput(), 'rebuilt dependencies successfully')
})

t.test('rebuild failure', async t => {
  const { npm } = await loadMockNpm(t, npmConfig)

  const semverPath = path.resolve(npm.prefix, 'node_modules', 'semver')
  spawk.spawn('testeditor', [semverPath])

  const scriptShell = npm.config.get('script-shell')
  const scriptArgs = isCmdRe.test(scriptShell)
    ? ['/d', '/s', '/c', 'testinstall']
    : ['-c', 'testinstall']
  spawk.spawn(scriptShell, scriptArgs, { cwd: semverPath }).exit(1).stdout('test error')
  await t.rejects(
    npm.exec('edit', ['semver']),
    { message: 'command failed' }
  )
})

t.test('editor failure', async t => {
  const { npm } = await loadMockNpm(t, npmConfig)

  const semverPath = path.resolve(npm.prefix, 'node_modules', 'semver')
  spawk.spawn('testeditor', [semverPath]).exit(1).stdout('test editor failure')

  await t.rejects(
    npm.exec('edit', ['semver']),
    { message: 'editor process exited with code: 1' }
  )
})

t.test('npm edit editor has flags', async t => {
  const { npm } = await loadMockNpm(t, {
    ...npmConfig,
    config: {
      ...npmConfig.config,
      editor: 'testeditor --flag',
    },
  })

  const semverPath = path.resolve(npm.prefix, 'node_modules', 'semver')
  spawk.spawn('testeditor', ['--flag', semverPath])

  const scriptShell = npm.config.get('script-shell')
  const scriptArgs = isCmdRe.test(scriptShell)
    ? ['/d', '/s', '/c', 'testinstall']
    : ['-c', 'testinstall']
  spawk.spawn(scriptShell, scriptArgs, { cwd: semverPath })

  await npm.exec('edit', ['semver'])
})

t.test('npm edit no args', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('edit', []),
    { code: 'EUSAGE' },
    'throws usage error'
  )
})

t.test('npm edit nonexistent package', async t => {
  const { npm } = await loadMockNpm(t, npmConfig)

  await t.rejects(
    npm.exec('edit', ['abbrev']),
    /lstat/
  )
})

t.test('scoped package', async t => {
  const { npm } = await loadMockNpm(t, npmConfig)
  const scopedPath = path.resolve(npm.prefix, 'node_modules', '@npmcli', 'scoped-package')
  spawk.spawn('testeditor', [scopedPath])
  await npm.exec('edit', ['@npmcli/scoped-package'])
})

t.test('subdependency', async t => {
  const { npm } = await loadMockNpm(t, npmConfig)
  const subdepPath = path.resolve(npm.prefix, 'node_modules', 'semver', 'node_modules', 'abbrev')
  spawk.spawn('testeditor', [subdepPath])
  await npm.exec('edit', ['semver/abbrev'])
})
