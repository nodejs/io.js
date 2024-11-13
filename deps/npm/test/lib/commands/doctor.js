const t = require('tap')
const fs = require('node:fs/promises')
const path = require('node:path')

const { load: loadMockNpm } = require('../../fixtures/mock-npm')
const tnock = require('../../fixtures/tnock.js')
const mockGlobals = require('@npmcli/mock-globals')
const { cleanCwd, cleanDate } = require('../../fixtures/clean-snapshot.js')

const cleanCacheSha = (str) =>
  str.replace(/content-v2\/sha512\/[^"]+/g, 'content-v2/sha512/{sha}')

t.cleanSnapshot = p => cleanCacheSha(cleanDate(cleanCwd(p)))
  .replace(/^\s+(doctor\s+at\s).*$\n/gm, '')

const npmManifest = (version) => {
  return {
    name: 'npm',
    versions: {
      [version]: {
        name: 'npm',
        version: version,
      },
    },
    time: {
      [version]: new Date(),
    },
    'dist-tags': { latest: version },
  }
}

const nodeVersions = [
  { version: 'v2.0.1', lts: false },
  { version: 'v2.0.0', lts: false },
  { version: 'v1.0.0', lts: 'NpmTestium' },
]

const dirs = {
  prefixDir: {
    node_modules: {
      testLink: t.fixture('symlink', './testDir'),
      testDir: {
        testFile: 'test contents',
      },
      '.bin': {},
    },
  },
  globalPrefixDir: {
    bin: {},
    node_modules: {},
  },
}

const globals = ({ globalPrefix }) => {
  return {
    process: {
      'env.PATH': `${globalPrefix}:${path.join(globalPrefix, 'bin')}`,
      version: 'v1.0.0',
    },
  }
}

mockGlobals(t, {
  process: {
    // set platform to not-windows before any tests because mockNpm
    // sets the platform specific location of node_modules based on it
    platform: 'test-not-windows',
    // getuid and getgid do not exist in windows, so we shim them
    // to return 0, as that is the value that lstat will assign the
    // gid and uid properties for fs.Stats objects
    ...(process.platform === 'win32' ? { getuid: () => 0, getgid: () => 0 } : {}),
  },
})

const mocks = {
  '{ROOT}/package.json': { version: '1.0.0' },
  which: async () => '/path/to/git',
  cacache: {
    verify: () => {
      return { badContentCount: 0, reclaimedCount: 0, missingContent: 0, verifiedContent: 0 }
    },
  },
}

t.test('all clear', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'output')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('all clear in color', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    ...dirs,
    config: {
      color: 'always',
    },
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'everything is ok in color')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('silent success', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    config: {
      loglevel: 'silent',
    },
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'output')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('silent errors', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    config: {
      loglevel: 'silent',
    },
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(404, '{}')
  await t.rejects(npm.exec('doctor', ['ping']), {
    message: /Check logs/,
  })
  t.matchSnapshot(joinedOutput(), 'output')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('ping 404', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(404, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []), {
    message: /See above/,
  })
  t.matchSnapshot(joinedOutput(), 'ping 404')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('ping 404 in color', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    ...dirs,
    config: {
      color: 'always',
    },
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(404, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'ping 404 in color')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('ping exception with code', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').replyWithError({ message: 'Test Error', code: 'TEST' })
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'ping failure')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('ping exception without code', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').replyWithError({ message: 'Test Error', code: false })
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'ping failure')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('npm out of date', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest('2.0.0'))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'npm is out of date')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('node out of date - lts', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals: (context) => {
      const g = globals(context)
      return {
        ...g,
        process: {
          ...g.process,
          version: 'v0.0.1',
        },
      }
    },
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'node is out of date')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('node out of date - current', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals: (context) => {
      const g = globals(context)
      return {
        ...g,
        process: {
          ...g.process,
          version: 'v2.0.0',
        },
      }
    },
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'node is out of date')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('non-default registry', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    config: { registry: 'http://some-other-url.npmjs.org' },
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'non default registry')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('missing git', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks: {
      ...mocks,
      which: async () => {
        throw new Error('test error')
      },
    },
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'missing git')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('windows skips permissions checks', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals: (context) => {
      const g = globals(context)
      return {
        ...g,
        process: {
          ...g.process,
          platform: 'win32',
        },
      }
    },
    prefixDir: {},
    globalPrefixDir: {},
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'no permissions checks')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('missing global directories', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    prefixDir: dirs.prefixDir,
    globalPrefixDir: {},
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'missing global directories')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('missing local node_modules', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    globalPrefixDir: dirs.globalPrefixDir,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'missing local node_modules')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('incorrect owner', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks: {
      ...mocks,
      'node:fs/promises': {
        ...fs,
        lstat: async (p) => {
          const stat = await fs.lstat(p)
          if (p.endsWith('_cacache')) {
            stat.uid += 1
            stat.gid += 1
          }
          return stat
        },
      },
    },
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'incorrect owner')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('incorrect permissions', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks: {
      ...mocks,
      'node:fs/promises': {
        ...fs,
        access: async () => {
          throw new Error('Test Error')
        },
      },
    },
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'incorrect owner')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('error reading directory', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks: {
      ...mocks,
      'node:fs/promises': {
        ...fs,
        readdir: async (s, ...args) => {
          if (s.endsWith('_logs')) {
            return fs.readdir(s, ...args)
          }
          // if (s.endsWith)
          throw new Error('Test Error')
        },
      },
    },
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'readdir error')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('cacache badContent', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks: {
      ...mocks,
      cacache: {
        verify: async () => {
          return { badContentCount: 1, reclaimedCount: 0, missingContent: 0, verifiedContent: 2 }
        },
      },
    },
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'corrupted cache content')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('cacache reclaimedCount', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks: {
      ...mocks,
      cacache: {
        verify: async () => {
          return { badContentCount: 0, reclaimedCount: 1, missingContent: 0, verifiedContent: 2 }
        },
      },
    },
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'content garbage collected')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('cacache missingContent', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks: {
      ...mocks,
      cacache: {
        verify: async () => {
          return { badContentCount: 0, reclaimedCount: 0, missingContent: 1, verifiedContent: 2 }
        },
      },
    },
    globals,
    ...dirs,
  })
  tnock(t, npm.config.get('registry'))
    .get('/-/ping').reply(200, '{}')
    .get('/npm').reply(200, npmManifest(npm.version))
  tnock(t, 'https://nodejs.org')
    .get('/dist/index.json').reply(200, nodeVersions)
  await npm.exec('doctor', [])
  t.matchSnapshot(joinedOutput(), 'missing content')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('bad proxy', async t => {
  const { joinedOutput, logs, npm } = await loadMockNpm(t, {
    mocks,
    globals,
    config: {
      proxy: 'ssh://npmjs.org',
    },
    ...dirs,
  })
  await t.rejects(npm.exec('doctor', []))
  t.matchSnapshot(joinedOutput(), 'output')
  t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
})

t.test('discrete checks', t => {
  t.test('ping', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals,
      ...dirs,
    })
    tnock(t, npm.config.get('registry'))
      .get('/-/ping').reply(200, '{}')
    await npm.exec('doctor', ['ping'])
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.test('versions', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals,
      ...dirs,
    })
    tnock(t, npm.config.get('registry'))
      .get('/npm').reply(200, npmManifest(npm.version))
    tnock(t, 'https://nodejs.org')
      .get('/dist/index.json').reply(200, nodeVersions)
    await npm.exec('doctor', ['versions'])
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.test('registry', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals,
      ...dirs,
    })
    tnock(t, npm.config.get('registry'))
      .get('/-/ping').reply(200, '{}')
    await npm.exec('doctor', ['registry'])
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.test('git', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals,
      ...dirs,
    })
    await npm.exec('doctor', ['git'])
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.test('permissions - not windows', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals,
      ...dirs,
    })
    await npm.exec('doctor', ['permissions'])
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.test('cache', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals,
      ...dirs,
    })
    await npm.exec('doctor', ['cache'])
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.test('permissions - windows', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals: (context) => {
        const g = globals(context)
        return {
          ...g,
          process: {
            ...g.process,
            platform: 'win32',
          },
        }
      },
      prefixDir: {},
      globalPrefixDir: {},
    })
    await npm.exec('doctor', ['permissions'])
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.test('invalid environment', async t => {
    const { joinedOutput, logs, npm } = await loadMockNpm(t, {
      mocks,
      globals: (context) => {
        const g = globals(context)
        return {
          ...g,
          process: {
            ...g.process,
            'env.PATH': '/nope',
          },
        }
      },
      prefixDir: {},
      globalPrefixDir: {},
    })
    await t.rejects(npm.exec('doctor', ['environment']))
    t.matchSnapshot(joinedOutput(), 'output')
    t.matchSnapshot({ info: logs.info, warn: logs.warn, error: logs.error }, 'logs')
  })

  t.end()
})
