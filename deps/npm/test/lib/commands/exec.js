const t = require('tap')
const fs = require('node:fs/promises')
const path = require('node:path')
const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const MockRegistry = require('@npmcli/mock-registry')

t.test('call with args', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      call: 'foo',
    },
  })

  await t.rejects(
    npm.exec('exec', ['bar']),
    { code: 'EUSAGE' }
  )
})

t.test('registry package', async t => {
  const registry = new MockRegistry({
    tap: t,
    registry: 'https://registry.npmjs.org/',
  })

  const manifest = registry.manifest({ name: '@npmcli/npx-test' })
  manifest.versions['1.0.0'].bin = { 'npx-test': 'index.js' }

  const { npm } = await loadMockNpm(t, {
    config: {
      audit: false,
      yes: true,
    },
    prefixDir: {
      'npm-exec-test': {
        'package.json': JSON.stringify(manifest),
        'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-success', '')`,
      },
    },
  })

  await registry.package({
    times: 2,
    manifest,
    tarballs: {
      '1.0.0': path.join(npm.prefix, 'npm-exec-test'),
    } })

  await npm.exec('exec', ['@npmcli/npx-test'])
  const exists = await fs.stat(path.join(npm.prefix, 'npm-exec-test-success'))
  t.ok(exists.isFile(), 'bin ran, creating file')
})

t.test('--prefix', async t => {
  const registry = new MockRegistry({
    tap: t,
    registry: 'https://registry.npmjs.org/',
  })

  const manifest = registry.manifest({ name: '@npmcli/npx-test' })
  manifest.versions['1.0.0'].bin = { 'npx-test': 'index.js' }

  const { npm } = await loadMockNpm(t, {
    config: {
      audit: false,
      yes: true,
    },
    prefixDir: {
      'npm-exec-test': {
        'package.json': JSON.stringify(manifest),
        'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-success', '')`,
      },
    },
  })

  // This is what `--prefix` does
  npm.config.globalPrefix = npm.config.localPrefix

  await registry.package({
    manifest,
    tarballs: {
      '1.0.0': path.join(npm.prefix, 'npm-exec-test'),
    } })

  await npm.exec('exec', ['@npmcli/npx-test'])
  const exists = await fs.stat(path.join(npm.prefix, 'npm-exec-test-success'))
  t.ok(exists.isFile(), 'bin ran, creating file')
})

t.test('runs in workspace path', async t => {
  const registry = new MockRegistry({
    tap: t,
    registry: 'https://registry.npmjs.org/',
  })

  const manifest = registry.manifest({ name: '@npmcli/npx-test' })
  manifest.versions['1.0.0'].bin = { 'npx-test': 'index.js' }

  const { npm } = await loadMockNpm(t, {
    config: {
      audit: false,
      yes: true,
      workspace: ['workspace-a'],
    },
    prefixDir: {
      'npm-exec-test': {
        'package.json': JSON.stringify(manifest),
        'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-success', '')`,
      },
      'package.json': JSON.stringify({
        name: '@npmcli/npx-workspace-test',
        workspaces: ['workspace-a'],
      }),
      'workspace-a': {
        'package.json': JSON.stringify({
          name: 'workspace-a',
        }),
      },
    },
  })

  await registry.package({ manifest,
    tarballs: {
      '1.0.0': path.join(npm.prefix, 'npm-exec-test'),
    },
  })
  await npm.exec('exec', ['@npmcli/npx-test'])
  const exists = await fs.stat(path.join(npm.prefix, 'workspace-a', 'npm-exec-test-success'))
  t.ok(exists.isFile(), 'bin ran, creating file inside workspace')
})

t.test('finds workspace bin first', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      workspace: ['workspace-a'],
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npmcli/npx-workspace-root-test',
        bin: { 'npx-test': 'index.js' },
        workspaces: ['workspace-a'],
      }),
      'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-fail', '')`,
      'workspace-a': {
        'package.json': JSON.stringify({
          name: '@npmcli/npx-workspace-test',
          bin: { 'npx-test': 'index.js' },
        }),
        'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-success', '')`,
      },
    },
  })

  await npm.exec('install', []) // reify
  await npm.exec('exec', ['npx-test'])
  const exists = await fs.stat(path.join(npm.prefix, 'workspace-a', 'npm-exec-test-success'))
  t.ok(exists.isFile(), 'bin ran, creating file inside workspace')
  t.rejects(fs.stat(path.join(npm.prefix, 'npm-exec-test-fail')))
})

t.test('finds workspace dep first', async t => {
  const registry = new MockRegistry({
    tap: t,
    registry: 'https://registry.npmjs.org/',
  })

  const manifest = registry.manifest({ name: '@npmcli/subdep', versions: ['1.0.0', '2.0.0'] })
  manifest.versions['1.0.0'].bin = { 'npx-test': 'index.js' }
  manifest.versions['2.0.0'].bin = { 'npx-test': 'index.js' }

  const { npm } = await loadMockNpm(t, {
    prefixDir: {
      subdep: {
        one: {
          'package.json': JSON.stringify(manifest.versions['1.0.0']),
          'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-one', '')`,
        },
        two: {
          'package.json': JSON.stringify(manifest.versions['2.0.0']),
          'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-two', '')`,
        },
      },
      'package.json': JSON.stringify({
        name: '@npmcli/npx-workspace-root-test',
        dependencies: { '@npmcli/subdep': '1.0.0' },
        bin: { 'npx-test': 'index.js' },
        workspaces: ['workspace-a'],
      }),
      'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-fail', '')`,
      'workspace-a': {
        'package.json': JSON.stringify({
          name: '@npmcli/npx-workspace-test',
          dependencies: { '@npmcli/subdep': '2.0.0' },
          bin: { 'npx-test': 'index.js' },
        }),
        'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-success', '')`,
      },
    },
  })

  await registry.package({ manifest,
    tarballs: {
      '1.0.0': path.join(npm.prefix, 'subdep', 'one'),
      '2.0.0': path.join(npm.prefix, 'subdep', 'two'),
    },
  })
  await npm.exec('install', [])
  npm.config.set('workspace', ['workspace-a'])
  await npm.exec('exec', ['npx-test'])
  const exists = await fs.stat(path.join(npm.prefix, 'workspace-a', 'npm-exec-test-success'))
  t.ok(exists.isFile(), 'bin ran, creating file')
})

t.test('npx --no-install @npmcli/npx-test', async t => {
  const registry = new MockRegistry({
    tap: t,
    registry: 'https://registry.npmjs.org/',
  })

  const manifest = registry.manifest({ name: '@npmcli/npx-test' })
  manifest.versions['1.0.0'].bin = { 'npx-test': 'index.js' }

  const { npm } = await loadMockNpm(t, {
    config: {
      audit: false,
      yes: false,
    },
    prefixDir: {
      'npm-exec-test': {
        'package.json': JSON.stringify(manifest),
        'index.js': `#!/usr/bin/env node
  require('fs').writeFileSync('npm-exec-test-success', '')`,
      },
    },
  })

  try {
    await npm.exec('exec', ['@npmcli/npx-test'])
    t.fail('Expected error was not thrown')
  } catch (error) {
    t.match(
      error.message,
      'npx canceled due to missing packages and no YES option: ',
      'Expected error message thrown'
    )
  }
})

t.test('packs from git spec', async t => {
  const spec = 'test/test#111111aaaaaaaabbbbbbbbccccccdddddddeeeee'
  const pkgPath = path.resolve(__dirname, '../../fixtures/git-test.tgz')

  const srv = MockRegistry.tnock(t, 'https://codeload.github.com')
  srv.get('/test/test/tar.gz/111111aaaaaaaabbbbbbbbccccccdddddddeeeee')
    .times(2)
    .reply(200, await fs.readFile(pkgPath))

  const { npm } = await loadMockNpm(t, {
    config: {
      audit: false,
      yes: true,
    },
  })
  try {
    await npm.exec('exec', [spec])
    const exists = await fs.stat(path.join(npm.prefix, 'npm-exec-test-success'))
    t.ok(exists.isFile(), 'bin ran, creating file')
  } catch (err) {
    t.fail(err, "shouldn't throw")
  }
})
