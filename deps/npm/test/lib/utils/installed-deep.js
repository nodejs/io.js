const t = require('tap')
const installedDeep = require('../../../lib/utils/installed-deep.js')
const mockNpm = require('../../fixtures/mock-npm')

const fixture = {
  'package.json': JSON.stringify({
    name: 'test-installed-deep',
    version: '1.0.0',
    dependencies: {
      a: '^1.0.0',
      b: '^1.0.0',
      c: '^1.0.0',
    },
    devDependencies: {
      d: '^1.0.0',
    },
    peerDependencies: {
      e: '^1.0.0',
    },
  }),
  node_modules: {
    a: {
      'package.json': JSON.stringify({
        name: 'a',
        version: '1.0.0',
        dependencies: {
          f: '^1.0.0',
        },
      }),
    },
    b: {
      'package.json': JSON.stringify({
        name: 'b',
        version: '1.0.0',
      }),
    },
    c: {
      'package.json': JSON.stringify({
        name: 'c',
        version: '1.0.0',
        dependencies: {
          ch: '1.0.0',
        },
      }),
    },
    ch: {
      'package.json': JSON.stringify({
        name: 'ch',
        version: '1.0.0',
      }),
    },
    d: {
      'package.json': JSON.stringify({
        name: 'd',
        version: '1.0.0',
      }),
    },
    e: {
      'package.json': JSON.stringify({
        name: 'e',
        version: '1.0.0',
      }),
    },
    f: {
      'package.json': JSON.stringify({
        name: 'f',
        version: '1.0.0',
        dependencies: {
          g: '^1.0.0',
          e: '^2.0.0',
        },
      }),
      node_modules: {
        e: {
          'package.json': JSON.stringify({
            name: 'e',
            version: '2.0.0',
            dependencies: {
              bb: '^1.0.0',
            },
          }),
          node_modules: {
            bb: {
              'package.json': JSON.stringify({
                name: 'bb',
                version: '1.0.0',
              }),
            },
          },
        },
      },
    },
    g: {
      'package.json': JSON.stringify({
        name: 'g',
        version: '1.0.0',
      }),
    },
  },
}

const globalFixture = {
  node_modules: {
    foo: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.0.0',
      }),
    },
    bar: {
      'package.json': JSON.stringify({
        name: 'bar',
        version: '1.0.0',
        dependencies: {
          'a-bar': '^1.0.0',
        },
      }),
      node_modules: {
        'a-bar': {
          'package.json': JSON.stringify({
            name: 'a-bar',
            version: '1.0.0',
          }),
        },
      },
    },
  },
}

const mockDeep = async (t, config) => {
  const mock = await mockNpm(t, {
    prefixDir: fixture,
    globalPrefixDir: globalFixture,
    config: {
      depth: Infinity,
      ...config,
    },
  })

  const res = await installedDeep(mock.npm)

  return res
}

t.test('get list of package names', async t => {
  const res = await mockDeep(t)
  t.same(
    res,
    [
      ['bar', '-g'],
      ['foo', '-g'],
      ['a-bar', '-g'],
      'a', 'b', 'c',
      'ch', 'd', 'e',
      'f', 'g', 'bb',
    ],
    'should return list of package names and global flag'
  )
  t.end()
})

t.test('get list of package names as global', async t => {
  const res = await mockDeep(t, { global: true })
  t.same(
    res,
    [
      'bar',
      'foo',
      'a-bar',
    ],
    'should return list of global packages with no extra flags'
  )
})

t.test('limit depth', async t => {
  const res = await mockDeep(t, { depth: 0 })
  t.same(
    res,
    [
      ['bar', '-g'],
      ['foo', '-g'],
      // XXX https://github.com/npm/statusboard/issues/380
      ['a-bar', '-g'],
      'a', 'b',
      'c', 'ch',
      'd', 'e',
      'f', 'g',
    ],
    'should print only packages up to the specified depth'
  )
})

t.test('limit depth as global', async t => {
  const res = await mockDeep(t, { depth: 0, global: true })
  t.same(
    res,
    [
      'bar',
      'foo',
      // https://github.com/npm/statusboard/issues/380
      'a-bar',
    ],
    'should reorder so that packages above that level depth goes last'
  )
})
