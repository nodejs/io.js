const t = require('tap')
const { join, extname } = require('node:path')
const MockRegistry = require('@npmcli/mock-registry')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')

const jsonifyTestdir = (obj) => {
  for (const [key, value] of Object.entries(obj || {})) {
    if (extname(key) === '.json') {
      obj[key] = JSON.stringify(value, null, 2) + '\n'
    } else if (typeof value === 'object') {
      obj[key] = jsonifyTestdir(value)
    } else {
      obj[key] = value.trim() + '\n'
    }
  }
  return obj
}

// generic helper to call diff with a specified dir contents and registry calls
const mockDiff = async (t, {
  exec,
  diff = [],
  tarballs = {},
  times = {},
  ...opts
} = {}) => {
  const tarballFixtures = Object.entries(tarballs).reduce((acc, [spec, fixture]) => {
    const lastAt = spec.lastIndexOf('@')
    const name = spec.slice(0, lastAt)
    const version = spec.slice(lastAt + 1)
    acc[name] = acc[name] || {}
    acc[name][version] = fixture
    if (!acc[name][version]['package.json']) {
      acc[name][version]['package.json'] = { name, version }
    } else {
      acc[name][version]['package.json'].name = name
      acc[name][version]['package.json'].version = version
    }
    return acc
  }, {})

  const { prefixDir, globalPrefixDir, otherDirs, config, ...rest } = opts
  const { npm, ...res } = await loadMockNpm(t, {
    command: 'diff',
    prefixDir: jsonifyTestdir(prefixDir),
    otherDirs: jsonifyTestdir({ tarballs: tarballFixtures, ...otherDirs }),
    globalPrefixDir: jsonifyTestdir(globalPrefixDir),
    config: {
      ...config,
      diff: [].concat(diff),
    },
    ...rest,
  })

  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    strict: true,
    debug: true,
  })

  const manifests = Object.entries(tarballFixtures).reduce((acc, [name, versions]) => {
    acc[name] = registry.manifest({
      name,
      packuments: Object.keys(versions).map((version) => ({ version })),
    })
    return acc
  }, {})

  for (const [name, manifest] of Object.entries(manifests)) {
    await registry.package({ manifest, times: times[name] ?? 1 })
    for (const [version, tarballManifest] of Object.entries(manifest.versions)) {
      await registry.tarball({
        manifest: tarballManifest,
        tarball: join(res.other, 'tarballs', name, version),
      })
    }
  }

  if (exec) {
    await res.diff.exec(exec)
    res.output = res.joinedOutput()
  }

  return { npm, registry, ...res }
}

// a more specific helper to call diff against a local package and a registry package
// and assert the diff output contains the matching strings
const assertFoo = async (t, arg) => {
  let diff = []
  let exec = []

  if (typeof arg === 'string' || Array.isArray(arg)) {
    diff = arg
  } else if (arg && typeof arg === 'object') {
    diff = arg.diff
    exec = arg.exec
  }

  const { output } = await mockDiff(t, {
    diff,
    prefixDir: {
      'package.json': { name: '@npmcli/foo', version: '1.0.0' },
      'index.js': 'const version = "1.0.0"',
      'a.js': 'const a = "a@1.0.0"',
      'b.js': 'const b = "b@1.0.0"',
    },
    tarballs: {
      '@npmcli/foo@0.1.0': {
        'index.js': 'const version = "0.1.0"',
        'a.js': 'const a = "a@0.1.0"',
        'b.js': 'const b = "b@0.1.0"',
      },
    },
    exec,
  })

  const hasFile = (f) => !exec.length || exec.some(e => e.endsWith(f))

  if (hasFile('package.json')) {
    t.match(output, /-\s*"version": "0\.1\.0"/)
    t.match(output, /\+\s*"version": "1\.0\.0"/)
  }

  if (hasFile('index.js')) {
    t.match(output, /-\s*const version = "0\.1\.0"/)
    t.match(output, /\+\s*const version = "1\.0\.0"/)
  }

  if (hasFile('a.js')) {
    t.match(output, /-\s*const a = "a@0\.1\.0"/)
    t.match(output, /\+\s*const a = "a@1\.0\.0"/)
  }

  if (hasFile('b.js')) {
    t.match(output, /-\s*const b = "b@0\.1\.0"/)
    t.match(output, /\+\s*const b = "b@1\.0\.0"/)
  }

  return output
}

const rejectDiff = async (t, msg, opts) => {
  const { npm } = await mockDiff(t, opts)
  await t.rejects(npm.exec('diff', []), msg)
}

t.test('no args', async t => {
  t.test('in a project dir', async t => {
    const output = await assertFoo(t)
    t.matchSnapshot(output)
  })

  t.test('no args, missing package.json name in cwd', async t => {
    await rejectDiff(t, /Needs multiple arguments to compare or run from a project dir./)
  })

  t.test('no args, bad package.json in cwd', async t => {
    await rejectDiff(t, /Needs multiple arguments to compare or run from a project dir./, {
      prefixDir: { 'package.json': '{invalid"json' },
    })
  })
})

t.test('single arg', async t => {
  t.test('spec using cwd package name', async t => {
    await assertFoo(t, '@npmcli/foo@0.1.0')
  })

  t.test('unknown spec, no package.json', async t => {
    await rejectDiff(t, /Needs multiple arguments to compare or run from a project dir./, {
      diff: ['@npmcli/foo@1.0.0'],
    })
  })

  t.test('spec using semver range', async t => {
    await assertFoo(t, '@npmcli/foo@~0.1.0')
  })

  t.test('version', async t => {
    await assertFoo(t, '0.1.0')
  })

  t.test('version, no package.json', async t => {
    await rejectDiff(t, /Needs multiple arguments to compare or run from a project dir./, {
      diff: ['0.1.0'],
    })
  })

  t.test('version, filtering by files', async t => {
    const output = await assertFoo(t, { diff: '0.1.0', exec: ['./a.js', './b.js'] })
    t.matchSnapshot(output)
  })

  t.test('spec is not a dep', async t => {
    const { output } = await mockDiff(t, {
      diff: 'bar@1.0.0',
      prefixDir: {
        node_modules: {},
        'package.json': { name: 'my-project', version: '1.0.0' },
      },
      tarballs: {
        'bar@1.0.0': {},
      },
      exec: [],
    })

    t.match(output, /-\s*"name": "bar"/)
    t.match(output, /\+\s*"name": "my-project"/)
  })

  t.test('unknown package name', async t => {
    const { npm, registry } = await mockDiff(t, {
      diff: 'bar',
      prefixDir: {
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
    })
    registry.getPackage('bar', { times: 2, code: 404 })
    t.rejects(npm.exec('diff', []), /404 Not Found.*bar/)
  })

  t.test('unknown package name, no package.json', async t => {
    const { npm } = await mockDiff(t, {
      diff: 'bar',
    })
    t.rejects(npm.exec('diff', []),
      /Needs multiple arguments to compare or run from a project dir./)
  })

  t.test('transform single direct dep name into spec comparison', async t => {
    const { output } = await mockDiff(t, {
      diff: 'bar',
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar@1.8.0': {},
      },
      times: { bar: 2 },
      exec: [],
    })

    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "1\.8\.0"/)
  })

  t.test('global space, transform single direct dep name', async t => {
    const { output } = await mockDiff(t, {
      diff: 'lorem',
      config: {
        global: true,
      },
      globalPrefixDir: {
        node_modules: {
          lorem: {
            'package.json': {
              name: 'lorem',
              version: '2.0.0',
            },
          },
        },
      },
      prefixDir: {
        node_modules: {
          lorem: {
            'package.json': {
              name: 'lorem',
              version: '3.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            lorem: '^3.0.0',
          },
        },
      },
      tarballs: {
        'lorem@1.0.0': {},
      },
      times: {
        lorem: 2,
      },
      exec: [],
    })

    t.match(output, 'lorem')
    t.match(output, /-\s*"version": "2\.0\.0"/)
    t.match(output, /\+\s*"version": "1\.0\.0"/)
  })

  t.test('transform single spec into spec comparison', async t => {
    const { output } = await mockDiff(t, {
      diff: 'bar@2.0.0',
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar@2.0.0': {},
      },
      times: {
        lorem: 2,
      },
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('transform single spec from transitive deps', async t => {
    const { output } = await mockDiff(t, {
      diff: 'lorem',
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
              dependencies: {
                lorem: '^2.0.0',
              },
            },
          },
          lorem: {
            'package.json': {
              name: 'lorem',
              version: '2.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'lorem@2.2.2': {},
      },
      times: {
        lorem: 2,
      },
      exec: [],
    })

    t.match(output, 'lorem')
    t.match(output, /-\s*"version": "2\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.2\.2"/)
  })

  t.test('missing actual tree', async t => {
    const { output } = await mockDiff(t, {
      diff: 'lorem',
      prefixDir: {
        'package.json': {
          name: 'lorem',
          version: '2.0.0',
        },
      },
      mocks: {
        '@npmcli/arborist': class {
          constructor () {
            throw new Error('ERR')
          }
        },
      },
      tarballs: {
        'lorem@2.2.2': {},
      },
      exec: [],
    })

    t.match(output, 'lorem')
    t.match(output, /-\s*"version": "2\.2\.2"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('unknown package name', async t => {
    const { output } = await mockDiff(t, {
      diff: 'bar',
      prefixDir: {
        'package.json': { version: '1.0.0' },
      },

      tarballs: {
        'bar@2.2.2': {},
      },
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "2\.2\.2"/)
    t.match(output, /\+\s*"version": "1\.0\.0"/)
  })

  t.test('use project name in project dir', async t => {
    const { output } = await mockDiff(t, {
      diff: '@npmcli/foo',
      prefixDir: {
        'package.json': { name: '@npmcli/foo', version: '1.0.0' },
      },
      tarballs: {
        '@npmcli/foo@2.2.2': {},
      },
      exec: [],
    })

    t.match(output, '@npmcli/foo')
    t.match(output, /-\s*"version": "2\.2\.2"/)
    t.match(output, /\+\s*"version": "1\.0\.0"/)
  })

  t.test('dir spec type', async t => {
    const { output } = await mockDiff(t, {
      diff: '../other/other-pkg',
      prefixDir: {
        'package.json': { name: '@npmcli/foo', version: '1.0.0' },
      },
      otherDirs: {
        'other-pkg': {
          'package.json': { name: '@npmcli/foo', version: '2.0.0' },
        },
      },
      exec: [],
    })

    t.match(output, '@npmcli/foo')
    t.match(output, /-\s*"version": "2\.0\.0"/)
    t.match(output, /\+\s*"version": "1\.0\.0"/)
  })

  t.test('unsupported spec type', async t => {
    const p = mockDiff(t, {
      diff: 'git+https://github.com/user/foo',
      exec: [],
    })

    await t.rejects(
      p,
      /Spec type git not supported./,
      'should throw spec type not supported error.'
    )
  })
})

t.test('first arg is a qualified spec', async t => {
  t.test('second arg is ALSO a qualified spec', async t => {
    const { output } = await mockDiff(t, {
      diff: ['bar@1.0.0', 'bar@^2.0.0'],
      tarballs: {
        'bar@1.0.0': {},
        'bar@2.2.2': {},
      },
      times: {
        bar: 2,
      },
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.2\.2"/)
  })

  t.test('second arg is a known dependency name', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar@2.0.0': {},
      },
      diff: ['bar@2.0.0', 'bar'],
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "2\.0\.0"/)
    t.match(output, /\+\s*"version": "1\.0\.0"/)
  })

  t.test('second arg is a valid semver version', async t => {
    const { output } = await mockDiff(t, {
      tarballs: {
        'bar@1.0.0': {},
        'bar@2.0.0': {},
      },
      times: {
        bar: 2,
      },
      diff: ['bar@1.0.0', '2.0.0'],
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is an unknown dependency name', async t => {
    const { output } = await mockDiff(t, {
      tarballs: {
        'bar@1.0.0': {},
        'bar-fork@2.0.0': {},
      },
      diff: ['bar@1.0.0', 'bar-fork'],
      exec: [],
    })

    t.match(output, /-\s*"name": "bar"/)
    t.match(output, /\+\s*"name": "bar-fork"/)

    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })
})

t.test('first arg is a known dependency name', async t => {
  t.test('second arg is a qualified spec', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar@2.0.0': {},
      },
      diff: ['bar', 'bar@2.0.0'],
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is ALSO a known dependency', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
            },
          },
          'bar-fork': {
            'package.json': {
              name: 'bar-fork',
              version: '1.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      diff: ['bar', 'bar-fork'],
      exec: [],
    })

    t.match(output, /-\s*"name": "bar"/)
    t.match(output, /\+\s*"name": "bar-fork"/)
  })

  t.test('second arg is a valid semver version', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar@2.0.0': {},
      },
      diff: ['bar', '2.0.0'],
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is an unknown dependency name', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '1.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar-fork@1.0.0': {},
      },
      diff: ['bar', 'bar-fork'],
      exec: [],
    })

    t.match(output, /-\s*"name": "bar"/)
    t.match(output, /\+\s*"name": "bar-fork"/)
  })
})

t.test('first arg is a valid semver range', async t => {
  t.test('second arg is a qualified spec', async t => {
    const { output } = await mockDiff(t, {
      tarballs: {
        'bar@1.0.0': {},
        'bar@2.0.0': {},
      },
      diff: ['1.0.0', 'bar@2.0.0'],
      times: { bar: 2 },
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is a known dependency', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '2.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar@1.0.0': {},
      },
      diff: ['1.0.0', 'bar'],
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is ALSO a semver version', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        'package.json': {
          name: 'bar',
        },
      },
      tarballs: {
        'bar@1.0.0': {},
        'bar@2.0.0': {},
      },
      diff: ['1.0.0', '2.0.0'],
      times: { bar: 2 },
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is ALSO a semver version BUT cwd not a project dir', async t => {
    const p = mockDiff(t, {
      diff: ['1.0.0', '2.0.0'],
      exec: [],
    })
    await t.rejects(
      p,
      /Needs to be run from a project dir in order to diff two versions./,
      'should throw two versions need project dir error usage msg'
    )
  })

  t.test('second arg is an unknown dependency name', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        prefixDir: {
          'package.json': {
            name: 'bar',
          },
        },
      },
      tarballs: {
        'bar@1.0.0': {},
        'bar@2.0.0': {},
      },
      diff: ['1.0.0', 'bar'],
      times: { bar: 2 },
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is a qualified spec, missing actual tree', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        'package.json': {
          name: 'lorem',
          version: '2.0.0',
        },
      },
      mocks: {
        '@npmcli/arborist': class {
          constructor () {
            throw new Error('ERR')
          }
        },
      },
      tarballs: {
        'lorem@1.0.0': {},
        'lorem@2.0.0': {},
      },
      times: { lorem: 2 },
      diff: ['1.0.0', 'lorem@2.0.0'],
      exec: [],
    })

    t.match(output, 'lorem')
    t.match(output, /-\s*"version": "1\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })
})

t.test('first arg is an unknown dependency name', async t => {
  t.test('second arg is a qualified spec', async t => {
    const { output } = await mockDiff(t, {
      tarballs: {
        'bar@2.0.0': {},
        'bar@3.0.0': {},
      },
      times: { bar: 2 },
      diff: ['bar', 'bar@2.0.0'],
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "3\.0\.0"/)
    t.match(output, /\+\s*"version": "2\.0\.0"/)
  })

  t.test('second arg is a known dependency', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        node_modules: {
          bar: {
            'package.json': {
              name: 'bar',
              version: '2.0.0',
            },
          },
        },
        'package.json': {
          name: 'my-project',
          dependencies: {
            bar: '^1.0.0',
          },
        },
      },
      tarballs: {
        'bar-fork@2.0.0': {},
      },
      diff: ['bar-fork', 'bar'],
      exec: [],
    })

    t.match(output, /-\s*"name": "bar-fork"/)
    t.match(output, /\+\s*"name": "bar"/)
  })

  t.test('second arg is a valid semver version', async t => {
    const { output } = await mockDiff(t, {
      tarballs: {
        'bar@1.5.0': {},
        'bar@2.0.0': {},
      },
      times: { bar: 2 },
      diff: ['bar', '^1.0.0'],
      exec: [],
    })

    t.match(output, 'bar')
    t.match(output, /-\s*"version": "2\.0\.0"/)
    t.match(output, /\+\s*"version": "1\.5\.0"/)
  })

  t.test('second arg is ALSO an unknown dependency name', async t => {
    const { output } = await mockDiff(t, {
      prefixDir: {
        'package.json': {
          name: 'my-project',
        },
      },
      tarballs: {
        'bar@1.0.0': {},
        'bar-fork@1.0.0': {},
      },
      diff: ['bar', 'bar-fork'],
      exec: [],
    })

    t.match(output, /-\s*"name": "bar"/)
    t.match(output, /\+\s*"name": "bar-fork"/)
  })

  t.test('cwd not a project dir', async t => {
    const { output } = await mockDiff(t, {
      tarballs: {
        'bar@1.0.0': {},
        'bar-fork@1.0.0': {},
      },
      diff: ['bar', 'bar-fork'],
      exec: [],
    })

    t.match(output, /-\s*"name": "bar"/)
    t.match(output, /\+\s*"name": "bar-fork"/)
  })
})

t.test('various options', async t => {
  const mockOptions = async (t, config) => {
    const file = (v) => new Array(50).fill(0).map((_, i) => `${i}${i === 20 ? v : ''}`).join('\n')
    const mock = await mockDiff(t, {
      diff: ['bar@2.0.0', 'bar@3.0.0'],
      config,
      exec: [],
      tarballs: {
        'bar@2.0.0': { 'index.js': file('2.0.0') },
        'bar@3.0.0': { 'index.js': file('3.0.0') },
      },
      times: { bar: 2 },
    })

    return mock
  }

  t.test('using --name-only option', async t => {
    const { output } = await mockOptions(t, {
      'diff-name-only': true,
    })
    t.matchSnapshot(output)
  })

  t.test('using diff option', async t => {
    const { output } = await mockOptions(t, {
      'diff-context': 5,
      'diff-ignore-whitespace': true,
      'diff-no-prefix': false,
      'diff-drc-prefix': 'foo/',
      'diff-fst-prefix': 'bar/',
      'diff-text': true,

    })
    t.matchSnapshot(output)
  })
})

t.test('too many args', async t => {
  const { npm } = await mockDiff(t, {
    diff: ['a', 'b', 'c'],
  })

  await t.rejects(
    npm.exec('diff', []),
    /Can't use more than two --diff arguments./,
    'should throw usage error'
  )
})

t.test('workspaces', async t => {
  const mockWorkspaces = (t, workspaces = true, opts) => mockDiff(t, {
    prefixDir: {
      'package.json': {
        name: 'workspaces-test',
        version: '1.2.3',
        workspaces: ['workspace-a', 'workspace-b', 'workspace-c'],
      },
      'workspace-a': {
        'package.json': {
          name: 'workspace-a',
          version: '1.2.3-a',
        },
      },
      'workspace-b': {
        'package.json': {
          name: 'workspace-b',
          version: '1.2.3-b',
        },
      },
      'workspace-c': {
        'package.json': {
          name: 'workspace-c',
          version: '1.2.3-c',
        },
      },
    },
    exec: [],
    config: workspaces === true ? { workspaces } : { workspace: workspaces },
    ...opts,
  })

  t.test('all workspaces', async t => {
    const { output } = await mockWorkspaces(t, true, {
      tarballs: {
        'workspace-a@2.0.0-a': {},
        'workspace-b@2.0.0-b': {},
        'workspace-c@2.0.0-c': {},
      },
    })

    t.match(output, '"name": "workspace-a"')
    t.match(output, /-\s*"version": "2\.0\.0-a"/)
    t.match(output, /\+\s*"version": "1\.2\.3-a"/)

    t.match(output, '"name": "workspace-b"')
    t.match(output, /-\s*"version": "2\.0\.0-b"/)
    t.match(output, /\+\s*"version": "1\.2\.3-b"/)

    t.match(output, '"name": "workspace-c"')
    t.match(output, /-\s*"version": "2\.0\.0-c"/)
    t.match(output, /\+\s*"version": "1\.2\.3-c"/)
  })

  t.test('one workspace', async t => {
    const { output } = await mockWorkspaces(t, 'workspace-a', {
      tarballs: {
        'workspace-a@2.0.0-a': {},
      },
    })

    t.match(output, '"name": "workspace-a"')
    t.match(output, /-\s*"version": "2\.0\.0-a"/)
    t.match(output, /\+\s*"version": "1\.2\.3-a"/)

    t.notMatch(output, '"name": "workspace-b"')
    t.notMatch(output, '"name": "workspace-c"')
  })

  t.test('invalid workspace', async t => {
    const p = mockWorkspaces(t, 'workspace-x')
    await t.rejects(p, /No workspaces found/)
    await t.rejects(p, /workspace-x/)
  })
})
