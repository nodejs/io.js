const t = require('tap')
const { basename } = require('node:path')
const tmock = require('../../fixtures/tmock')
const mockNpm = require('../../fixtures/mock-npm')

const CURRENT_VERSION = '123.420.69'
const CURRENT_MAJOR = '122.420.69'
const CURRENT_MINOR = '123.419.69'
const CURRENT_PATCH = '123.420.68'
const NEXT_VERSION = '123.421.70'
const NEXT_MINOR = '123.420.70'
const NEXT_PATCH = '123.421.69'
const CURRENT_BETA = '124.0.0-beta.99999'
const HAVE_BETA = '124.0.0-beta.0'

const runUpdateNotifier = async (t, {
  STAT_ERROR,
  WRITE_ERROR,
  PACOTE_ERROR,
  STAT_MTIME = 0,
  mocks: _mocks = {},
  command = 'help',
  prefixDir,
  version = CURRENT_VERSION,
  argv = [],
  wroteFile = false,
  ...config
} = {}) => {
  const mockFs = {
    ...require('node:fs/promises'),
    stat: async (path) => {
      if (basename(path) !== '_update-notifier-last-checked') {
        t.fail('no stat allowed for non upate notifier files')
      }
      if (STAT_ERROR) {
        throw STAT_ERROR
      }
      return { mtime: new Date(STAT_MTIME) }
    },
    writeFile: async (path, content) => {
      wroteFile = true
      if (content !== '') {
        t.fail('no write file content allowed')
      }
      if (basename(path) !== '_update-notifier-last-checked') {
        t.fail('no writefile allowed for non upate notifier files')
      }
      if (WRITE_ERROR) {
        throw WRITE_ERROR
      }
    },
  }

  const MANIFEST_REQUEST = []
  const mockPacote = {
    manifest: async (spec) => {
      if (!spec.match(/^npm@/)) {
        t.fail('no pacote manifest allowed for non npm packages')
      }
      MANIFEST_REQUEST.push(spec)
      if (PACOTE_ERROR) {
        throw PACOTE_ERROR
      }
      const manifestV = spec === 'npm@latest' ? CURRENT_VERSION
        : /-/.test(spec) ? CURRENT_BETA : NEXT_VERSION
      return { version: manifestV }
    },
  }

  const mocks = {
    pacote: mockPacote,
    'node:fs/promises': mockFs,
    '{ROOT}/package.json': { version },
    'ci-info': { isCI: false, name: null },
    ..._mocks,
  }

  const mock = await mockNpm(t, {
    command,
    mocks,
    config,
    exec: true,
    prefixDir,
    argv,
  })
  const updateNotifier = tmock(t, '{LIB}/cli/update-notifier.js', mocks)

  const result = await updateNotifier(mock.npm)

  return {
    wroteFile,
    result,
    MANIFEST_REQUEST,
  }
}

t.test('duration has elapsed, no updates', async t => {
  const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t)
  t.equal(wroteFile, true)
  t.not(result)
  t.equal(MANIFEST_REQUEST.length, 1)
})

t.test('situations in which we do not notify', t => {
  t.test('nothing to do if notifier disabled', async t => {
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, {
      'update-notifier': false,
    })
    t.equal(wroteFile, false)
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, [], 'no requests for manifests')
  })

  t.test('do not suggest update if already updating', async t => {
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, {
      command: 'install',
      prefixDir: { 'package.json': `{"name":"${t.testName}"}` },
      argv: ['npm'],
      global: true,
    })
    t.equal(wroteFile, false)
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, [], 'no requests for manifests')
  })

  t.test('do not suggest update if already updating with spec', async t => {
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, {
      command: 'install',
      prefixDir: { 'package.json': `{"name":"${t.testName}"}` },
      argv: ['npm@latest'],
      global: true,
    })
    t.equal(wroteFile, false)
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, [], 'no requests for manifests')
  })

  t.test('do not update if same as latest', async t => {
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t)
    t.equal(wroteFile, true)
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, ['npm@latest'], 'requested latest version')
  })
  t.test('check if stat errors (here for coverage)', async t => {
    const STAT_ERROR = new Error('blorg')
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, { STAT_ERROR })
    t.equal(wroteFile, true)
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, ['npm@latest'], 'requested latest version')
  })
  t.test('ok if write errors (here for coverage)', async t => {
    const WRITE_ERROR = new Error('grolb')
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, { WRITE_ERROR })
    t.equal(wroteFile, true)
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, ['npm@latest'], 'requested latest version')
  })
  t.test('ignore pacote failures (here for coverage)', async t => {
    const PACOTE_ERROR = new Error('pah-KO-tchay')
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, { PACOTE_ERROR })
    t.equal(result, null)
    t.equal(wroteFile, true)
    t.strictSame(MANIFEST_REQUEST, ['npm@latest'], 'requested latest version')
  })
  t.test('do not update if newer than latest, but same as next', async t => {
    const {
      wroteFile,
      result,
      MANIFEST_REQUEST,
    } = await runUpdateNotifier(t, { version: NEXT_VERSION })
    t.equal(result, null)
    t.equal(wroteFile, true)
    const reqs = ['npm@latest', `npm@^${NEXT_VERSION}`]
    t.strictSame(MANIFEST_REQUEST, reqs, 'requested latest and next versions')
  })
  t.test('do not update if on the latest beta', async t => {
    const {
      wroteFile,
      result,
      MANIFEST_REQUEST,
    } = await runUpdateNotifier(t, { version: CURRENT_BETA })
    t.equal(result, null)
    t.equal(wroteFile, true)
    const reqs = [`npm@^${CURRENT_BETA}`]
    t.strictSame(MANIFEST_REQUEST, reqs, 'requested latest and next versions')
  })

  t.test('do not update in CI', async t => {
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, { mocks: {
      'ci-info': { isCI: true, name: 'something' },
    } })
    t.equal(wroteFile, false)
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, [], 'no requests for manifests')
  })

  t.test('only check weekly for GA releases', async t => {
    // One week (plus five minutes to account for test environment fuzziness)
    const STAT_MTIME = Date.now() - 1000 * 60 * 60 * 24 * 7 + 1000 * 60 * 5
    const { wroteFile, result, MANIFEST_REQUEST } = await runUpdateNotifier(t, { STAT_MTIME })
    t.equal(wroteFile, false, 'duration was not reset')
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, [], 'no requests for manifests')
  })

  t.test('only check daily for betas', async t => {
    // One day (plus five minutes to account for test environment fuzziness)
    const STAT_MTIME = Date.now() - 1000 * 60 * 60 * 24 + 1000 * 60 * 5
    const {
      wroteFile,
      result,
      MANIFEST_REQUEST,
    } = await runUpdateNotifier(t, { STAT_MTIME, version: HAVE_BETA })
    t.equal(wroteFile, false, 'duration was not reset')
    t.equal(result, null)
    t.strictSame(MANIFEST_REQUEST, [], 'no requests for manifests')
  })

  t.end()
})

t.test('notification situations', async t => {
  const cases = {
    [HAVE_BETA]: [`^{V}`],
    [NEXT_PATCH]: [`latest`, `^{V}`],
    [NEXT_MINOR]: [`latest`, `^{V}`],
    [CURRENT_PATCH]: ['latest'],
    [CURRENT_MINOR]: ['latest'],
    [CURRENT_MAJOR]: ['latest'],
  }

  for (const [version, reqs] of Object.entries(cases)) {
    for (const color of [false, 'always']) {
      await t.test(`${version} - color=${color}`, async t => {
        const {
          wroteFile,
          result,
          MANIFEST_REQUEST,
        } = await runUpdateNotifier(t, { version, color })
        t.matchSnapshot(result)
        t.equal(wroteFile, true)
        t.strictSame(MANIFEST_REQUEST, reqs.map(r => `npm@${r.replace('{V}', version)}`))
      })
    }
  }
})
