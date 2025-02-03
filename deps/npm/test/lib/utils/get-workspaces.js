const { resolve } = require('node:path')
const t = require('tap')
const getWorkspaces = require('../../../lib/utils/get-workspaces.js')

const normalizePath = p => p
  .replace(/\\+/g, '/')
  .replace(/\r\n/g, '\n')

const cleanOutput = (str, path) => normalizePath(str)
  .replace(normalizePath(path), '{PATH}')

const clean = (res, path) => {
  const cleaned = new Map()
  for (const [key, value] of res.entries()) {
    cleaned.set(key, cleanOutput(value, path))
  }
  return cleaned
}

const path = t.testdir({
  packages: {
    a: {
      'package.json': JSON.stringify({
        name: 'a',
        version: '1.0.0',
        scripts: { glorp: 'echo a doing the glerp glop' },
      }),
    },
    b: {
      'package.json': JSON.stringify({
        name: 'b',
        version: '2.0.0',
        scripts: { glorp: 'echo b doing the glerp glop' },
      }),
    },
    c: {
      'package.json': JSON.stringify({
        name: 'c',
        version: '1.0.0',
        scripts: {
          test: 'exit 0',
          posttest: 'echo posttest',
          lorem: 'echo c lorem',
        },
      }),
    },
    d: {
      'package.json': JSON.stringify({
        name: 'd',
        version: '1.0.0',
        scripts: {
          test: 'exit 0',
          posttest: 'echo posttest',
        },
      }),
    },
    e: {
      'package.json': JSON.stringify({
        name: 'e',
        scripts: { test: 'exit 0', start: 'echo start something' },
      }),
    },
    noscripts: {
      'package.json': JSON.stringify({
        name: 'noscripts',
        version: '1.0.0',
      }),
    },
  },
  'package.json': JSON.stringify({
    name: 'x',
    version: '1.2.3',
    workspaces: ['packages/*'],
  }),
})

let workspaces

t.test('filter by package name', async t => {
  workspaces = await getWorkspaces(['a', 'b'], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      a: '{PATH}/packages/a',
      b: '{PATH}/packages/b',
    })),
    'should filter by package name'
  )
})

t.test('include workspace root', async t => {
  workspaces = await getWorkspaces(['a', 'b'],
    { path, includeWorkspaceRoot: true, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      x: '{PATH}',
      a: '{PATH}/packages/a',
      b: '{PATH}/packages/b',
    })),
    'include rootspace root'
  )
})

t.test('filter by package directory', async t => {
  workspaces = await getWorkspaces(['./packages/c'], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      c: '{PATH}/packages/c',
    })),
    'should filter by package directory'
  )
})

t.test('filter by rel package directory', async t => {
  workspaces = await getWorkspaces(['packages/c'], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      c: '{PATH}/packages/c',
    })),
    'should filter by rel package directory'
  )
})

t.test('filter by absolute package directory', async t => {
  workspaces = await getWorkspaces([resolve(path, 'packages/c')], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      c: '{PATH}/packages/c',
    })),
    'should filter by absolute package directory'
  )
})

t.test('filter by parent directory name', async t => {
  workspaces = await getWorkspaces(['packages'], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      a: '{PATH}/packages/a',
      b: '{PATH}/packages/b',
      c: '{PATH}/packages/c',
      d: '{PATH}/packages/d',
      e: '{PATH}/packages/e',
      noscripts: '{PATH}/packages/noscripts',
    })),
    'should filter by parent directory name'
  )
})

t.test('filter by parent directory path', async t => {
  workspaces = await getWorkspaces(['./packages/'], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      a: '{PATH}/packages/a',
      b: '{PATH}/packages/b',
      c: '{PATH}/packages/c',
      d: '{PATH}/packages/d',
      e: '{PATH}/packages/e',
      noscripts: '{PATH}/packages/noscripts',
    })),
    'should filter by parent directory path'
  )
})

t.test('filter by absolute parent directory path', async t => {
  workspaces = await getWorkspaces([resolve(path, './packages')], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      a: '{PATH}/packages/a',
      b: '{PATH}/packages/b',
      c: '{PATH}/packages/c',
      d: '{PATH}/packages/d',
      e: '{PATH}/packages/e',
      noscripts: '{PATH}/packages/noscripts',
    })),
    'should filter by absolute parent directory path'
  )
})

t.test('no filter set', async t => {
  workspaces = await getWorkspaces([], { path, relativeFrom: path })
  t.same(
    clean(workspaces, path),
    new Map(Object.entries({
      a: '{PATH}/packages/a',
      b: '{PATH}/packages/b',
      c: '{PATH}/packages/c',
      d: '{PATH}/packages/d',
      e: '{PATH}/packages/e',
      noscripts: '{PATH}/packages/noscripts',
    })),
    'should return all workspaces if no filter set'
  )
})

t.test('missing workspace', async t => {
  await t.rejects(
    getWorkspaces(['missing'], { path, relativeFrom: path }),
    /No workspaces found/,
    'should throw no workspaces found error'
  )
})

t.test('no workspaces configured', async t => {
  const unconfiguredWorkspaces = t.testdir({
    'package.json': JSON.stringify({
      name: 'no-configured-workspaces',
      version: '1.0.0',
    }),
  })
  await t.rejects(
    getWorkspaces([], { path: unconfiguredWorkspaces, relativeFrom: path }),
    /No workspaces found/,
    'should throw no workspaces found error'
  )
})
