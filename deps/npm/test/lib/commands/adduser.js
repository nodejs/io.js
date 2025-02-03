const t = require('tap')
const fs = require('node:fs')
const path = require('node:path')
const ini = require('ini')

const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const mockGlobals = require('@npmcli/mock-globals')
const MockRegistry = require('@npmcli/mock-registry')
const stream = require('node:stream')

const mockAddUser = async (t, { stdin: stdinLines, registry: registryUrl, ...options } = {}) => {
  if (stdinLines) {
    const stdin = new stream.PassThrough()
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
    command: 'adduser',
  })
  const registry = new MockRegistry({
    tap: t,
    registry: registryUrl ?? mock.npm.config.get('registry'),
  })
  return {
    registry,
    rc: () => ini.parse(fs.readFileSync(path.join(mock.home, '.npmrc'), 'utf8')),
    ...mock,
  }
}

t.test('usage', async t => {
  const { adduser } = await loadMockNpm(t, { command: 'adduser' })
  t.match(adduser.usage, 'adduser', 'usage has command name in it')
})

t.test('legacy', async t => {
  t.test('simple adduser', async t => {
    const { npm, rc, registry, adduser } = await mockAddUser(t, {
      stdin: ['test-user', 'test-password', 'test-email@npmjs.org'],
      config: { 'auth-type': 'legacy' },
      homeDir: {
        '.npmrc': [
          '//registry.npmjs.org/:_authToken=user',
          '//registry.npmjs.org/:always-auth=user',
          '//registry.npmjs.org/:email=test-email-old@npmjs.org',
        ].join('\n'),
      },
    })
    registry.couchadduser({
      username: 'test-user',
      password: 'test-password',
      email: 'test-email@npmjs.org',
      token: 'npm_test-token',
    })
    await adduser.exec([])
    t.same(npm.config.get('email'), 'test-email-old@npmjs.org')
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(rc(), {
      '//registry.npmjs.org/:_authToken': 'npm_test-token',
      email: 'test-email-old@npmjs.org',
    }, 'should only have token and un-nerfed old email')
  })

  t.test('scoped adduser', async t => {
    const { npm, rc, registry, adduser } = await mockAddUser(t, {
      stdin: ['test-user', 'test-password', 'test-email@npmjs.org'],
      config: {
        'auth-type': 'legacy',
        scope: '@myscope',
      },
    })
    registry.couchadduser({
      username: 'test-user',
      password: 'test-password',
      email: 'test-email@npmjs.org',
      token: 'npm_test-token',
    })
    await adduser.exec([])
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(npm.config.get('@myscope:registry'), 'https://registry.npmjs.org/')
    t.same(rc(), {
      '//registry.npmjs.org/:_authToken': 'npm_test-token',
      '@myscope:registry': 'https://registry.npmjs.org/',
    }, 'should only have token and scope:registry')
  })

  t.test('scoped adduser with valid scoped registry config', async t => {
    const { npm, rc, registry, adduser } = await mockAddUser(t, {
      stdin: ['test-user', 'test-password', 'test-email@npmjs.org'],
      registry: 'https://diff-registry.npmjs.org',
      homeDir: {
        '.npmrc': '@myscope:registry=https://diff-registry.npmjs.org',
      },
      config: {
        'auth-type': 'legacy',
        scope: '@myscope',
      },
    })
    registry.couchadduser({
      username: 'test-user',
      password: 'test-password',
      email: 'test-email@npmjs.org',
      token: 'npm_test-token',
    })
    await adduser.exec([])
    t.same(npm.config.get('//diff-registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(npm.config.get('@myscope:registry'), 'https://diff-registry.npmjs.org')
    t.same(rc(), {
      '@myscope:registry': 'https://diff-registry.npmjs.org',
      '//diff-registry.npmjs.org/:_authToken': 'npm_test-token',
    }, 'should only have token and scope:registry')
  })

  t.test('save config failure', async t => {
    const { registry, adduser } = await mockAddUser(t, {
      stdin: ['test-user', 'test-password', 'test-email@npmjs.org'],
      config: { 'auth-type': 'legacy' },
      homeDir: {
        '.npmrc': {},
      },
    })
    registry.couchadduser({
      username: 'test-user',
      password: 'test-password',
      email: 'test-email@npmjs.org',
      token: 'npm_test-token',
    })
    await t.rejects(adduser.exec([]))
  })
  t.end()
})

t.test('web', t => {
  t.test('basic adduser', async t => {
    const { npm, rc, registry, adduser } = await mockAddUser(t, {
      config: { 'auth-type': 'web' },
    })
    registry.webadduser({ token: 'npm_test-token' })
    await adduser.exec([])
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
    t.same(rc(), {
      '//registry.npmjs.org/:_authToken': 'npm_test-token',
    })
  })

  t.test('server error', async t => {
    const { adduser, registry } = await mockAddUser(t, {
      config: { 'auth-type': 'web' },
    })
    registry.nock.post(registry.fullPath('/-/v1/login'))
      .reply(503, {})
    await t.rejects(
      adduser.exec([]),
      { message: /503/ }
    )
  })

  t.test('fallback', async t => {
    const { npm, registry, adduser } = await mockAddUser(t, {
      stdin: ['test-user', 'test-password', 'test-email@npmjs.org'],
      config: { 'auth-type': 'web' },
    })
    registry.nock.post(registry.fullPath('/-/v1/login'))
      .reply(404, {})
    registry.couchadduser({
      username: 'test-user',
      password: 'test-password',
      email: 'test-email@npmjs.org',
      token: 'npm_test-token',
    })
    await adduser.exec([])
    t.same(npm.config.get('//registry.npmjs.org/:_authToken'), 'npm_test-token')
  })
  t.end()
})
