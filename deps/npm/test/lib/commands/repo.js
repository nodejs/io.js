const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm.js')
const { sep } = require('node:path')

const fixture = {
  'package.json': JSON.stringify({
    name: 'thispkg',
    version: '1.2.3',
    repository: 'https://example.com/thispkg.git',
  }),
  norepo: {
    'package.json': JSON.stringify({
      name: 'norepo',
      version: '1.2.3',
    }),
  },
  'repoobbj-nourl': {
    'package.json': JSON.stringify({
      name: 'repoobj-nourl',
      repository: { no: 'url' },
    }),
  },
  hostedgit: {
    'package.json': JSON.stringify({
      repository: 'git://github.com/foo/hostedgit',
    }),
  },
  hostedgitat: {
    'package.json': JSON.stringify({
      repository: 'git@github.com:foo/hostedgitat',
    }),
  },
  hostedssh: {
    'package.json': JSON.stringify({
      repository: 'ssh://git@github.com/foo/hostedssh',
    }),
  },
  hostedgitssh: {
    'package.json': JSON.stringify({
      repository: 'git+ssh://git@github.com/foo/hostedgitssh',
    }),
  },
  hostedgithttp: {
    'package.json': JSON.stringify({
      repository: 'git+http://github.com/foo/hostedgithttp',
    }),
  },
  hostedgithttps: {
    'package.json': JSON.stringify({
      repository: 'git+https://github.com/foo/hostedgithttps',
    }),
  },
  hostedgitobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git://github.com/foo/hostedgitobj' },
    }),
  },
  hostedgitatobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git@github.com:foo/hostedgitatobj' },
    }),
  },
  hostedsshobj: {
    'package.json': JSON.stringify({
      repository: { url: 'ssh://git@github.com/foo/hostedsshobj' },
    }),
  },
  hostedgitsshobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git+ssh://git@github.com/foo/hostedgitsshobj' },
    }),
  },
  hostedgithttpobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git+http://github.com/foo/hostedgithttpobj' },
    }),
  },
  hostedgithttpsobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git+https://github.com/foo/hostedgithttpsobj' },
    }),
  },
  unhostedgit: {
    'package.json': JSON.stringify({
      repository: 'git://gothib.com/foo/unhostedgit',
    }),
  },
  unhostedgitat: {
    'package.json': JSON.stringify({
      repository: 'git@gothib.com:foo/unhostedgitat',
    }),
  },
  unhostedssh: {
    'package.json': JSON.stringify({
      repository: 'ssh://git@gothib.com/foo/unhostedssh',
    }),
  },
  unhostedgitssh: {
    'package.json': JSON.stringify({
      repository: 'git+ssh://git@gothib.com/foo/unhostedgitssh',
    }),
  },
  unhostedgithttp: {
    'package.json': JSON.stringify({
      repository: 'git+http://gothib.com/foo/unhostedgithttp',
    }),
  },
  unhostedgithttps: {
    'package.json': JSON.stringify({
      repository: 'git+https://gothib.com/foo/unhostedgithttps',
    }),
  },
  unhostedgitobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git://gothib.com/foo/unhostedgitobj' },
    }),
  },
  unhostedgitatobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git@gothib.com:foo/unhostedgitatobj' },
    }),
  },
  unhostedsshobj: {
    'package.json': JSON.stringify({
      repository: { url: 'ssh://git@gothib.com/foo/unhostedsshobj' },
    }),
  },
  unhostedgitsshobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git+ssh://git@gothib.com/foo/unhostedgitsshobj' },
    }),
  },
  unhostedgithttpobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git+http://gothib.com/foo/unhostedgithttpobj' },
    }),
  },
  unhostedgithttpsobj: {
    'package.json': JSON.stringify({
      repository: { url: 'git+https://gothib.com/foo/unhostedgithttpsobj' },
    }),
  },
  directory: {
    'package.json': JSON.stringify({
      repository: {
        type: 'git',
        url: 'git+https://github.com/foo/test-repo-with-directory.git',
        directory: 'some/directory',
      },
    }),
  },
}

const workspaceFixture = {
  'package.json': JSON.stringify({
    name: 'workspaces-test',
    version: '1.2.3-test',
    workspaces: ['workspace-a', 'workspace-b', 'workspace-c'],
    repository: 'https://github.com/npm/workspaces-test',
  }),
  'workspace-a': {
    'package.json': JSON.stringify({
      name: 'workspace-a',
      version: '1.2.3-a',
      repository: 'http://repo.workspace-a/',
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
}

const loadMockNpm = async (t, prefixDir, config = {}) => {
  // keep a tally of which urls got opened
  const opened = {}

  const mock = await mockNpm(t, {
    command: 'repo',
    mocks: {
      '{LIB}/utils/open-url.js': {
        openUrl: async (_, url) => {
          opened[url] = opened[url] || 0
          opened[url]++
        },
      },
    },
    config,
    prefixDir,
  })

  return {
    ...mock,
    opened,
  }
}

t.test('open repo urls', async t => {
  const { repo, opened } = await loadMockNpm(t, fixture)
  const expect = {
    hostedgit: 'https://github.com/foo/hostedgit',
    hostedgitat: 'https://github.com/foo/hostedgitat',
    hostedssh: 'https://github.com/foo/hostedssh',
    hostedgitssh: 'https://github.com/foo/hostedgitssh',
    hostedgithttp: 'https://github.com/foo/hostedgithttp',
    hostedgithttps: 'https://github.com/foo/hostedgithttps',
    hostedgitobj: 'https://github.com/foo/hostedgitobj',
    hostedgitatobj: 'https://github.com/foo/hostedgitatobj',
    hostedsshobj: 'https://github.com/foo/hostedsshobj',
    hostedgitsshobj: 'https://github.com/foo/hostedgitsshobj',
    hostedgithttpobj: 'https://github.com/foo/hostedgithttpobj',
    hostedgithttpsobj: 'https://github.com/foo/hostedgithttpsobj',
    unhostedgit: 'https://gothib.com/foo/unhostedgit',
    unhostedssh: 'https://gothib.com/foo/unhostedssh',
    unhostedgitssh: 'https://gothib.com/foo/unhostedgitssh',
    unhostedgithttp: 'http://gothib.com/foo/unhostedgithttp',
    unhostedgithttps: 'https://gothib.com/foo/unhostedgithttps',
    unhostedgitobj: 'https://gothib.com/foo/unhostedgitobj',
    unhostedsshobj: 'https://gothib.com/foo/unhostedsshobj',
    unhostedgitsshobj: 'https://gothib.com/foo/unhostedgitsshobj',
    unhostedgithttpobj: 'http://gothib.com/foo/unhostedgithttpobj',
    unhostedgithttpsobj: 'https://gothib.com/foo/unhostedgithttpsobj',
    directory: 'https://github.com/foo/test-repo-with-directory/tree/HEAD/some/directory',
    '.': 'https://example.com/thispkg',
  }
  for (const [pkg, url] of Object.entries(expect)) {
    await repo.exec([['.', pkg].join(sep)])
    t.equal(opened[url], 1, `opened ${url}`)
  }
})

t.test('fail if cannot figure out repo url', async t => {
  const { repo } = await loadMockNpm(t, fixture)

  const cases = [
    'norepo',
    'repoobbj-nourl',
    'unhostedgitat',
    'unhostedgitatobj',
  ]

  for (const pkg of cases) {
    await t.rejects(
      repo.exec([['.', pkg].join(sep)]),
      { pkgid: pkg }
    )
  }
})

t.test('open default package if none specified', async t => {
  const { repo, opened } = await loadMockNpm(t, fixture)
  await repo.exec([])
  t.equal(opened['https://example.com/thispkg'], 1, 'opened expected url', { opened })
})

t.test('workspaces', async t => {
  const mockWorkspaces = (t, config) => loadMockNpm(t, workspaceFixture, config)

  t.test('include workspace root', async (t) => {
    const { opened, repo } = await mockWorkspaces(t, {
      workspaces: true,
      'include-workspace-root': true,
    })
    await repo.exec([])
    t.match({
      'https://github.com/npm/workspaces-test': 1,
      'https://repo.workspace-a/': 1, // Gets translated to https!
      'https://github.com/npm/workspace-b': 1,
    }, opened, 'opened two valid repo urls')
  })

  t.test('all workspaces', async (t) => {
    const { opened, repo } = await mockWorkspaces(t, {
      workspaces: true,
    })
    await repo.exec([])
    t.match({
      'https://repo.workspace-a/': 1, // Gets translated to https!
      'https://github.com/npm/workspace-b': 1,
    }, opened, 'opened two valid repo urls')
  })

  t.test('one workspace', async (t) => {
    const { opened, repo } = await mockWorkspaces(t, {
      workspace: ['workspace-a'],
    })
    await repo.exec([])
    t.match({
      'https://repo.workspace-a/': 1,
    }, opened, 'opened one requested repo urls')
  })

  t.test('invalid workspace', async (t) => {
    const { opened, repo } = await mockWorkspaces(t, {
      workspace: ['workspace-x'],
    })
    await t.rejects(
      repo.exec([]),
      /workspace-x/
    )
    t.match({}, opened, 'opened no repo urls')
  })

  t.test('package arg and workspace', async (t) => {
    const { opened, repo } = await mockWorkspaces(t, {
      workspace: ['workspace-x'],
    })
    await repo.exec(['.'])
    t.match({
      'https://github.com/npm/workspaces-test': 1,
    }, opened, 'opened url for package arg, not workspace')
  })
})
