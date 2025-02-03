const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')
const { cleanCwd } = require('../../fixtures/clean-snapshot.js')

t.cleanSnapshot = (str) => cleanCwd(str)

t.test('simple query', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          name: 'a',
          version: '1.0.0',
        },
        b: {
          name: 'b',
          version: '^2.0.0',
        },
      },
      'package.json': JSON.stringify({
        name: 'project',
        dependencies: {
          a: '^1.0.0',
          b: '^1.0.0',
        },
        peerDependencies: {
          c: '1.0.0',
        },
      }),
    },
  })
  await npm.exec('query', [':root, :root > *'])
  t.matchSnapshot(joinedOutput(), 'should return root object and direct children')
})

t.test('recursive tree', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          name: 'a',
          version: '1.0.0',
        },
        b: {
          name: 'b',
          version: '^2.0.0',
          dependencies: {
            a: '1.0.0',
          },
        },
      },
      'package.json': JSON.stringify({
        name: 'project',
        dependencies: {
          a: '^1.0.0',
          b: '^1.0.0',
        },
      }),
    },
  })
  await npm.exec('query', ['*'])
  t.matchSnapshot(joinedOutput(), 'should return everything in the tree, accounting for recursion')
})

t.test('workspace query', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      workspace: ['c'],
    },
    prefixDir: {
      node_modules: {
        a: {
          name: 'a',
          version: '1.0.0',
        },
        b: {
          name: 'b',
          version: '^2.0.0',
        },
        c: t.fixture('symlink', '../c'),
      },
      c: {
        'package.json': JSON.stringify({
          name: 'c',
          version: '1.0.0',
        }),
      },
      'package.json': JSON.stringify({
        name: 'project',
        workspaces: ['c'],
        dependencies: {
          a: '^1.0.0',
          b: '^1.0.0',
        },
      }),
    },
  })
  await npm.exec('query', [':scope'])
  t.matchSnapshot(joinedOutput(), 'should return workspace object')
})

t.test('include-workspace-root', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      'include-workspace-root': true,
      workspace: ['c'],
    },
    prefixDir: {
      node_modules: {
        a: {
          name: 'a',
          version: '1.0.0',
        },
        b: {
          name: 'b',
          version: '^2.0.0',
        },
        c: t.fixture('symlink', '../c'),
      },
      c: {
        'package.json': JSON.stringify({
          name: 'c',
          version: '1.0.0',
        }),
      },
      'package.json': JSON.stringify({
        name: 'project',
        workspaces: ['c'],
        dependencies: {
          a: '^1.0.0',
          b: '^1.0.0',
        },
      }),
    },
  })
  await npm.exec('query', [':scope'])
  t.matchSnapshot(joinedOutput(), 'should return workspace object and root object')
})
t.test('linked node', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: t.fixture('symlink', '../a'),
      },
      a: {
        'package.json': JSON.stringify({
          name: 'a',
          version: '1.0.0',
        }),
      },
      'package.json': JSON.stringify({
        name: 'project',
        dependencies: {
          a: 'file:./a',
        },
      }),
    },
  })
  await npm.exec('query', ['[name=a]'])
  t.matchSnapshot(joinedOutput(), 'should return linked node res')
})

t.test('global', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      global: true,
    },
    globalPrefixDir: {
      node_modules: {
        lorem: {
          'package.json': JSON.stringify({
            name: 'lorem',
            version: '2.0.0',
          }),
        },
      },

    },
  })
  await npm.exec('query', ['[name=lorem]'])
  t.matchSnapshot(joinedOutput(), 'should return global package')
})

t.test('package-lock-only', t => {
  t.test('no package lock', async t => {
    const { npm } = await loadMockNpm(t, {
      config: {
        'package-lock-only': true,
      },
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'project',
          dependencies: {
            a: '^1.0.0',
          },
        }),
      },
    })
    await t.rejects(npm.exec('query', [':root, :root > *']), { code: 'EUSAGE' })
  })

  t.test('with package lock', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, {
      config: {
        'package-lock-only': true,
      },
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'project',
          dependencies: {
            a: '^1.0.0',
          },
        }),
        'package-lock.json': JSON.stringify({
          name: 'project',
          lockfileVersion: 3,
          requires: true,
          packages: {
            '': {
              dependencies: {
                a: '^1.0.0',
              },
            },
            'node_modules/a': {
              version: '1.2.3',
              resolved: 'https://dummy.npmjs.org/a/-/a-1.2.3.tgz',
              integrity: 'sha512-dummy',
              engines: {
                node: '>=14.17',
              },
            },
          },
        }),
      },
    })
    await npm.exec('query', ['*'])
    t.matchSnapshot(joinedOutput(), 'should return valid response with only lock info')
  })
  t.end()
})

t.test('expect entries', t => {
  const { exitCode } = process
  t.afterEach(() => process.exitCode = exitCode)
  const prefixDir = {
    node_modules: {
      a: { name: 'a', version: '1.0.0' },
    },
    'package.json': JSON.stringify({
      name: 'project',
      dependencies: { a: '^1.0.0' },
    }),
  }
  t.test('false, has entries', async t => {
    const { logs, npm, joinedOutput } = await loadMockNpm(t, {
      prefixDir,
    })
    npm.config.set('expect-results', false)
    await npm.exec('query', ['#a'])
    t.not(joinedOutput(), '[]', 'has entries')
    t.same(logs.warn.byTitle('query'), ['query Expected no results, got 1'])
    t.ok(process.exitCode, 'exits with code')
  })
  t.test('false, no entries', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, {
      prefixDir,
    })
    npm.config.set('expect-results', false)
    await npm.exec('query', ['#b'])
    t.equal(joinedOutput(), '[]', 'does not have entries')
    t.notOk(process.exitCode, 'exits without code')
  })
  t.test('true, has entries', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, {
      prefixDir,
    })
    npm.config.set('expect-results', true)
    await npm.exec('query', ['#a'])
    t.not(joinedOutput(), '[]', 'has entries')
    t.notOk(process.exitCode, 'exits without code')
  })
  t.test('true, no entries', async t => {
    const { logs, npm, joinedOutput } = await loadMockNpm(t, {
      prefixDir,
    })
    npm.config.set('expect-results', true)
    await npm.exec('query', ['#b'])
    t.equal(joinedOutput(), '[]', 'does not have entries')
    t.same(logs.warn.byTitle('query'), ['query Expected results, got 0'])
    t.ok(process.exitCode, 'exits with code')
  })
  t.test('count, matches', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, {
      prefixDir,
    })
    npm.config.set('expect-result-count', 1)
    await npm.exec('query', ['#a'])
    t.not(joinedOutput(), '[]', 'has entries')
    t.notOk(process.exitCode, 'exits without code')
  })
  t.test('count 1, does not match', async t => {
    const { logs, npm, joinedOutput } = await loadMockNpm(t, {
      prefixDir,
    })
    npm.config.set('expect-result-count', 1)
    await npm.exec('query', ['#b'])
    t.equal(joinedOutput(), '[]', 'does not have entries')
    t.same(logs.warn.byTitle('query'), ['query Expected 1 result, got 0'])
    t.ok(process.exitCode, 'exits with code')
  })
  t.test('count 3, does not match', async t => {
    const { logs, npm, joinedOutput } = await loadMockNpm(t, {
      prefixDir,
    })
    npm.config.set('expect-result-count', 3)
    await npm.exec('query', ['#b'])
    t.equal(joinedOutput(), '[]', 'does not have entries')
    t.same(logs.warn.byTitle('query'), ['query Expected 3 results, got 0'])
    t.ok(process.exitCode, 'exits with code')
  })
  t.end()
})

t.test('missing', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      node_modules: {
        a: {
          name: 'a',
          version: '1.0.0',
        },
      },
      'package.json': JSON.stringify({
        name: 'project',
        dependencies: {
          a: '^1.0.0',
          b: '^1.0.0',
        },
      }),
    },
  })
  await npm.exec('query', [':missing'])
  t.matchSnapshot(joinedOutput(), 'should return missing node')
})
