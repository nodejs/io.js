const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')

const MockRegistry = require('@npmcli/mock-registry')

const token = 'test-auth-token'
const auth = { '//registry.npmjs.org/:_authToken': token }
const versions = ['1.0.0', '1.0.1', '1.0.1-pre']

t.test('no args', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('undeprecate', []),
    { code: 'EUSAGE' },
    'logs usage'
  )
})

t.test('undeprecate', async t => {
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
  registry.nock.put('/foo', body => {
    for (const version of versions) {
      if (body.versions[version].deprecated !== '') {
        return false
      }
    }
    return true
  }).reply(200, {})

  await npm.exec('undeprecate', ['foo'])
  t.match(logs.notice, [
    'undeprecating foo@1.0.0',
    'undeprecating foo@1.0.1',
    'undeprecating foo@1.0.1-pre',
  ])
  t.match(joinedOutput(), '')
})

t.test('dry-run', async t => {
  const { npm, logs, joinedOutput } = await loadMockNpm(t, { config: {
    'dry-run': true,
    ...auth,
  } })
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

  await npm.exec('undeprecate', ['foo'])
  t.match(logs.notice, [
    'undeprecating foo@1.0.0',
    'undeprecating foo@1.0.1',
    'undeprecating foo@1.0.1-pre',
  ])
  t.match(joinedOutput(), '')
})
