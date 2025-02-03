const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm.js')
const MockRegistry = require('@npmcli/mock-registry')

const cacache = require('cacache')
const fs = require('node:fs')
const path = require('node:path')

const pkg = 'test-package'

t.cleanSnapshot = str => {
  return str
    .replace(/Finished in [0-9.s]+/g, 'Finished in xxxs')
    .replace(/Cache verified and compressed (.*)/, 'Cache verified and compressed ({PATH})')
}

t.test('cache no args', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('cache', []),
    { code: 'EUSAGE' },
    'should throw usage instructions'
  )
})

t.test('cache clean', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('cache', ['clean']),
    /the npm cache self-heals/,
    'should throw warning'
  )
})

t.test('cache clean (force)', async t => {
  const { npm } = await loadMockNpm(t, {
    cacheDir: { _cacache: {} },
    config: { force: true },
  })
  const cache = path.join(npm.cache, '_cacache')
  await npm.exec('cache', ['clean'])
  t.notOk(fs.existsSync(cache), 'cache dir was removed')
})

t.test('cache add no arg', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('cache', ['add']),
    {
      code: 'EUSAGE',
      message: 'First argument to `add` is required',
    },
    'throws usage error'
  )
})

t.test('cache add single pkg', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      package: {
        'package.json': JSON.stringify({
          name: pkg,
          version: '1.0.0',
        }),
      },
    },
  })
  const cache = path.join(npm.cache, '_cacache')
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({
    manifest,
    times: 2,
    tarballs: { '1.0.0': path.join(npm.prefix, 'package') },
  })
  await npm.exec('cache', ['add', pkg])
  t.equal(joinedOutput(), '')
  t.resolves(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package/-/test-package-1.0.0.tgz'))
  t.resolves(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package'))
})

t.test('cache add multiple pkgs', async t => {
  const pkg2 = 'test-package-two'
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      package: {
        'package.json': JSON.stringify({
          name: pkg,
          version: '1.0.0',
        }),
      },
    },
  })
  const cache = path.join(npm.cache, '_cacache')
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
  })
  const manifest = registry.manifest({ name: pkg })
  const manifest2 = registry.manifest({ name: pkg2 })
  await registry.package({
    manifest,
    times: 2,
    tarballs: { '1.0.0': path.join(npm.prefix, 'package') },
  })
  await registry.package({
    manifest: manifest2, times: 2, tarballs: { '1.0.0': path.join(npm.prefix, 'package') },
  })
  await npm.exec('cache', ['add', pkg, pkg2])
  t.equal(joinedOutput(), '')
  t.resolves(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package/-/test-package-1.0.0.tgz'))
  t.resolves(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package'))
  t.resolves(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package-two/-/test-package-two-1.0.0.tgz'))
  t.resolves(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package-two'))
})

t.test('cache ls', async t => {
  const keys = [
    'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package/-/test-package-1.0.0.tgz',
  ]
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  for (const key of keys) {
    await cacache.put(cache, key, 'test data')
  }
  await npm.exec('cache', ['ls'])
  t.matchSnapshot(joinedOutput(), 'logs cache entries')
})

t.test('cache ls pkgs', async t => {
  const keys = [
    'make-fetch-happen:request-cache:https://registry.npmjs.org/npm',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/npm/-/npm-1.2.0.tgz',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/webpack/-/webpack-4.47.0.tgz',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/webpack/-/webpack-4.40.0.tgz',
  ]
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  for (const key of keys) {
    await cacache.put(cache, key, 'test data')
  }
  await cacache.put(cache,
    'make-fetch-happen:request-cache:https://registry.npmjs.org/webpack',
    JSON.stringify({ versions: {
      '4.40.0': { dist: { tarball: 'https://registry.npmjs.org/webpack/-/webpack-4.40.0.tgz' } },
      '4.47.0': { dist: { tarball: 'https://registry.npmjs.org/webpack/-/webpack-4.47.0.tgz' } },
    } })
  )
  await npm.exec('cache', ['ls', 'webpack@>4.44.1', 'npm'])
  t.matchSnapshot(joinedOutput(), 'logs cache entries for npm and webpack and one webpack tgz')
})

t.test('cache ls special', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  await cacache.put(cache,
    'make-fetch-happen:request-cache:https://registry.npmjs.org/foo',
    JSON.stringify({ versions: { '1.2.3-beta': {} } })
  )
  await cacache.put(cache,
    'make-fetch-happen:request-cache:https://registry.npmjs.org/foo/-/foo-1.2.3-beta.tgz',
    'test-data'
  )
  await npm.exec('cache', ['ls', 'foo@1.2.3-beta'])
  t.matchSnapshot(joinedOutput(), 'logs cache entries for foo')
})

t.test('cache ls nonpublic registry', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  await cacache.put(cache,
    'make-fetch-happen:request-cache:https://somerepo.github.org/extemporaneously',
    JSON.stringify({
      versions: { '1.0.0': { dist: { tarball: 'https://somerepo.github.org/aabbcc/' } } },
    })
  )
  await cacache.put(cache,
    'make-fetch-happen:request-cache:https://somerepo.github.org/aabbcc/',
    'test data'
  )
  await npm.exec('cache', ['ls', 'extemporaneously'])
  t.matchSnapshot(joinedOutput(), 'logs cache entry for extemporaneously and its tarball')
})

t.test('cache ls tagged', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('cache', ['ls', 'webpack@latest']),
    { code: 'EUSAGE' },
    'should throw usage error'
  )
})

t.test('cache ls scoped and scoped slash', async t => {
  const keys = [
    'make-fetch-happen:request-cache:https://registry.npmjs.org/@fritzy/staydown/-/@fritzy/staydown-3.1.1.tgz',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/@fritzy%2fstaydown',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/@gar/npm-expansion/-/@gar/npm-expansion-2.1.0.tgz',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/@gar%2fnpm-expansion',
  ]
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  for (const key of keys) {
    await cacache.put(cache, key, 'test data')
  }
  await npm.exec('cache', ['ls', '@fritzy/staydown', '@gar/npm-expansion'])
  t.matchSnapshot(joinedOutput(), 'logs cache entries for @gar and @fritzy')
})

t.test('cache ls corrupted', async t => {
  const keys = [
    'make-fetch-happen:request-cache:https://registry.npmjs.org/corrupted',
    'make-fetch-happen:request-cache:https://registry.npmjs.org/corrupted/-/corrupted-3.1.0.tgz',
  ]
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  for (const key of keys) {
    await cacache.put(cache, key, Buffer.from('<>>>}"'))
  }
  await npm.exec('cache', ['ls', 'corrupted'])
  t.matchSnapshot(joinedOutput(), 'logs cache entries with bad data')
})

t.test('cache ls missing packument version not an object', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  await cacache.put(cache,
    'make-fetch-happen:request-cache:https://registry.npmjs.org/missing-version',
    JSON.stringify({ versions: 'not an object' })
  )
  await npm.exec('cache', ['ls', 'missing-version'])
  t.matchSnapshot(joinedOutput(), 'logs cache entry for packument')
})

t.test('cache rm', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)
  const cache = path.join(npm.cache, '_cacache')
  await cacache.put(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package', '{}')
  await cacache.put(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package/-/test-package-1.0.0.tgz', 'test data')
  await npm.exec('cache', ['rm', 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package/-/test-package-1.0.0.tgz'])
  t.matchSnapshot(joinedOutput(), 'logs deleting single entry')
  t.resolves(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package'))
  t.rejects(cacache.get(cache, 'make-fetch-happen:request-cache:https://registry.npmjs.org/test-package/-/test-package-1.0.0.tgz'))
})

t.test('cache rm unfound', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)
  await npm.exec('cache', ['rm', 'made-up-key'])
  t.same(joinedOutput(), '', 'no errors, no output')
})

t.test('cache verify', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)
  await npm.exec('cache', ['verify'])
  t.matchSnapshot(joinedOutput(), 'shows verified cache output')
})

t.test('cache verify as part of home', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    globals: ({ prefix }) => ({ 'process.env.HOME': path.dirname(prefix) }),
  })
  await npm.exec('cache', ['verify'])
  t.match(joinedOutput(), 'Cache verified and compressed (~', 'contains ~ shorthand')
})

t.test('cache verify w/ extra output', async t => {
  const verify = {
    runTime: {
      markStartTime: 0,
      fixPerms: 3,
      garbageCollect: 54982,
      rebuildIndex: 62779,
      cleanTmp: 62781,
      writeVerifile: 62783,
      markEndTime: 62783,
      total: 62783,
    },
    verifiedContent: 17057,
    reclaimedCount: 1144,
    reclaimedSize: 248164665,
    badContentCount: 12345,
    keptSize: 1644485260,
    missingContent: 92,
    rejectedEntries: 92,
    totalEntries: 20175,
  }
  const { npm, joinedOutput } = await loadMockNpm(t, {
    mocks: { cacache: { verify: () => verify } },
  })
  await npm.exec('cache', ['verify'])
  t.matchSnapshot(joinedOutput(), 'shows extra output')
})

t.test('cache completion', async t => {
  const { cache } = await loadMockNpm(t, { command: 'cache' })
  const { completion } = cache

  const testComp = (argv, expect) => {
    return t.resolveMatch(completion({ conf: { argv: { remain: argv } } }), expect, argv.join(' '))
  }

  await Promise.all([
    testComp(['npm', 'cache'], ['add', 'clean', 'verify']),
    testComp(['npm', 'cache', 'add'], []),
    testComp(['npm', 'cache', 'clean'], []),
    testComp(['npm', 'cache', 'verify'], []),
  ])
})
