const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const MockRegistry = require('@npmcli/mock-registry')
const mockGlobals = require('@npmcli/mock-globals')
const stream = require('node:stream')

const authToken = 'abcd1234'
const password = 'this is not really a password'

const auth = {
  '//registry.npmjs.org/:_authToken': authToken,
}

const now = new Date().toISOString()
const tokens = [
  {
    key: 'abcd1234abcd1234',
    token: 'efgh5678efgh5678',
    cidr_whitelist: null,
    readonly: false,
    created: now,
    updated: now,
  },
  {
    key: 'abcd1256',
    token: 'hgfe8765',
    cidr_whitelist: ['192.168.1.1/32'],
    readonly: true,
    created: now,
    updated: now,
  },
]

t.test('completion', async t => {
  const { token } = await loadMockNpm(t, { command: 'token' })

  const testComp = (argv, expect) => {
    t.resolveMatch(token.completion({ conf: { argv: { remain: argv } } }), expect, argv.join(' '))
  }

  testComp(['npm', 'token'], ['list', 'revoke', 'create'])
  testComp(['npm', 'token', 'list'], [])
  testComp(['npm', 'token', 'revoke'], [])
  testComp(['npm', 'token', 'create'], [])

  t.rejects(token.completion({ conf: { argv: { remain: ['npm', 'token', 'foobar'] } } }), {
    message: 'foobar not recognize',
  })
})

t.test('token foobar', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(npm.exec('token', ['foobar']), /foobar is not a recognized subcommand/)
})

t.test('token list', async t => {
  const { npm, outputs } = await loadMockNpm(t, {
    config: { ...auth },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  registry.getTokens(tokens)
  await npm.exec('token', [])
  t.strictSame(outputs, [
    `Publish token efgh5678efgh5678… with id abcd123 created ${now.slice(0, 10)}`,
    '',
    `Read only token hgfe8765… with id abcd125 created ${now.slice(0, 10)}`,
    'with IP whitelist: 192.168.1.1/32',
    '',
  ])
})

t.test('token list json output', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      ...auth,
      json: true,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  registry.getTokens(tokens)
  await npm.exec('token', ['list'])
  const parsed = JSON.parse(joinedOutput())
  t.match(parsed, tokens, 'prints the json parsed tokens')
})

t.test('token list parseable output', async t => {
  const { npm, outputs } = await loadMockNpm(t, {
    config: {
      ...auth,
      parseable: true,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  registry.getTokens(tokens)
  await npm.exec('token', [])
  t.strictSame(outputs, [
    'key\ttoken\tcreated\treadonly\tCIDR whitelist',
    `abcd1234abcd1234\tefgh5678efgh5678\t${now}\tfalse\t`,
    `abcd1256\thgfe8765\t${now}\ttrue\t192.168.1.1/32`,
  ])
})

t.test('token revoke', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: { ...auth },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })

  registry.getTokens(tokens)
  registry.nock.delete(`/-/npm/v1/tokens/token/${tokens[0].key}`).reply(200)
  await npm.exec('token', ['rm', tokens[0].key.slice(0, 8)])

  t.equal(joinedOutput(), 'Removed 1 token')
})

t.test('token revoke multiple tokens', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: { ...auth },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })

  registry.getTokens(tokens)
  registry.nock.delete(`/-/npm/v1/tokens/token/${tokens[0].key}`).reply(200)
  registry.nock.delete(`/-/npm/v1/tokens/token/${tokens[1].key}`).reply(200)
  await npm.exec('token', ['rm', tokens[0].key.slice(0, 8), tokens[1].key.slice(0, 8)])

  t.equal(joinedOutput(), 'Removed 2 tokens')
})

t.test('token revoke json output', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      ...auth,
      json: true,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })

  registry.getTokens(tokens)
  registry.nock.delete(`/-/npm/v1/tokens/token/${tokens[0].key}`).reply(200)
  await npm.exec('token', ['rm', tokens[0].key.slice(0, 8)])

  const parsed = JSON.parse(joinedOutput())
  t.same(parsed, [tokens[0].key], 'logs the token as json')
})

t.test('token revoke parseable output', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      ...auth,
      parseable: true,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })

  registry.getTokens(tokens)
  registry.nock.delete(`/-/npm/v1/tokens/token/${tokens[0].key}`).reply(200)
  await npm.exec('token', ['rm', tokens[0].key.slice(0, 8)])
  t.equal(joinedOutput(), tokens[0].key, 'logs the token as a string')
})

t.test('token revoke by token', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: { ...auth },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  registry.getTokens(tokens)
  registry.nock.delete(`/-/npm/v1/tokens/token/${tokens[0].token}`).reply(200)
  await npm.exec('token', ['rm', tokens[0].token])
  t.equal(joinedOutput(), 'Removed 1 token')
})

t.test('token revoke requires an id', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(npm.exec('token', ['rm']), {
    code: 'EUSAGE',
    message: '`<tokenKey>` argument is required',
  })
})

t.test('token revoke ambiguous id errors', async t => {
  const { npm } = await loadMockNpm(t, {
    config: { ...auth },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  registry.getTokens(tokens)
  await t.rejects(npm.exec('token', ['rm', 'abcd']), {
    message: /Token ID "abcd" was ambiguous/,
  })
})

t.test('token revoke unknown token', async t => {
  const { npm } = await loadMockNpm(t, {
    config: { ...auth },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })

  registry.getTokens(tokens)
  await t.rejects(npm.exec('token', ['rm', '0xnotreal']),
    'Unknown token id or value 0xnotreal'
  )
})

t.test('token create', async t => {
  const cidr = ['10.0.0.0/8', '192.168.1.0/24']
  const { npm, outputs } = await loadMockNpm(t, {
    config: {
      ...auth,
      cidr,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  const stdin = new stream.PassThrough()
  stdin.write(`${password}\n`)
  mockGlobals(t, {
    'process.stdin': stdin,
    'process.stdout': new stream.PassThrough(), // to quiet readline
  }, { replace: true })
  registry.createToken({ password, cidr })
  await npm.exec('token', ['create'])
  t.strictSame(outputs, [
    '',
    'Created publish token n3wt0k3n',
    'with IP whitelist: 10.0.0.0/8,192.168.1.0/24',
  ])
})

t.test('token create read only', async t => {
  const { npm, outputs } = await loadMockNpm(t, {
    config: {
      ...auth,
      'read-only': true,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  const stdin = new stream.PassThrough()
  stdin.write(`${password}\n`)
  mockGlobals(t, {
    'process.stdin': stdin,
    'process.stdout': new stream.PassThrough(), // to quiet readline
  }, { replace: true })
  registry.createToken({ readonly: true, password })
  await npm.exec('token', ['create'])
  t.strictSame(outputs, [
    '',
    'Created read only token n3wt0k3n',
  ])
})

t.test('token create json output', async t => {
  const cidr = ['10.0.0.0/8', '192.168.1.0/24']
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      ...auth,
      json: true,
      cidr,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  const stdin = new stream.PassThrough()
  stdin.write(`${password}\n`)
  mockGlobals(t, {
    'process.stdin': stdin,
    'process.stdout': new stream.PassThrough(), // to quiet readline
  }, { replace: true })
  registry.createToken({ password, cidr })
  await npm.exec('token', ['create'])
  const parsed = JSON.parse(joinedOutput())
  t.match(
    parsed,
    { token: 'n3wt0k3n', readonly: false, cidr_whitelist: cidr }
  )
  t.ok(parsed.created, 'also returns created')
})

t.test('token create parseable output', async t => {
  const cidr = ['10.0.0.0/8', '192.168.1.0/24']
  const { npm, outputs } = await loadMockNpm(t, {
    config: {
      ...auth,
      parseable: true,
      cidr,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: authToken,
  })
  const stdin = new stream.PassThrough()
  stdin.write(`${password}\n`)
  mockGlobals(t, {
    'process.stdin': stdin,
    'process.stdout': new stream.PassThrough(), // to quiet readline
  }, { replace: true })
  registry.createToken({ password, cidr })
  await npm.exec('token', ['create'])
  t.equal(outputs[1], 'token\tn3wt0k3n')
  t.ok(outputs[2].startsWith('created\t'))
  t.equal(outputs[3], 'readonly\tfalse')
  t.equal(outputs[4], 'cidr_whitelist\t10.0.0.0/8,192.168.1.0/24')
})

t.test('token create ipv6 cidr', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      ...auth,
      cidr: '::1/128',
    },
  })
  await t.rejects(npm.exec('token', ['create'], {
    code: 'EINVALIDCIDR',
    message: /CIDR whitelist can only contain IPv4 addresses, ::1\/128 is IPv6/,
  }))
})

t.test('token create invalid cidr', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      ...auth,
      cidr: 'apple/cider',
    },
  })
  await t.rejects(npm.exec('token', ['create'], {
    code: 'EINVALIDCIDR',
    message: 'CIDR whitelist contains invalid CIDR entry: apple/cider',
  }))
})
