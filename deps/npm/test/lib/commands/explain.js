const t = require('tap')
const { resolve } = require('node:path')
const mockNpm = require('../../fixtures/mock-npm.js')

const mockExplain = async (t, opts) => {
  const mock = await mockNpm(t, {
    command: 'explain',
    mocks: {
      // keep the snapshots pared down a bit, since this has its own tests.
      '{LIB}/utils/explain-dep.js': {
        explainNode: (expl, depth, chalk) => {
          const color = chalk.level !== 0
          return `${expl.name}@${expl.version} depth=${depth} color=${color}`
        },
      },
    },
    ...opts,
  })

  return mock
}

t.test('no args throws usage', async t => {
  const { explain } = await mockExplain(t)
  await t.rejects(
    explain.exec([]),
    explain.usage
  )
})

t.test('no match throws not found', async t => {
  const { explain } = await mockExplain(t)
  await t.rejects(
    explain.exec(['foo@1.2.3', 'node_modules/baz']),
    'No dependencies found matching foo@1.2.3, node_modules/baz'
  )
})

t.test('invalid package name throws not found', async t => {
  const { explain } = await mockExplain(t)
  const badName = ' not a valid package name '
  await t.rejects(
    explain.exec([`${badName}@1.2.3`]),
    `No dependencies found matching ${badName}@1.2.3`
  )
})

t.test('explain some nodes', async t => {
  const mockNodes = async (t, config = {}) => {
    const mock = await mockExplain(t, {
      prefixDir: {
        node_modules: {
          foo: {
            'package.json': JSON.stringify({
              name: 'foo',
              version: '1.2.3',
              dependencies: {
                bar: '*',
              },
            }),
          },
          bar: {
            'package.json': JSON.stringify({
              name: 'bar',
              version: '1.2.3',
            }),
          },
          baz: {
            'package.json': JSON.stringify({
              name: 'baz',
              version: '1.2.3',
              dependencies: {
                foo: '*',
                bar: '2',
              },
            }),
            node_modules: {
              bar: {
                'package.json': JSON.stringify({
                  name: 'bar',
                  version: '2.3.4',
                }),
              },
              extra: {
                'package.json': JSON.stringify({
                  name: 'extra',
                  version: '99.9999.999999',
                  description: 'extraneous package',
                }),
              },
            },
          },
        },
        'package.json': JSON.stringify({
          dependencies: {
            baz: '1',
          },
        }),
      },
      config: {
        color: 'always',
        ...config,
      },
    })

    return mock
  }

  t.test('works with the location', async t => {
    const path = 'node_modules/foo'
    const { explain, joinedOutput } = await mockNodes(t)
    await explain.exec([path])
    t.strictSame(joinedOutput(), 'foo@1.2.3 depth=Infinity color=true')
  })

  t.test('works with a full actual path', async t => {
    const { npm, explain, joinedOutput } = await mockNodes(t)
    const path = resolve(npm.prefix, 'node_modules/foo')
    await explain.exec([path])
    t.strictSame(joinedOutput(), 'foo@1.2.3 depth=Infinity color=true')
  })

  t.test('finds all nodes by name', async t => {
    const { explain, joinedOutput } = await mockNodes(t)
    await explain.exec(['bar'])
    t.strictSame(joinedOutput(),
      'bar@1.2.3 depth=Infinity color=true\n\n' +
      'bar@2.3.4 depth=Infinity color=true'
    )
  })

  t.test('finds only nodes that match the spec', async t => {
    const { explain, joinedOutput } = await mockNodes(t)
    await explain.exec(['bar@1'])
    t.strictSame(joinedOutput(), 'bar@1.2.3 depth=Infinity color=true')
  })

  t.test('finds extraneous nodes', async t => {
    const { explain, joinedOutput } = await mockNodes(t)
    await explain.exec(['extra'])
    t.strictSame(joinedOutput(), 'extra@99.9999.999999 depth=Infinity color=true')
  })

  t.test('json output', async t => {
    const { explain, joinedOutput } = await mockNodes(t, { json: true })
    await explain.exec(['node_modules/foo'])
    t.match(JSON.parse(joinedOutput()), [{
      name: 'foo',
      version: '1.2.3',
      dependents: Array,
    }])
  })

  t.test('report if no nodes found', async t => {
    const { explain } = await mockNodes(t)
    await t.rejects(
      explain.exec(['asdf/foo/bar', 'quux@1.x']),
      'No dependencies found matching asdf/foo/bar, quux@1.x'
    )
  })
})

t.test('workspaces', async t => {
  const mockWorkspaces = async (t, exec = [], workspaces = true) => {
    const mock = await mockExplain(t, {
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'workspaces-project',
          version: '1.0.0',
          workspaces: ['packages/*'],
          dependencies: {
            abbrev: '^1.0.0',
          },
        }),
        node_modules: {
          a: t.fixture('symlink', '../packages/a'),
          b: t.fixture('symlink', '../packages/b'),
          c: t.fixture('symlink', '../packages/c'),
          once: {
            'package.json': JSON.stringify({
              name: 'once',
              version: '1.0.0',
              dependencies: {
                wrappy: '2.0.0',
              },
            }),
          },
          abbrev: {
            'package.json': JSON.stringify({
              name: 'abbrev',
              version: '1.0.0',
            }),
          },
          wrappy: {
            'package.json': JSON.stringify({
              name: 'wrappy',
              version: '2.0.0',
            }),
          },
        },
        packages: {
          a: {
            'package.json': JSON.stringify({
              name: 'a',
              version: '1.0.0',
              dependencies: {
                once: '1.0.0',
              },
            }),
          },
          b: {
            'package.json': JSON.stringify({
              name: 'b',
              version: '1.0.0',
              dependencies: {
                abbrev: '^1.0.0',
              },
            }),
          },
          c: {
            'package.json': JSON.stringify({
              name: 'c',
              version: '1.0.0',
            }),
          },
        },
      },
      config: {
        ...(typeof workspaces === 'boolean' ? { workspaces } : { workspace: workspaces }),
        color: 'always',
      },
    })

    await mock.explain.exec(exec)

    return mock.joinedOutput()
  }

  t.test('should explain workspaces deps', async t => {
    const OUTPUT = await mockWorkspaces(t, ['wrappy'])
    t.strictSame(
      OUTPUT,
      'wrappy@2.0.0 depth=Infinity color=true'
    )
  })

  t.test('should explain deps when filtering to a single ws', async t => {
    const OUTPUT = await mockWorkspaces(t, ['wrappy'], ['a'])
    t.strictSame(
      OUTPUT,
      'wrappy@2.0.0 depth=Infinity color=true'
    )
  })

  t.test('should explain deps of workspaces only', async t => {
    const OUTPUT = await mockWorkspaces(t, ['abbrev'])
    t.strictSame(
      OUTPUT,
      'abbrev@1.0.0 depth=Infinity color=true'
    )
  })

  t.test('should throw usage if dep not found within filtered ws', async t => {
    await t.rejects(
      mockWorkspaces(t, ['abbrev'], ['a']),
      'No dependencies found matching abbrev'
    )
  })

  t.test('workspaces disabled', async t => {
    await t.rejects(
      mockWorkspaces(t, ['once'], false),
      'No dependencies found matching once',
      'should throw usage if dep not found when excluding ws'
    )
  })
})
