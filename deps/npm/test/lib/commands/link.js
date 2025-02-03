const t = require('tap')
const { resolve, join } = require('node:path')
const fs = require('node:fs')
const Arborist = require('@npmcli/arborist')
const { cleanCwd } = require('../../fixtures/clean-snapshot.js')
const mockNpm = require('../../fixtures/mock-npm')

t.cleanSnapshot = (str) => cleanCwd(str)

const mockLink = async (t, { globalPrefixDir, ...opts } = {}) => {
  const mock = await mockNpm(t, {
    ...opts,
    command: 'link',
    globalPrefixDir,
    mocks: {
      ...opts.mocks,
      '{LIB}/utils/reify-output.js': async () => {},
    },
  })

  const printLinks = async ({ global = false } = {}) => {
    let res = ''
    const arb = new Arborist(global ? {
      path: resolve(mock.npm.globalDir, '..'),
      global: true,
    } : { path: mock.prefix })
    const tree = await arb.loadActual()
    const linkedItems = [...tree.inventory.values()]
      .sort((a, b) => a.pkgid.localeCompare(b.pkgid, 'en'))
    for (const item of linkedItems) {
      if (item.isLink) {
        res += `${item.path} -> ${item.target.path}\n`
      }
    }
    return res
  }

  return {
    ...mock,
    printLinks,
  }
}

t.test('link to globalDir when in current working dir of pkg and no args', async t => {
  const { link, printLinks } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
      },
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'test-pkg-link',
        version: '1.0.0',
      }),
    },
  })

  await link.exec()
  t.matchSnapshot(await printLinks({ global: true }), 'should create a global link to current pkg')
})

t.test('link ws to globalDir when workspace specified and no args', async t => {
  const { link, printLinks } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
      },
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'test-pkg-link',
        version: '1.0.0',
        workspaces: ['packages/*'],
      }),
      packages: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
      },
    },
    config: { workspace: 'a' },
  })

  await link.exec()
  t.matchSnapshot(await printLinks({ global: true }), 'should create a global link to current pkg')
})

t.test('link global linked pkg to local nm when using args', async t => {
  const { link, printLinks } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        '@myscope': {
          foo: {
            'package.json': JSON.stringify({
              name: '@myscope/foo',
              version: '1.0.0',
            }),
          },
          bar: {
            'package.json': JSON.stringify({
              name: '@myscope/bar',
              version: '1.0.0',
            }),
          },
          linked: t.fixture('symlink', '../../../other/scoped-linked'),
        },
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
        b: {
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
          }),
        },
        'test-pkg-link': t.fixture('symlink', '../../other/test-pkg-link'),
      },
    },
    otherDirs: {
      'test-pkg-link': {
        'package.json': JSON.stringify({
          name: 'test-pkg-link',
          version: '1.0.0',
        }),
      },
      'link-me-too': {
        'package.json': JSON.stringify({
          name: 'link-me-too',
          version: '1.0.0',
        }),
      },
      'scoped-linked': {
        'package.json': JSON.stringify({
          name: '@myscope/linked',
          version: '1.0.0',
        }),
      },
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'my-project',
        version: '1.0.0',
        dependencies: {
          foo: '^1.0.0',
        },
      }),
      node_modules: {
        foo: {
          'package.json': JSON.stringify({
            name: 'foo',
            version: '1.0.0',
          }),
        },
      },
    },
  })

  // installs examples for:
  // - test-pkg-link: pkg linked to globalDir from local fs
  // - @myscope/linked: scoped pkg linked to globalDir from local fs
  // - @myscope/bar: prev installed scoped package available in globalDir
  // - a: prev installed package available in globalDir
  // - file:./link-me-too: pkg that needs to be reified in globalDir first
  await link.exec([
    'test-pkg-link',
    '@myscope/linked',
    '@myscope/bar',
    'a',
    'file:../other/link-me-too',
  ])

  t.matchSnapshot(await printLinks(), 'should create a local symlink to global pkg')
})

t.test('link global linked pkg to local workspace using args', async t => {
  const { link, printLinks } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        '@myscope': {
          foo: {
            'package.json': JSON.stringify({
              name: '@myscope/foo',
              version: '1.0.0',
            }),
          },
          bar: {
            'package.json': JSON.stringify({
              name: '@myscope/bar',
              version: '1.0.0',
            }),
          },
          linked: t.fixture('symlink', '../../../other/scoped-linked'),
        },
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
        b: {
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.0.0',
          }),
        },
        'test-pkg-link': t.fixture('symlink', '../../other/test-pkg-link'),
      },
    },
    otherDirs: {
      'test-pkg-link': {
        'package.json': JSON.stringify({
          name: 'test-pkg-link',
          version: '1.0.0',
        }),
      },
      'link-me-too': {
        'package.json': JSON.stringify({
          name: 'link-me-too',
          version: '1.0.0',
        }),
      },
      'scoped-linked': {
        'package.json': JSON.stringify({
          name: '@myscope/linked',
          version: '1.0.0',
        }),
      },
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'my-project',
        version: '1.0.0',
        workspaces: ['packages/*'],
      }),
      packages: {
        x: {
          'package.json': JSON.stringify({
            name: 'x',
            version: '1.0.0',
            dependencies: {
              foo: '^1.0.0',
            },
          }),
        },
      },
      node_modules: {
        foo: {
          'package.json': JSON.stringify({
            name: 'foo',
            version: '1.0.0',
          }),
        },
      },
    },
    config: { workspace: 'x' },
  })

  // installs examples for:
  // - test-pkg-link: pkg linked to globalDir from local fs
  // - @myscope/linked: scoped pkg linked to globalDir from local fs
  // - @myscope/bar: prev installed scoped package available in globalDir
  // - a: prev installed package available in globalDir
  // - file:./link-me-too: pkg that needs to be reified in globalDir first
  await link.exec([
    'test-pkg-link',
    '@myscope/linked',
    '@myscope/bar',
    'a',
    'file:../other/link-me-too',
  ])

  t.matchSnapshot(await printLinks(), 'should create a local symlink to global pkg')
})

t.test('link pkg already in global space', async t => {
  const { npm, link, printLinks, prefix } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        '@myscope': {
          linked: t.fixture('symlink', '../../../other/scoped-linked'),
        },
      },
    },
    otherDirs: {
      'scoped-linked': {
        'package.json': JSON.stringify({
          name: '@myscope/linked',
          version: '1.0.0',
        }),
      },
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'my-project',
        version: '1.0.0',
      }),
    },
  })

  // XXX: how to convert this to a config that gets passed in?
  npm.config.find = () => 'default'

  await link.exec(['@myscope/linked'])

  t.equal(
    require(resolve(prefix, 'package.json')).dependencies,
    undefined,
    'should not save to package.json upon linking'
  )

  t.matchSnapshot(await printLinks(), 'should create a local symlink to global pkg')
})

t.test('link pkg already in global space when prefix is a symlink', async t => {
  const { npm, link, printLinks, prefix } = await mockLink(t, {
    globalPrefixDir: t.fixture('symlink', './other/real-global-prefix'),
    otherDirs: {
      // mockNpm does this automatically but only for globalPrefixDir so here we
      // need to do it manually since we are making a symlink somewhere else
      'real-global-prefix': mockNpm.setGlobalNodeModules({
        node_modules: {
          '@myscope': {
            linked: t.fixture('symlink', '../../../scoped-linked'),
          },
        },
      }),
      'scoped-linked': {
        'package.json': JSON.stringify({
          name: '@myscope/linked',
          version: '1.0.0',
        }),
      },
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'my-project',
        version: '1.0.0',
      }),
    },
  })

  npm.config.find = () => 'default'

  await link.exec(['@myscope/linked'])

  t.equal(
    require(resolve(prefix, 'package.json')).dependencies,
    undefined,
    'should not save to package.json upon linking'
  )

  t.matchSnapshot(await printLinks(), 'should create a local symlink to global pkg')
})

t.test('should not save link to package file', async t => {
  const { link, prefix } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        '@myscope': {
          linked: t.fixture('symlink', '../../../other/scoped-linked'),
        },
      },
    },
    otherDirs: {
      'scoped-linked': {
        'package.json': JSON.stringify({
          name: '@myscope/linked',
          version: '1.0.0',
        }),
      },
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'my-project',
        version: '1.0.0',
      }),
    },
    config: { save: false },
  })

  await link.exec(['@myscope/linked'])
  t.match(
    require(resolve(prefix, 'package.json')).dependencies,
    undefined,
    'should not save to package.json upon linking'
  )
})

t.test('should not prune dependencies when linking packages', async t => {
  const { link, prefix } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        linked: t.fixture('symlink', '../../other/linked'),
      },
    },
    otherDirs: {
      linked: {
        'package.json': JSON.stringify({
          name: 'linked',
          version: '1.0.0',
        }),
      },
    },
    prefixDir: {
      node_modules: {
        foo: {
          'package.json': JSON.stringify({ name: 'foo', version: '1.0.0' }),
        },
      },
      'package.json': JSON.stringify({
        name: 'my-project',
        version: '1.0.0',
      }),
    },
  })

  await link.exec(['linked'])

  t.ok(
    fs.statSync(resolve(prefix, 'node_modules/foo')),
    'should not prune any extraneous dep when running npm link'
  )
})

t.test('completion', async t => {
  const { link } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        foo: {},
        bar: {},
        lorem: {},
        ipsum: {},
      },
    },
  })

  const words = await link.completion({})

  t.same(
    words,
    ['bar', 'foo', 'ipsum', 'lorem'],
    'should list all package names available in globalDir'
  )
})

t.test('--global option', async t => {
  const { link } = await mockLink(t, {
    config: { global: true },
  })
  await t.rejects(
    link.exec([]),
    /link should never be --global/,
    'should throw an useful error'
  )
})

t.test('hash character in working directory path', async t => {
  const { link, printLinks } = await mockLink(t, {
    globalPrefixDir: {
      node_modules: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
      },
    },
    otherDirs: {
      'i_like_#_in_my_paths': {
        'test-pkg-link': {
          'package.json': JSON.stringify({
            name: 'test-pkg-link',
            version: '1.0.0',
          }),
        },
      },
    },
    chdir: ({ other }) => join(other, 'i_like_#_in_my_paths', 'test-pkg-link'),
  })
  await link.exec([])

  t.matchSnapshot(await printLinks({ global: true }),
    'should create a global link to current pkg, even within path with hash')
})

t.test('test linked installed as symlinks', async t => {
  const { link, prefix, printLinks } = await mockLink(t, {
    otherDirs: {
      mylink: {
        'package.json': JSON.stringify({
          name: 'mylink',
          version: '1.0.0',
        }),
      },
    },
  })

  await link.exec([
    join('file:../other/mylink'),
  ])

  t.ok(fs.lstatSync(join(prefix, 'node_modules', 'mylink')).isSymbolicLink(),
    'linked path should by symbolic link'
  )

  t.matchSnapshot(await printLinks(), 'linked package should not be installed')
})
