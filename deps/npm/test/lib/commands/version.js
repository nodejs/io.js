const { readFileSync, statSync } = require('node:fs')
const { resolve } = require('node:path')
const t = require('tap')
const _mockNpm = require('../../fixtures/mock-npm')
const mockGlobals = require('@npmcli/mock-globals')

const mockNpm = async (t, opts = {}) => {
  const res = await _mockNpm(t, {
    ...opts,
    command: 'version',
    mocks: {
      ...opts.mocks,
      '{ROOT}/package.json': { version: '1.0.0' },
    },
  })
  return {
    ...res,
    result: () => res.outputs[0],
  }
}

t.test('node@1', async t => {
  mockGlobals(t, { 'process.versions': { node: '1.0.0' } }, { replace: true })

  t.test('no args', async t => {
    const { version, result } = await mockNpm(t, {
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-version-no-args',
          version: '3.2.1',
        }),
      },
    })

    await version.exec([])

    t.strictSame(
      result(),
      "{ 'test-version-no-args': '3.2.1', npm: '1.0.0', node: '1.0.0' }",
      'should output expected values for various versions in npm'
    )
  })

  t.test('too many args', async t => {
    const { version } = await mockNpm(t)
    await t.rejects(
      version.exec(['foo', 'bar']),
      /npm version/,
      'should throw usage instructions error'
    )
  })

  t.test('completion', async t => {
    const { version } = await mockNpm(t)
    const testComp = async (argv, expect) => {
      const res = await version.completion({ conf: { argv: { remain: argv } } })
      t.strictSame(res, expect, argv.join(' '))
    }

    await testComp(
      ['npm', 'version'],
      ['major', 'minor', 'patch', 'premajor', 'preminor', 'prepatch', 'prerelease', 'from-git']
    )
    await testComp(['npm', 'version', 'major'], [])
  })

  t.test('failure reading package.json', async t => {
    const { version, result } = await mockNpm(t)

    await version.exec([])

    t.strictSame(
      result(),
      `{ npm: '1.0.0', node: '1.0.0' }`,
      'should not have package name on returning object'
    )
  })
})

t.test('empty versions', async t => {
  mockGlobals(t, { 'process.versions': {} }, { replace: true })

  t.test('--json option', async t => {
    const { version, result } = await mockNpm(t, {
      config: { json: true },
    })

    await version.exec([])
    t.same(result(), '{\n  "npm": "1.0.0"\n}', 'should return json stringified result')
  })

  t.test('with one arg', async t => {
    const { version, result } = await mockNpm(t, {
      mocks: {
        libnpmversion: () => '4.0.0',
      },
    })

    await version.exec(['major'])
    t.same(result(), 'v4.0.0', 'outputs the new version prefixed by the tagVersionPrefix')
  })

  t.test('workspaces', async t => {
    t.test('no args, all workspaces', async t => {
      const { version, result } = await mockNpm(t, {
        prefixDir: {
          'package.json': JSON.stringify(
            {
              name: 'workspaces-test',
              version: '1.0.0',
              workspaces: ['workspace-a', 'workspace-b'],
            },
            null,
            2
          ),
          'workspace-a': {
            'package.json': JSON.stringify({
              name: 'workspace-a',
              version: '1.0.0',
            }),
          },
          'workspace-b': {
            'package.json': JSON.stringify({
              name: 'workspace-b',
              version: '1.0.0',
            }),
          },
        },
        config: { workspaces: true },
      })

      await version.exec([])
      t.same(
        result(),
        `{
  'workspace-a': '1.0.0',
  'workspace-b': '1.0.0',
  'workspaces-test': '1.0.0',
  npm: '1.0.0'
}`,
        'outputs includes main package and workspace versions'
      )
    })

    t.test('no args, single workspaces', async t => {
      const { version, result } = await mockNpm(t, {
        prefixDir: {
          'package.json': JSON.stringify(
            {
              name: 'workspaces-test',
              version: '1.0.0',
              workspaces: ['workspace-a', 'workspace-b'],
            },
            null,
            2
          ),
          'workspace-a': {
            'package.json': JSON.stringify({
              name: 'workspace-a',
              version: '1.0.0',
            }),
          },
          'workspace-b': {
            'package.json': JSON.stringify({
              name: 'workspace-b',
              version: '1.0.0',
            }),
          },
        },
        config: {
          workspace: 'workspace-a',
        },
      })

      await version.exec([])
      t.same(
        result(),
        "{ 'workspace-a': '1.0.0', 'workspaces-test': '1.0.0', npm: '1.0.0' }",
        'outputs includes main package and requested workspace versions'
      )
    })

    t.test('no args, all workspaces, workspace with missing name or version', async t => {
      const { version, result } = await mockNpm(t, {
        prefixDir: {
          'package.json': JSON.stringify(
            {
              name: 'workspaces-test',
              version: '1.0.0',
              workspaces: ['workspace-a', 'workspace-b', 'workspace-c'],
            },
            null,
            2
          ),
          'workspace-a': {
            'package.json': JSON.stringify({
              name: 'workspace-a',
              version: '1.0.0',
            }),
          },
          'workspace-b': {
            'package.json': JSON.stringify({
              name: 'workspace-b',
            }),
          },
          'workspace-c': {
            'package.json': JSON.stringify({
              version: '1.0.0',
            }),
          },
        },
        config: { workspaces: true },
      })

      await version.exec([])
      t.same(
        result(),
        "{ 'workspace-a': '1.0.0', 'workspaces-test': '1.0.0', npm: '1.0.0' }",
        'outputs includes main package and valid workspace versions'
      )
    })

    t.test('with one arg, all workspaces', async t => {
      const { version, outputs, prefix } = await mockNpm(t, {
        prefixDir: {
          'package.json': JSON.stringify(
            {
              name: 'workspaces-test',
              version: '1.0.0',
              workspaces: ['workspace-a', 'workspace-b'],
            },
            null,
            2
          ),
          'workspace-a': {
            'package.json': JSON.stringify({
              name: 'workspace-a',
              version: '1.0.0',
            }),
          },
          'workspace-b': {
            'package.json': JSON.stringify({
              name: 'workspace-b',
              version: '1.0.0',
            }),
          },
        },
        config: { workspaces: true },
      })

      await version.exec(['major'])
      t.same(
        outputs.slice(0, 4),
        ['workspace-a', 'v2.0.0', 'workspace-b', 'v2.0.0'],
        'outputs the new version for only the workspaces prefixed by the tagVersionPrefix'
      )

      t.matchSnapshot(readFileSync(resolve(prefix, 'package-lock.json'), 'utf8'))
    })

    t.test('with one arg, all workspaces, saves package.json', async t => {
      const { version, outputs, prefix } = await mockNpm(t, {
        prefixDir: {
          'package.json': JSON.stringify(
            {
              name: 'workspaces-test',
              version: '1.0.0',
              workspaces: ['workspace-a', 'workspace-b'],
              dependencies: {
                'workspace-a': '^1.0.0',
                'workspace-b': '^1.0.0',
              },
            },
            null,
            2
          ),
          'workspace-a': {
            'package.json': JSON.stringify({
              name: 'workspace-a',
              version: '1.0.0',
            }),
          },
          'workspace-b': {
            'package.json': JSON.stringify({
              name: 'workspace-b',
              version: '1.0.0',
            }),
          },
        },
        config: {
          save: true,
          workspaces: true,
        },
      })

      await version.exec(['major'])
      t.same(
        outputs.slice(0, 4),
        ['workspace-a', 'v2.0.0', 'workspace-b', 'v2.0.0'],
        'outputs the new version for only the workspaces prefixed by the tagVersionPrefix'
      )

      t.matchSnapshot(readFileSync(resolve(prefix, 'package-lock.json'), 'utf8'))
    })

    t.test('too many args', async t => {
      const { version } = await mockNpm(t, { config: { workspaces: true } })

      await t.rejects(
        version.exec(['foo', 'bar']),
        /npm version/,
        'should throw usage instructions error'
      )
    })

    t.test('no workspaces-update', async t => {
      const { version, outputs, prefix } = await mockNpm(t, {
        prefixDir: {
          'package.json': JSON.stringify(
            {
              name: 'workspaces-test',
              version: '1.0.0',
              workspaces: ['workspace-a', 'workspace-b'],
            },
            null,
            2
          ),
          'workspace-a': {
            'package.json': JSON.stringify({
              name: 'workspace-a',
              version: '1.0.0',
            }),
          },
          'workspace-b': {
            'package.json': JSON.stringify({
              name: 'workspace-b',
              version: '1.0.0',
            }),
          },
        },
        mocks: {
          libnpmversion: () => {
            return '2.0.0'
          },
        },
        config: {
          workspaces: true,
          'workspaces-update': false,
        },
      })

      await version.exec(['major'])
      t.same(
        outputs.slice(0, 4),
        ['workspace-a', 'v2.0.0', 'workspace-b', 'v2.0.0'],
        'outputs the new version for only the workspaces prefixed by the tagVersionPrefix'
      )

      t.throws(
        () => statSync(resolve(prefix, 'package-lock.json')),
        'should not have a lockfile since have not reified'
      )
    })
  })
})
