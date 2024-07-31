const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')

const MockRegistry = require('@npmcli/mock-registry')

const user = 'test-user'
const token = 'test-auth-token'
const auth = { '//registry.npmjs.org/:_authToken': token }
const versions = ['1.0.0', '1.0.1', '1.0.1-pre']

// libnpmaccess maps these to read-write and read-only
const packages = { foo: 'write', bar: 'write', baz: 'write', buzz: 'read' }

t.test('completion', async t => {
  const { npm, deprecate } = await loadMockNpm(t, {
    command: 'deprecate',
    config: {
      ...auth,
    },
  })

  const testComp = async (argv, expect) => {
    const res =
      await deprecate.completion({ conf: { argv: { remain: argv } } })
    t.strictSame(res, expect, `completion: ${argv}`)
  }

  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })

  registry.whoami({ username: user, times: 4 })
  registry.getPackages({ team: user, packages, times: 4 })
  await Promise.all([
    testComp([], ['foo', 'bar', 'baz']),
    testComp(['b'], ['bar', 'baz']),
    testComp(['fo'], ['foo']),
    testComp(['g'], []),
  ])

  await testComp(['foo', 'something'], [])

  registry.whoami({ responseCode: 401, body: {} })

  await t.rejects(
    testComp([], []),
    { code: 'E401' }
  )
})

t.test('no args', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('deprecate', []),
    { code: 'EUSAGE' },
    'logs usage'
  )
})

t.test('only one arg', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('deprecate', ['foo']),
    { code: 'EUSAGE' },
    'logs usage'
  )
})

t.test('invalid semver range', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('deprecate', ['foo@notaversion', 'this will fail']),
    /invalid version range/,
    'logs semver error'
  )
})

t.test('undeprecate', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, { config: { ...auth } })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  const manifest = registry.manifest({
    name: 'foo',
    versions,
  })
  await registry.package({ manifest, query: { write: true } })
  registry.nock.put('/foo', body => {
    for (const version of versions) {
      if (body.versions[version].deprecated !== '') {
        return false
      }
    }
    return true
  }).reply(200, {})

  await npm.exec('deprecate', ['foo', ''])
  t.match(joinedOutput(), '')
})

t.test('deprecates given range', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, { config: { ...auth } })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  const manifest = registry.manifest({
    name: 'foo',
    versions,
  })
  await registry.package({ manifest, query: { write: true } })
  const message = 'test deprecation message'
  registry.nock.put('/foo', body => {
    if (body.versions['1.0.1'].deprecated) {
      return false
    }
    if (body.versions['1.0.1-pre'].deprecated) {
      return false
    }
    return body.versions['1.0.0'].deprecated === message
  }).reply(200, {})
  await npm.exec('deprecate', ['foo@1.0.0', message])
  t.match(joinedOutput(), '')
})

t.test('deprecates all versions when no range is specified', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, { config: { ...auth } })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  const manifest = registry.manifest({
    name: 'foo',
    versions,
  })
  await registry.package({ manifest, query: { write: true } })
  const message = 'test deprecation message'
  registry.nock.put('/foo', body => {
    for (const version of versions) {
      if (body.versions[version].deprecated !== message) {
        return false
      }
    }
    return true
  }).reply(200, {})

  await npm.exec('deprecate', ['foo', message])
  t.match(joinedOutput(), '')
})

t.test('does nothing if version does not actually exist', async t => {
  const { npm, logs, joinedOutput } = await loadMockNpm(t, { config: { ...auth } })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  const manifest = registry.manifest({
    name: 'foo',
    versions,
  })
  await registry.package({ manifest, query: { write: true } })
  await npm.exec('deprecate', ['foo@1.0.99', 'this should be ignored'])
  t.match(joinedOutput(), '')
  t.equal(logs.warn[0], 'deprecate No version found for 1.0.99')
})
