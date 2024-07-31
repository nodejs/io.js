const t = require('tap')
const MockRegistry = require('@npmcli/mock-registry')
const _mockNpm = require('../../fixtures/mock-npm')
const { cleanCwd } = require('../../fixtures/clean-snapshot')

t.cleanSnapshot = (str) => cleanCwd(str)

const packument = spec => {
  const mocks = {
    cat: {
      name: 'cat',
      'dist-tags': {
        latest: '1.0.1',
      },
      versions: {
        '1.0.1': {
          version: '1.0.1',
          dependencies: {
            dog: '2.0.0',
          },
        },
      },
    },
    chai: {
      name: 'chai',
      'dist-tags': {
        latest: '1.0.1',
      },
      versions: {
        '1.0.1': {
          version: '1.0.1',
        },
      },
    },
    dog: {
      name: 'dog',
      'dist-tags': {
        latest: '2.0.0',
      },
      versions: {
        '1.0.1': {
          version: '1.0.1',
        },
        '2.0.0': {
          version: '2.0.0',
        },
      },
    },
    theta: {
      name: 'theta',
      'dist-tags': {
        latest: '1.0.1',
      },
      versions: {
        '1.0.1': {
          version: '1.0.1',
        },
      },
    },
  }

  if (spec.name === 'eta') {
    throw new Error('There is an error with this package.')
  }

  if (!mocks[spec.name]) {
    const err = new Error()
    err.code = 'E404'
    throw err
  }

  return mocks[spec.name]
}

const fixtures = {
  global: {
    node_modules: {
      cat: {
        'package.json': JSON.stringify({
          name: 'cat',
          version: '1.0.0',
        }, null, 2),
      },
    },
  },
  local: {
    'package.json': JSON.stringify({
      name: 'delta',
      version: '1.0.0',
      dependencies: {
        cat: '^1.0.0',
        dog: '^1.0.0',
        theta: '^1.0.0',
      },
      devDependencies: {
        zeta: '^1.0.0',
      },
      optionalDependencies: {
        lorem: '^1.0.0',
      },
      peerDependencies: {
        chai: '^1.0.0',
      },
    }, null, 2),
    node_modules: {
      cat: {
        'package.json': JSON.stringify({
          name: 'cat',
          version: '1.0.0',
          dependencies: {
            dog: '2.0.0',
          },
        }, null, 2),
        node_modules: {
          dog: {
            'package.json': JSON.stringify({
              name: 'dog',
              version: '2.0.0',
            }, null, 2),
          },
        },
      },
      chai: {
        'package.json': JSON.stringify({
          name: 'chai',
          version: '1.0.0',
        }, null, 2),
      },
      dog: {
        'package.json': JSON.stringify({
          name: 'dog',
          version: '1.0.1',
        }, null, 2),
      },
      zeta: {
        'package.json': JSON.stringify({
          name: 'zeta',
          version: '1.0.0',
        }, null, 2),
      },
    },
  },
  workspaces: {
    'package.json': JSON.stringify({
      name: 'workspaces-project',
      version: '1.0.0',
      workspaces: ['packages/*'],
      dependencies: {
        dog: '^1.0.0',
      },
    }),
    node_modules: {
      a: t.fixture('symlink', '../packages/a'),
      b: t.fixture('symlink', '../packages/b'),
      c: t.fixture('symlink', '../packages/c'),
      d: t.fixture('symlink', '../packages/d'),
      e: t.fixture('symlink', '../packages/e'),
      cat: {
        'package.json': JSON.stringify({
          name: 'cat',
          version: '1.0.0',
          dependencies: {
            dog: '2.0.0',
          },
        }),
        node_modules: {
          dog: {
            'package.json': JSON.stringify({
              name: 'dog',
              version: '2.0.0',
            }),
          },
        },
      },
      chai: {
        'package.json': JSON.stringify({
          name: 'chai',
          version: '1.0.0',
        }),
      },
      dog: {
        'package.json': JSON.stringify({
          name: 'dog',
          version: '1.0.1',
        }),
      },
      foo: {
        'package.json': JSON.stringify({
          name: 'foo',
          version: '1.0.0',
          dependencies: {
            chai: '^1.0.0',
          },
        }),
      },
      zeta: {
        'package.json': JSON.stringify({
          name: 'zeta',
          version: '1.0.0',
        }),
      },
    },
    packages: {
      a: {
        'package.json': JSON.stringify({
          name: 'a',
          version: '1.0.0',
          dependencies: {
            b: '^1.0.0',
            cat: '^1.0.0',
            foo: '^1.0.0',
          },
        }),
      },
      b: {
        'package.json': JSON.stringify({
          name: 'b',
          version: '1.0.0',
          dependencies: {
            zeta: '^1.0.0',
          },
        }),
      },
      c: {
        'package.json': JSON.stringify({
          name: 'c',
          version: '1.0.0',
          dependencies: {
            theta: '^1.0.0',
          },
        }),
      },
      d: {
        'package.json': JSON.stringify({
          name: 'd',
          version: '1.0.0',
          dependencies: {
            theta: '^1.0.0',
          },
        }),
      },
      e: {
        'package.json': JSON.stringify({
          name: 'e',
          version: '1.0.0',
          dependencies: {
            theta: '^1.0.0',
          },
        }),
      },
    },
  },
}

const mockNpm = async (t, { prefixDir, ...opts } = {}) => {
  const res = await _mockNpm(t, {
    command: 'outdated',
    mocks: {
      pacote: {
        packument,
      },
    },
    ...opts,
    prefixDir,
  })

  // this is not currently used, but ensures that no requests are
  // hitting the registry.
  // XXX: the pacote mock should be replaced with mock registry calls
  const registry = new MockRegistry({
    tap: t,
    registry: res.npm.config.get('registry'),
    strict: true,
  })

  return {
    ...res,
    registry,
  }
}

t.test('should display outdated deps', async t => {
  await t.test('outdated global', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      globalPrefixDir: fixtures.global,
      config: { global: true },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        color: 'always',
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --omit=dev', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        omit: ['dev'],
        color: 'always',
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --omit=dev --omit=peer', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        omit: ['dev', 'peer'],
        color: 'always',
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --omit=prod', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        omit: ['prod'],
        color: 'always',
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --long', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        long: true,
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --json', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        json: true,
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --json --long', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        json: true,
        long: true,
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --parseable', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        parseable: true,
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --parseable --long', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        parseable: true,
        long: true,
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated --all', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
      config: {
        all: true,
      },
    })
    await outdated.exec([])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })

  await t.test('outdated specific dep', async t => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.local,
    })
    await outdated.exec(['cat'])
    t.equal(process.exitCode, 1)
    t.matchSnapshot(joinedOutput())
  })
})

t.test('should return if no outdated deps', async t => {
  const testDir = {
    'package.json': JSON.stringify({
      name: 'delta',
      version: '1.0.0',
      dependencies: {
        cat: '^1.0.0',
      },
    }, null, 2),
    node_modules: {
      cat: {
        'package.json': JSON.stringify({
          name: 'cat',
          version: '1.0.1',
        }, null, 2),
      },
    },
  }

  const { outdated, joinedOutput } = await mockNpm(t, {
    prefixDir: testDir,

  })
  await outdated.exec([])
  t.equal(joinedOutput(), '', 'no logs')
})

t.test('throws if error with a dep', async t => {
  const testDir = {
    'package.json': JSON.stringify({
      name: 'delta',
      version: '1.0.0',
      dependencies: {
        eta: '^1.0.0',
      },
    }, null, 2),
    node_modules: {
      eta: {
        'package.json': JSON.stringify({
          name: 'eta',
          version: '1.0.1',
        }, null, 2),
      },
    },
  }

  const { outdated } = await mockNpm(t, {
    prefixDir: testDir,
  })

  await t.rejects(outdated.exec([]), 'There is an error with this package.')
})

t.test('should skip missing non-prod deps', async t => {
  const testDir = {
    'package.json': JSON.stringify({
      name: 'delta',
      version: '1.0.0',
      devDependencies: {
        chai: '^1.0.0',
      },
    }, null, 2),
    node_modules: {},
  }

  const { outdated, joinedOutput } = await mockNpm(t, {
    prefixDir: testDir,
  })

  await outdated.exec([])

  t.equal(joinedOutput(), '', 'no logs')
})

t.test('should skip invalid pkg ranges', async t => {
  const testDir = {
    'package.json': JSON.stringify({
      name: 'delta',
      version: '1.0.0',
      dependencies: {
        cat: '>=^2',
      },
    }, null, 2),
    node_modules: {
      cat: {
        'package.json': JSON.stringify({
          name: 'cat',
          version: '1.0.0',
        }, null, 2),
      },
    },
  }

  const { outdated, joinedOutput } = await mockNpm(t, {
    prefixDir: testDir,
  })
  await outdated.exec([])
  t.equal(joinedOutput(), '', 'no logs')
})

t.test('should skip git specs', async t => {
  const testDir = {
    'package.json': JSON.stringify({
      name: 'delta',
      version: '1.0.0',
      dependencies: {
        cat: 'github:username/foo',
      },
    }, null, 2),
    node_modules: {
      cat: {
        'package.json': JSON.stringify({
          name: 'cat',
          version: '1.0.0',
        }, null, 2),
      },
    },
  }

  const { outdated, joinedOutput } = await mockNpm(t, {
    prefixDir: testDir,
  })
  await outdated.exec([])
  t.equal(joinedOutput(), '', 'no logs')
})

t.test('workspaces', async t => {
  const mockWorkspaces = async (t, { exitCode = 1, ...config } = {}) => {
    const { outdated, joinedOutput } = await mockNpm(t, {
      prefixDir: fixtures.workspaces,
      config,
    })

    await outdated.exec([])

    t.matchSnapshot(joinedOutput(), 'output')
    t.equal(process.exitCode, exitCode ?? undefined)
  }

  await t.test('should display ws outdated deps human output', t =>
    mockWorkspaces(t))

  // TODO: This should display dog, but doesn't because arborist filters
  // workspace deps even if they're also root deps
  // This will be fixed in a future arborist version
  await t.test('should display only root outdated when ws disabled', t =>
    mockWorkspaces(t, { workspaces: false, exitCode: null }))

  await t.test('should display ws outdated deps json output', t =>
    mockWorkspaces(t, { json: true }))

  await t.test('should display ws outdated deps parseable output', t =>
    mockWorkspaces(t, { parseable: true }))

  await t.test('should display all dependencies', t =>
    mockWorkspaces(t, { all: true }))

  await t.test('should highlight ws in dependend by section', t =>
    mockWorkspaces(t, { color: 'always' }))

  await t.test('should display results filtered by ws', t =>
    mockWorkspaces(t, { workspace: 'a' }))

  await t.test('should display json results filtered by ws', t =>
    mockWorkspaces(t, { json: true, workspace: 'a' }))

  await t.test('should display parseable results filtered by ws', t =>
    mockWorkspaces(t, { parseable: true, workspace: 'a' }))

  await t.test('should display nested deps when filtering by ws and using --all', t =>
    mockWorkspaces(t, { all: true, workspace: 'a' }))

  await t.test('should display no results if ws has no deps to display', t =>
    mockWorkspaces(t, { workspace: 'b', exitCode: null }))

  await t.test('should display missing deps when filtering by ws', t =>
    mockWorkspaces(t, { workspace: 'c', exitCode: 1 }))
})

t.test('aliases', async t => {
  const testDir = {
    'package.json': JSON.stringify({
      name: 'display-aliases',
      version: '1.0.0',
      dependencies: {
        cat: 'npm:dog@latest',
      },
    }),
    node_modules: {
      cat: {
        'package.json': JSON.stringify({
          name: 'dog',
          version: '1.0.0',
        }),
      },
    },
  }

  const { outdated, joinedOutput } = await mockNpm(t, {
    prefixDir: testDir,
  })
  await outdated.exec([])

  t.matchSnapshot(joinedOutput(), 'should display aliased outdated dep output')
  t.equal(process.exitCode, 1)
})

t.test('aliases with version range', async t => {
  const testDir = {
    'package.json': JSON.stringify({
      name: 'display-aliases',
      version: '1.0.0',
      dependencies: {
        cat: 'npm:dog@^1.0.0',
      },
    }),
    node_modules: {
      cat: {
        'package.json': JSON.stringify({
          name: 'dog',
          version: '1.0.0',
        }),
      },
    },
  }

  const { outdated, joinedOutput } = await mockNpm(t, {
    prefixDir: testDir,
  })
  await outdated.exec([])

  t.matchSnapshot(
    joinedOutput(),
    'should display aliased outdated dep output with correct wanted values'
  )
  t.equal(process.exitCode, 1)
})
