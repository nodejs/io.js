const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const MockRegistry = require('@npmcli/mock-registry')
const cacache = require('cacache')
const path = require('node:path')

t.test('no details', async t => {
  const { npm, logs, joinedOutput } = await loadMockNpm(t)
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
  })
  registry.ping()
  await npm.exec('ping', [])
  t.match(logs.notice, [
    'PING https://registry.npmjs.org/',
    /PONG [0-9]+ms/,
  ])
  t.equal(joinedOutput(), '')
})

t.test('with details', async t => {
  const { npm, logs, joinedOutput } = await loadMockNpm(t)
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
  })
  registry.ping({ body: { test: true, test2: true } })
  await npm.exec('ping', [])
  t.match(logs.notice, [
    `PING https://registry.npmjs.org/`,
    /PONG [0-9]+ms/,
    `PONG {\nPONG   "test": true,\nPONG   "test2": true\nPONG }`,
  ])
  t.match(joinedOutput(), '')
})

t.test('valid json', async t => {
  const { npm, logs, joinedOutput } = await loadMockNpm(t, {
    config: { json: true },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
  })
  registry.ping()
  await npm.exec('ping', [])
  t.match(logs.notice, [
    'PING https://registry.npmjs.org/',
    /PONG [0-9]+ms/,
  ])
  t.match(JSON.parse(joinedOutput()), {
    registry: npm.config.get('registry'),
    time: /[0-9]+/,
    details: {},
  })
})

t.test('invalid json', async t => {
  const { npm, logs, joinedOutput } = await loadMockNpm(t, {
    config: { json: true },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
  })
  registry.ping({ body: '{not: real"json]' })
  await npm.exec('ping', [])
  t.match(logs.notice, [
    'PING https://registry.npmjs.org/',
    /PONG [0-9]+ms/,
  ])
  t.match(JSON.parse(joinedOutput()), {
    registry: npm.config.get('registry'),
    time: /[0-9]+/,
    details: {},
  })
})
t.test('fail when registry is unreachable even if request is cached', async t => {
  const { npm } = await loadMockNpm(t, {
    config: { registry: 'https://ur.npmlocal.npmtest/' },
    cacheDir: { _cacache: {} },
  })
  const url = `${npm.config.get('registry')}-/ping`
  const cache = path.join(npm.cache, '_cacache')
  await cacache.put(cache,
    `make-fetch-happen:request-cache:${url}`,
    '{}', { metadata: { url } }
  )
  t.rejects(npm.exec('ping', []), {
    code: 'ENOTFOUND',
  },
  'throws ENOTFOUND error')
})
