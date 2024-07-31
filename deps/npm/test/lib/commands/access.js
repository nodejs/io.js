const t = require('tap')

const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const MockRegistry = require('@npmcli/mock-registry')

const token = 'test-auth-token'
const auth = { '//registry.npmjs.org/:_authToken': 'test-auth-token' }

t.test('completion', async t => {
  const { access } = await loadMockNpm(t, { command: 'access' })
  const testComp = (argv, expect) => {
    const res = access.completion({ conf: { argv: { remain: argv } } })
    t.resolves(res, expect, argv.join(' '))
  }

  testComp(['npm', 'access'], ['list', 'get', 'set', 'grant', 'revoke'])
  testComp(['npm', 'access', 'list'], ['packages', 'collaborators'])
  testComp(['npm', 'access', 'ls'], ['packages', 'collaborators'])
  testComp(['npm', 'access', 'get'], ['status'])
  testComp(['npm', 'access', 'set'], [
    'status=public',
    'status=private',
    'mfa=none',
    'mfa=publish',
    'mfa=automation',
    '2fa=none',
    '2fa=publish',
    '2fa=automation',
  ])
  testComp(['npm', 'access', 'grant'], ['read-only', 'read-write'])
  testComp(['npm', 'access', 'revoke'], [])
  testComp(['npm', 'access', 'grant', ''], [])

  await t.rejects(
    access.completion({ conf: { argv: { remain: ['npm', 'access', 'foobar'] } } }),
    { message: 'foobar not recognized' }
  )
})

t.test('command required', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(npm.exec('access', []), { code: 'EUSAGE' })
})

t.test('unrecognized command', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('access', ['blerg']), { code: 'EUSAGE' })
})

t.test('subcommand required', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(npm.exec('access', ['get']), { code: 'EUSAGE' })
})

t.test('unrecognized subcommand', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(npm.exec('access', ['list', 'blerg']), { code: 'EUSAGE' })
})

t.test('grant', t => {
  t.test('invalid permissions', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(npm.exec('access', ['grant', 'other']), { code: 'EUSAGE' })
  })

  t.test('no permissions', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(npm.exec('access', ['grant', 'read-only']), { code: 'EUSAGE' })
  })

  t.test('read-only', async t => {
    const authToken = 'abcd1234'
    const { npm } = await loadMockNpm(t, {
      config: {
        '//registry.npmjs.org/:_authToken': authToken,
      },
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: authToken,
    })
    const permissions = 'read-only'
    registry.setPermissions({ spec: '@npmcli/test-package', team: '@npm:test-team', permissions })
    await npm.exec('access', ['grant', permissions, '@npm:test-team', '@npmcli/test-package'])
  })
  t.end()
})

t.test('revoke', t => {
  t.test('success', async t => {
    const authToken = 'abcd1234'
    const { npm } = await loadMockNpm(t, {
      config: {
        '//registry.npmjs.org/:_authToken': authToken,
      },
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: authToken,
    })
    registry.removePermissions({ spec: '@npmcli/test-package', team: '@npm:test-team' })
    await npm.exec('access', ['revoke', '@npm:test-team', '@npmcli/test-package'])
  })
  t.end()
})

t.test('list', t => {
  const packages = {
    '@npmcli/test-package': 'read',
    '@npmcli/other-package': 'write',
  }
  const collaborators = {
    npm: 'write',
    github: 'read',
  }
  t.test('invalid subcommand', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(npm.exec('access', ['list', 'other'], { code: 'EUSAGE' }))
  })

  t.test('packages explicit user', async t => {
    const { npm, outputs } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.getPackages({ team: '@npm:test-team', packages })
    await npm.exec('access', ['list', 'packages', '@npm:test-team'])
    t.same(outputs, [
      '@npmcli/other-package: read-write',
      '@npmcli/test-package: read-only',
    ])
  })

  t.test('packages infer user', async t => {
    const { npm, outputs } = await loadMockNpm(t, { config: { ...auth } })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.whoami({ username: 'npm' })
    registry.getPackages({ team: 'npm', packages })
    await npm.exec('access', ['list', 'packages'])
    t.same(outputs, [
      '@npmcli/other-package: read-write',
      '@npmcli/test-package: read-only',
    ])
  })

  t.test('packages json', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, { config: { json: true } })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.getPackages({ team: '@npm:test-team', packages })
    await npm.exec('access', ['list', 'packages', '@npm:test-team'])
    t.same(JSON.parse(joinedOutput()), {
      '@npmcli/test-package': 'read-only',
      '@npmcli/other-package': 'read-write',
    })
  })

  t.test('collaborators explicit package', async t => {
    const { npm, outputs } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.getCollaborators({ spec: '@npmcli/test-package', collaborators })
    await npm.exec('access', ['list', 'collaborators', '@npmcli/test-package'])
    t.same(outputs, [
      'github: read-only',
      'npm: read-write',
    ])
  })

  t.test('collaborators user', async t => {
    const { npm, outputs } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.getCollaborators({ spec: '@npmcli/test-package', collaborators })
    await npm.exec('access', ['list', 'collaborators', '@npmcli/test-package', 'npm'])
    t.same(outputs, [
      'npm: read-write',
    ])
  })
  t.end()
})

t.test('get', t => {
  t.test('invalid subcommand', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(npm.exec('access', ['get', 'other'], { code: 'EUSAGE' }))
  })

  t.test('status explicit package', async t => {
    const { npm, outputs } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.getVisibility({ spec: '@npmcli/test-package', visibility: { public: true } })
    await npm.exec('access', ['get', 'status', '@npmcli/test-package'])
    t.same(outputs, ['@npmcli/test-package: public'])
  })
  t.test('status implicit package', async t => {
    const { npm, outputs } = await loadMockNpm(t, {
      prefixDir: {
        'package.json': JSON.stringify({ name: '@npmcli/test-package' }),
      },
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.getVisibility({ spec: '@npmcli/test-package', visibility: { public: true } })
    await npm.exec('access', ['get', 'status'])
    t.same(outputs, ['@npmcli/test-package: public'])
  })
  t.test('status no package', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(
      npm.exec('access', ['get', 'status']),
      { code: 'ENOENT' }
    )
  })
  t.test('status invalid package', async t => {
    const { npm } = await loadMockNpm(t, {
      prefixDir: { 'package.json': '[not:valid_json}' },
    })
    await t.rejects(
      npm.exec('access', ['get', 'status']),
      { code: 'EJSONPARSE' }
    )
  })
  t.test('status json', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, { config: { json: true } })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.getVisibility({ spec: '@npmcli/test-package', visibility: { public: true } })
    await npm.exec('access', ['get', 'status', '@npmcli/test-package'])
    t.same(JSON.parse(joinedOutput()), { '@npmcli/test-package': 'public' })
  })
  t.end()
})

t.test('set', t => {
  t.test('status=public', async t => {
    const { npm, outputs } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package', body: { access: 'public' } })
    registry.getVisibility({ spec: '@npmcli/test-package', visibility: { public: true } })
    await npm.exec('access', ['set', 'status=public', '@npmcli/test-package'])
    t.same(outputs, ['@npmcli/test-package: public'])
  })
  t.test('status=private', async t => {
    const { npm, outputs } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package', body: { access: 'restricted' } })
    registry.getVisibility({ spec: '@npmcli/test-package', visibility: { public: false } })
    await npm.exec('access', ['set', 'status=private', '@npmcli/test-package'])
    t.same(outputs, ['@npmcli/test-package: private'])
  })
  t.test('status=invalid', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(
      npm.exec('access', ['set', 'status=invalid', '@npmcli/test-package']),
      { code: 'EUSAGE' }
    )
  })
  t.test('status non scoped package', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(
      npm.exec('access', ['set', 'status=public', 'npm']),
      { code: 'EUSAGE' }
    )
  })
  t.test('mfa=none', async t => {
    const { npm } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package',
      body: {
        publish_requires_tfa: false,
      } })
    await npm.exec('access', ['set', 'mfa=none', '@npmcli/test-package'])
  })
  t.test('mfa=publish', async t => {
    const { npm } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package',
      body: {
        publish_requires_tfa: true,
        automation_token_overrides_tfa: false,
      } })
    await npm.exec('access', ['set', 'mfa=publish', '@npmcli/test-package'])
  })
  t.test('mfa=automation', async t => {
    const { npm } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package',
      body: {
        publish_requires_tfa: true,
        automation_token_overrides_tfa: true,
      } })
    await npm.exec('access', ['set', 'mfa=automation', '@npmcli/test-package'])
  })
  t.test('mfa=invalid', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(
      npm.exec('access', ['set', 'mfa=invalid']),
      { code: 'EUSAGE' }
    )
  })
  t.test('2fa=none', async t => {
    const { npm } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package',
      body: {
        publish_requires_tfa: false,
      } })
    await npm.exec('access', ['set', '2fa=none', '@npmcli/test-package'])
  })
  t.test('2fa=publish', async t => {
    const { npm } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package',
      body: {
        publish_requires_tfa: true,
        automation_token_overrides_tfa: false,
      } })
    await npm.exec('access', ['set', '2fa=publish', '@npmcli/test-package'])
  })
  t.test('2fa=automation', async t => {
    const { npm } = await loadMockNpm(t)
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
    })
    registry.setAccess({ spec: '@npmcli/test-package',
      body: {
        publish_requires_tfa: true,
        automation_token_overrides_tfa: true,
      } })
    await npm.exec('access', ['set', '2fa=automation', '@npmcli/test-package'])
  })
  t.test('2fa=invalid', async t => {
    const { npm } = await loadMockNpm(t)
    await t.rejects(
      npm.exec('access', ['set', '2fa=invalid']),
      { code: 'EUSAGE' }
    )
  })

  t.end()
})
