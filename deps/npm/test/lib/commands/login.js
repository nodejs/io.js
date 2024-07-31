const t = require('tap')
const fs = require('node:fs')
const path = require('node:path')
const stream = require('node:stream')
const ini = require('ini')

const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const mockGlobals = require('@npmcli/mock-globals')
const MockRegistry = require('@npmcli/mock-registry')

const mockLogin = async (t, { stdin: stdinLines, registry: registryUrl, ...options } = {}) => {
  let stdin
  if (stdinLines) {
    stdin = new stream.PassThrough()
    for (const l of stdinLines) {
      stdin.write(l + '\n')
    }
    mockGlobals(t, {
      'process.stdin': stdin,
      'process.stdout': new stream.PassThrough(), // to quiet readline
    }, { replace: true })
  }
  const mock = await loadMockNpm(t, {
    ...options,
    command: 'login',
  })
  const registry = new MockRegistry({
    tap: t,
    registry: registryUrl ?? mock.npm.config.get('registry'),
  })
  return {
    registry,
    stdin,
    rc: () => ini.parse(fs.readFileSync(path.join(mock.home, '.npmrc'), 'utf8')),
    ...mock,
  }
}

t.test('usage', async t => {
  const { login } = await loadMockNpm(t, { command: 'login' })
  t.match(login.usage, 'login', 'usage has command name in it')
})

t.test('legacy', t => {
  t.test('basic login', async t => {
    const { npm, registry, login, rc } = await mockLogin(t, {
      stdin: ['test-user', 'test-password'],
      config: { 'auth-type': 'legacy' },
      homeDir: {
        '.npmrc': [
          '//registry.npmjs.org/:_authToken=user',
          '//registry.npmjs.org/:always-auth=user',
          '//registry.npmjs.org/:email=test-email-old@npmjs.org',
        ].join('\n'),
      },
    })
    registry.couchlogin({
      username: 'test-user',
      password: 'test-password',
      token: 'npm_test-token',
    })
    await login.exec([])
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(rc(), {
      '//registry.npmjs.org/:_authToken': 'npm_test-token',
      email: 'test-email-old@npmjs.org',
    }, 'should only have token and un-nerfed old email')
  })

  t.test('scoped login default registry', async t => {
    const { npm, registry, login, rc } = await mockLogin(t, {
      stdin: ['test-user', 'test-password'],
      config: {
        'auth-type': 'legacy',
        scope: '@npmcli',
      },
    })
    registry.couchlogin({
      username: 'test-user',
      password: 'test-password',
      token: 'npm_test-token',
    })
    await login.exec([])
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(npm.config.get('@npmcli:registry'), 'https://registry.npmjs.org/')
    t.same(rc(), {
      '//registry.npmjs.org/:_authToken': 'npm_test-token',
      '@npmcli:registry': 'https://registry.npmjs.org/',
    }, 'should only have token and scope:registry')
  })

  t.test('scoped login scoped registry', async t => {
    const { npm, registry, login, rc } = await mockLogin(t, {
      stdin: ['test-user', 'test-password'],
      registry: 'https://diff-registry.npmjs.org',
      config: {
        'auth-type': 'legacy',
        scope: '@npmcli',
      },
      homeDir: {
        '.npmrc': '@npmcli:registry=https://diff-registry.npmjs.org',
      },
    })
    registry.couchlogin({
      username: 'test-user',
      password: 'test-password',
      token: 'npm_test-token',
    })
    await login.exec([])
    t.same(npm.config.get('//diff-registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(npm.config.get('@npmcli:registry'), 'https://diff-registry.npmjs.org')
    t.same(rc(), {
      '@npmcli:registry': 'https://diff-registry.npmjs.org',
      '//diff-registry.npmjs.org/:_authToken': 'npm_test-token',
    }, 'should only have token and scope:registry')
  })
  t.end()
})

t.test('web', t => {
  t.test('basic login', async t => {
    const { npm, registry, login, rc } = await mockLogin(t, {
      config: { 'auth-type': 'web' },
    })
    registry.weblogin({ token: 'npm_test-token' })
    await login.exec([])
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(rc(), {
      '//registry.npmjs.org/:_authToken': 'npm_test-token',
    })
  })
  t.test('server error', async t => {
    const { registry, login } = await mockLogin(t, {
      config: { 'auth-type': 'web' },
    })
    registry.nock.post(registry.fullPath('/-/v1/login'))
      .reply(503, {})
    await t.rejects(
      login.exec([]),
      { message: /503/ }
    )
  })
  t.test('fallback', async t => {
    const { npm, registry, login } = await mockLogin(t, {
      stdin: ['test-user', 'test-password'],
      config: { 'auth-type': 'web' },
    })
    registry.nock.post(registry.fullPath('/-/v1/login'))
      .reply(404, {})
    registry.couchlogin({
      username: 'test-user',
      password: 'test-password',
      token: 'npm_test-token',
    })
    await login.exec([])
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
  })
  t.end()
})
