const { resolve } = require('node:path')
const { readFileSync } = require('node:fs')
const t = require('tap')
const _mockNpm = require('../../fixtures/mock-npm')
const { cleanCwd } = require('../../fixtures/clean-snapshot')

t.cleanSnapshot = (str) => cleanCwd(str)

const mockNpm = async (t, { ...opts } = {}) => {
  const res = await _mockNpm(t, {
    ...opts,
    command: 'pkg',
  })

  const readPackageJson = (dir = '') =>
    JSON.parse(readFileSync(resolve(res.prefix, dir, 'package.json'), 'utf8'))

  return {
    ...res,
    pkg: (...args) => res.npm.exec('pkg', args),
    readPackageJson,
    OUTPUT: () => res.joinedOutput(),
  }
}

t.test('no args', async t => {
  const { pkg } = await mockNpm(t)

  await t.rejects(
    pkg(),
    { code: 'EUSAGE' },
    'should throw usage error'
  )
})

t.test('no global mode', async t => {
  const { pkg } = await mockNpm(t, {
    config: { global: true },
  })

  await t.rejects(
    pkg('get', 'foo'),
    { code: 'EPKGGLOBAL' },
    'should throw no global mode error'
  )
})

t.test('get no args', async t => {
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
      }),
    },
  })

  await pkg('get')

  t.strictSame(
    JSON.parse(OUTPUT()),
    {
      name: 'foo',
      version: '1.1.1',
    },
    'should print package.json content'
  )
})

t.test('get single arg', async t => {
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
      }),
    },
  })

  await pkg('get', 'version')

  t.strictSame(
    JSON.parse(OUTPUT()),
    '1.1.1',
    'should print retrieved package.json field'
  )
})

t.test('get multiple arg', async t => {
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
      }),
    },
  })

  await pkg('get', 'name', 'version')

  t.strictSame(
    JSON.parse(OUTPUT()),
    {
      name: 'foo',
      version: '1.1.1',
    },
    'should print retrieved package.json field'
  )
})

t.test('get multiple arg with empty value', async t => {
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        author: '',
      }),
    },
  })

  await pkg('get', 'name', 'author')

  t.strictSame(
    JSON.parse(OUTPUT()),
    {
      name: 'foo',
      author: '',
    },
    'should print retrieved package.json field regardless of empty value'
  )
})

t.test('get nested arg', async t => {
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
        scripts: {
          test: 'node test.js',
        },
      }),
    },
  })

  await pkg('get', 'scripts.test')

  t.strictSame(
    JSON.parse(OUTPUT()),
    'node test.js',
    'should print retrieved nested field'
  )
})

t.test('get array field', async t => {
  const files = [
    'index.js',
    'cli.js',
  ]
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
        files,
      }),
    },
  })

  await pkg('get', 'files')

  t.strictSame(
    JSON.parse(OUTPUT()),
    files,
    'should print retrieved array field'
  )
})

t.test('get array item', async t => {
  const files = [
    'index.js',
    'cli.js',
  ]
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
        files,
      }),
    },
  })

  await pkg('get', 'files[0]')

  t.strictSame(
    JSON.parse(OUTPUT()),
    'index.js',
    'should print retrieved array field'
  )
})

t.test('get array nested items notation', async t => {
  const contributors = [
    {
      name: 'Ruy',
      url: 'http://example.com/ruy',
    },
    {
      name: 'Gar',
      url: 'http://example.com/gar',
    },
  ]
  const { pkg, OUTPUT } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
        contributors,
      }),
    },
  })

  await pkg('get', 'contributors.name')
  t.strictSame(
    JSON.parse(OUTPUT()),
    {
      'contributors[0].name': 'Ruy',
      'contributors[1].name': 'Gar',
    },
    'should print json result containing matching results'
  )
})

t.test('set no args', async t => {
  const { pkg } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({ name: 'foo' }),
    },
  })
  await t.rejects(
    pkg('set'),
    { code: 'EUSAGE' },
    'should throw an error if no args'
  )
})

t.test('set missing value', async t => {
  const { pkg } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({ name: 'foo' }),
    },
  })
  await t.rejects(
    pkg('set', 'key='),
    { code: 'EUSAGE' },
    'should throw an error if missing value'
  )
})

t.test('set missing key', async t => {
  const { pkg } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({ name: 'foo' }),
    },
  })
  await t.rejects(
    pkg('set', '=value'),
    { code: 'EUSAGE' },
    'should throw an error if missing key'
  )
})

t.test('set single field', async t => {
  const json = {
    name: 'foo',
    version: '1.1.1',
  }
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify(json),
    },
  })

  await pkg('set', 'description=Awesome stuff')
  t.strictSame(
    readPackageJson(),
    {
      ...json,
      description: 'Awesome stuff',
    },
    'should add single field to package.json'
  )
})

t.test('push to array syntax', async t => {
  const json = {
    name: 'foo',
    version: '1.1.1',
    keywords: [
      'foo',
    ],
  }
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify(json),
    },
  })

  await pkg('set', 'keywords[]=bar', 'keywords[]=baz')
  t.strictSame(
    readPackageJson(),
    {
      ...json,
      keywords: [
        'foo',
        'bar',
        'baz',
      ],
    },
    'should append to arrays using empty bracket syntax'
  )
})

t.test('set multiple fields', async t => {
  const json = {
    name: 'foo',
    version: '1.1.1',
  }
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify(json),
    },
  })

  await pkg('set', 'bin.foo=foo.js', 'scripts.test=node test.js')
  t.strictSame(
    readPackageJson(),
    {
      ...json,
      bin: {
        foo: 'foo.js',
      },
      scripts: {
        test: 'node test.js',
      },
    },
    'should add single field to package.json'
  )
})

t.test('set = separate value', async t => {
  const json = {
    name: 'foo',
    version: '1.1.1',
  }
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify(json),
    },
  })

  await pkg('set', 'tap[test-env][0]=LC_ALL=sk')
  t.strictSame(
    readPackageJson(),
    {
      ...json,
      tap: {
        'test-env': [
          'LC_ALL=sk',
        ],
      },
    },
    'should add single field to package.json'
  )
})

t.test('set --json', async t => {
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.1.1',
      }),
    },
    config: { json: true },
  })

  await pkg('set', 'private=true')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
      version: '1.1.1',
      private: true,
    },
    'should add boolean field to package.json'
  )

  await pkg('set', 'tap.timeout=60')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
      version: '1.1.1',
      private: true,
      tap: {
        timeout: 60,
      },
    },
    'should add number field to package.json'
  )

  await pkg('set', 'foo={ "bar": { "baz": "BAZ" } }')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
      version: '1.1.1',
      private: true,
      tap: {
        timeout: 60,
      },
      foo: {
        bar: {
          baz: 'BAZ',
        },
      },
    },
    'should add object field to package.json'
  )

  await pkg('set', 'workspaces=["packages/*"]')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
      version: '1.1.1',
      private: true,
      workspaces: [
        'packages/*',
      ],
      tap: {
        timeout: 60,
      },
      foo: {
        bar: {
          baz: 'BAZ',
        },
      },
    },
    'should add object field to package.json'
  )

  await pkg('set', 'description="awesome"')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
      version: '1.1.1',
      description: 'awesome',
      private: true,
      workspaces: [
        'packages/*',
      ],
      tap: {
        timeout: 60,
      },
      foo: {
        bar: {
          baz: 'BAZ',
        },
      },
    },
    'should add object field to package.json'
  )
})

t.test('delete no args', async t => {
  const { pkg } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({ name: 'foo' }),
    },
  })
  await t.rejects(
    pkg('delete'),
    { code: 'EUSAGE' },
    'should throw an error if deleting no args'
  )
})

t.test('delete invalid key', async t => {
  const { pkg } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({ name: 'foo' }),
    },
  })
  await t.rejects(
    pkg('delete', ''),
    { code: 'EUSAGE' },
    'should throw an error if deleting invalid args'
  )
})

t.test('delete single field', async t => {
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.0.0',
      }),
    },
  })
  await pkg('delete', 'version')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
    },
    'should delete single field from package.json'
  )
})

t.test('delete multiple field', async t => {
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.0.0',
        description: 'awesome',
      }),
    },
  })
  await pkg('delete', 'version', 'description')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
    },
    'should delete multiple fields from package.json'
  )
})

t.test('delete nested field', async t => {
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.0.0',
        info: {
          foo: {
            bar: [
              {
                baz: 'deleteme',
              },
            ],
          },
        },
      }),
    },
  })
  await pkg('delete', 'info.foo.bar[0].baz')
  t.strictSame(
    readPackageJson(),
    {
      name: 'foo',
      version: '1.0.0',
      info: {
        foo: {
          bar: [
            {},
          ],
        },
      },
    },
    'should delete nested fields from package.json'
  )
})

t.test('workspaces', async t => {
  const mockWorkspaces = (t) => mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'root',
        version: '1.0.0',
        workspaces: [
          'packages/*',
        ],
      }),
      packages: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
        b: {
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.2.3',
          }),
        },
      },
    },
    config: { workspaces: true },
  })

  t.test('get', async t => {
    const { pkg, OUTPUT } = await mockWorkspaces(t)
    await pkg('get', 'name', 'version')
    t.strictSame(
      JSON.parse(OUTPUT()),
      {
        a: {
          name: 'a',
          version: '1.0.0',
        },
        b: {
          name: 'b',
          version: '1.2.3',
        },
      },
      'should return expected result for configured workspaces'
    )
  })

  t.test('set', async t => {
    const { pkg, readPackageJson } = await mockWorkspaces(t)

    await pkg('set', 'funding=http://example.com')

    t.strictSame(
      readPackageJson('packages/a'),
      {
        name: 'a',
        version: '1.0.0',
        funding: 'http://example.com',
      },
      'should add field to workspace a'
    )

    t.strictSame(
      readPackageJson('packages/b'),
      {
        name: 'b',
        version: '1.2.3',
        funding: 'http://example.com',
      },
      'should add field to workspace b'
    )

    await pkg('delete', 'version')

    t.strictSame(
      readPackageJson('packages/a'),
      {
        name: 'a',
        funding: 'http://example.com',
      },
      'should delete version field from workspace a'
    )

    t.strictSame(
      readPackageJson('packages/b'),
      {
        name: 'b',
        funding: 'http://example.com',
      },
      'should delete version field from workspace b'
    )
  })
})

t.test('single workspace', async t => {
  const mockWorkspace = (t) => mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'root',
        version: '1.0.0',
        workspaces: [
          'packages/*',
        ],
      }),
      packages: {
        a: {
          'package.json': JSON.stringify({
            name: 'a',
            version: '1.0.0',
          }),
        },
        b: {
          'package.json': JSON.stringify({
            name: 'b',
            version: '1.2.3',
          }),
        },
      },
    },
    config: { workspace: ['packages/a'] },
  })

  t.test('multiple args', async t => {
    const { pkg, OUTPUT } = await mockWorkspace(t)
    await pkg('get', 'name', 'version')

    t.strictSame(
      JSON.parse(OUTPUT()),
      { a: { name: 'a', version: '1.0.0' } },
      'should only return info for one workspace'
    )
  })

  t.test('single arg', async t => {
    const { pkg, OUTPUT } = await mockWorkspace(t)
    await pkg('get', 'version')

    t.strictSame(
      JSON.parse(OUTPUT()),
      { a: '1.0.0' },
      'should only return info for one workspace'
    )
  })
})

t.test('fix', async t => {
  const { pkg, readPackageJson } = await mockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'foo ',
        version: 'v1.1.1',
      }),
    },
  })

  await pkg('fix')
  t.strictSame(
    readPackageJson(),
    { name: 'foo', version: '1.1.1' },
    'fixes package.json issues'
  )
})
