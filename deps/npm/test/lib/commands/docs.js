const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm.js')
const { sep } = require('node:path')

const fixtures = {
  pkg: {
    'package.json': JSON.stringify({
      name: 'thispkg',
      version: '1.2.3',
      homepage: 'https://example.com',
    }),
    nodocs: {
      'package.json': JSON.stringify({
        name: 'nodocs',
        version: '1.2.3',
      }),
    },
    docsurl: {
      'package.json': JSON.stringify({
        name: 'docsurl',
        version: '1.2.3',
        homepage: 'https://bugzilla.localhost/docsurl',
      }),
    },
    repourl: {
      'package.json': JSON.stringify({
        name: 'repourl',
        version: '1.2.3',
        repository: 'https://github.com/foo/repourl',
      }),
    },
    repoobj: {
      'package.json': JSON.stringify({
        name: 'repoobj',
        version: '1.2.3',
        repository: { url: 'https://github.com/foo/repoobj' },
      }),
    },
    repourlobj: {
      'package.json': JSON.stringify({
        name: 'repourlobj',
        version: '1.2.3',
        repository: { url: { works: false } },
      }),
    },
  },
  workspaces: {
    'package.json': JSON.stringify({
      name: 'workspaces-test',
      version: '1.2.3-test',
      workspaces: ['workspace-a', 'workspace-b', 'workspace-c'],
    }),
    'workspace-a': {
      'package.json': JSON.stringify({
        name: 'workspace-a',
        version: '1.2.3-a',
        homepage: 'http://docs.workspace-a/',
      }),
    },
    'workspace-b': {
      'package.json': JSON.stringify({
        name: 'workspace-b',
        version: '1.2.3-n',
        repository: 'https://github.com/npm/workspace-b',
      }),
    },
    'workspace-c': JSON.stringify({
      'package.json': {
        name: 'workspace-n',
        version: '1.2.3-n',
      },
    }),
  },
}

const setup = async (t, { prefixDir = fixtures.pkg, config } = {}) => {
  // keep a tally of which urls got opened
  const opened = {}
  const openUrl = async (_, url) => {
    opened[url] = opened[url] || 0
    opened[url]++
  }

  const res = await mockNpm(t, {
    prefixDir,
    mocks: {
      '{LIB}/utils/open-url.js': { openUrl },
    },
    config,
  })

  return {
    ...res,
    opened,
  }
}

t.test('open docs urls', async t => {
  const expect = {
    nodocs: 'https://www.npmjs.com/package/nodocs',
    docsurl: 'https://bugzilla.localhost/docsurl',
    repourl: 'https://github.com/foo/repourl#readme',
    repoobj: 'https://github.com/foo/repoobj#readme',
    repourlobj: 'https://www.npmjs.com/package/repourlobj',
    '.': 'https://example.com',
  }

  for (const [key, url] of Object.entries(expect)) {
    await t.test(`open ${key} url`, async t => {
      const { npm, opened } = await setup(t)
      await npm.exec('docs', [['.', key].join(sep)])
      t.strictSame({ [url]: 1 }, opened, `opened ${url}`)
    })
  }
})

t.test('open default package if none specified', async t => {
  const { npm, opened } = await setup(t)

  await npm.exec('docs', [])
  t.strictSame({ 'https://example.com': 1 }, opened, 'opened expected url')
})

t.test('workspaces', async (t) => {
  await t.test('all workspaces', async t => {
    const { npm, opened } = await setup(t, {
      prefixDir: fixtures.workspaces,
      config: { workspaces: true },
    })
    await npm.exec('docs', [])
    t.strictSame({
      'http://docs.workspace-a/': 1,
      'https://github.com/npm/workspace-b#readme': 1,
    }, opened, 'opened two valid docs urls')
  })

  await t.test('one workspace', async t => {
    const { npm, opened } = await setup(t, {
      prefixDir: fixtures.workspaces,
      config: { workspace: 'workspace-a' },
    })
    await npm.exec('docs', [])
    t.strictSame({
      'http://docs.workspace-a/': 1,
    }, opened, 'opened one requested docs urls')
  })

  await t.test('invalid workspace', async t => {
    const { npm, opened } = await setup(t, {
      prefixDir: fixtures.workspaces,
      config: { workspace: 'workspace-x' },
    })
    await t.rejects(
      npm.exec('docs', []),
      /No workspaces found/
    )
    await t.rejects(
      npm.exec('docs', []),
      /workspace-x/
    )
    t.match({}, opened, 'opened no docs urls')
  })
})
