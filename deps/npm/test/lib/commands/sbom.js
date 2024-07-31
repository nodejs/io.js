const t = require('tap')
const mockNpm = require('../../fixtures/mock-npm.js')

const FAKE_TIMESTAMP = '2020-01-01T00:00:00.000Z'
const FAKE_UUID = '00000000-0000-0000-0000-000000000000'

t.cleanSnapshot = s => {
  let sbom

  try {
    sbom = JSON.parse(s)
  } catch (e) {
    return s
  }

  // Clean dynamic values from snapshots. SPDX and CycloneDX have different
  // formats for these values, so we need to do it separately.
  if (sbom.SPDXID) {
    sbom.documentNamespace = `http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-${FAKE_UUID}`

    if (sbom.creationInfo) {
      sbom.creationInfo.created = FAKE_TIMESTAMP
      sbom.creationInfo.creators = ['Tool: npm/cli-10.0.0']
    }
  } else {
    sbom.serialNumber = `urn:uuid:${FAKE_UUID}`

    if (sbom.metadata) {
      sbom.metadata.timestamp = FAKE_TIMESTAMP
      sbom.metadata.tools[0].version = '10.0.0'
    }
  }

  return JSON.stringify(sbom, null, 2)
}

const simpleNmFixture = {
  node_modules: {
    foo: {
      'package.json': JSON.stringify({
        name: 'foo',
        version: '1.0.0',
        dependencies: {
          dog: '^1.0.0',
        },
      }),
      node_modules: {
        dog: {
          'package.json': JSON.stringify({
            name: 'dog',
            version: '1.0.0',
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
  },
}

const mockSbom = async (t, { mocks, config, ...opts } = {}) => {
  const mock = await mockNpm(t, {
    ...opts,
    config: {
      ...config,
    },
    command: 'sbom',
    mocks: {
      'node:path': {
        ...require('node:path'),
        sep: '/',
      },
      ...mocks,
    },
  })

  return {
    ...mock,
    result: () => mock.joinedOutput(),
  }
}

t.test('sbom', async t => {
  t.test('basic sbom - spdx', async t => {
    const config = {
      'sbom-format': 'spdx',
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-sbom',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
            chai: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('basic sbom - cyclonedx', async t => {
    const config = {
      'sbom-format': 'cyclonedx',
      'sbom-type': 'application',
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-sbom',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
            chai: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('--omit dev', async t => {
    const config = {
      'sbom-format': 'spdx',
      omit: ['dev'],
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-sbom',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
          },
          devDependencies: {
            chai: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('--omit optional', async t => {
    const config = {
      'sbom-format': 'spdx',
      omit: ['optional'],
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-sbom',
          version: '1.0.0',
          dependencies: {
            chai: '^1.0.0',
          },
          optionalDependencies: {
            foo: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('--omit peer', async t => {
    const config = {
      'sbom-format': 'spdx',
      omit: ['peer'],
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-sbom',
          version: '1.0.0',
          dependencies: {
            chai: '^1.0.0',
          },
          peerDependencies: {
            foo: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('missing format', async t => {
    const config = {}
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-sbom',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
            chai: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await t.rejects(sbom.exec([]), {
      code: 'EUSAGE',
      message: 'Must specify --sbom-format flag with one of: cyclonedx, spdx.',
    },
    'should throw error')

    t.matchSnapshot(result())
  })

  t.test('invalid dep', async t => {
    const config = {
      'sbom-format': 'spdx',
    }
    const { sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-ls',
          version: '1.0.0',
          dependencies: {
            foo: '^2.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await t.rejects(
      sbom.exec([]),
      { code: 'ESBOMPROBLEMS', message: /invalid: foo@1.0.0/ },
      'should list dep problems'
    )
  })

  t.test('missing dep', async t => {
    const config = {
      'sbom-format': 'spdx',
    }
    const { sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-ls',
          version: '1.0.0',
          dependencies: {
            ipsum: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await t.rejects(
      sbom.exec([]),
      { code: 'ESBOMPROBLEMS', message: /missing: ipsum@\^1.0.0/ },
      'should list dep problems'
    )
  })

  t.test('missing (optional) dep', async t => {
    const config = {
      'sbom-format': 'spdx',
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-ls',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
            chai: '^1.0.0',
          },
          optionalDependencies: {
            ipsum: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('extraneous dep', async t => {
    const config = {
      'sbom-format': 'spdx',
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-ls',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
          },
        }),
        ...simpleNmFixture,
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('lock file only', async t => {
    const config = {
      'sbom-format': 'spdx',
      'package-lock-only': true,
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-ls',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
            chai: '^1.0.0',
          },
        }),
        'package-lock.json': JSON.stringify({
          dependencies: {
            foo: {
              version: '1.0.0',
              requires: {
                dog: '^1.0.0',
              },
            },
            dog: {
              version: '1.0.0',
            },
            chai: {
              version: '1.0.0',
            },
          },
        }),
      },
    })
    await sbom.exec([])
    t.matchSnapshot(result())
  })

  t.test('lock file only - missing lock file', async t => {
    const config = {
      'sbom-format': 'spdx',
      'package-lock-only': true,
    }
    const { result, sbom } = await mockSbom(t, {
      config,
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'test-npm-ls',
          version: '1.0.0',
          dependencies: {
            foo: '^1.0.0',
            chai: '^1.0.0',
          },
        }),
      },
    })
    await t.rejects(sbom.exec([]), {
      code: 'EUSAGE',
      message: 'A package lock or shrinkwrap file is required in package-lock-only mode',
    },
    'should throw error')

    t.matchSnapshot(result())
  })

  t.test('loading a tree containing workspaces', async t => {
    const mockWorkspaces = async (t, exec = [], config = {}) => {
      const { result, sbom } = await mockSbom(t, {
        config,
        prefixDir: {
          'package.json': JSON.stringify({
            name: 'workspaces-tree',
            version: '1.0.0',
            workspaces: ['./a', './b', './d', './group/*'],
            dependencies: { pacote: '1.0.0' },
          }),
          node_modules: {
            a: t.fixture('symlink', '../a'),
            b: t.fixture('symlink', '../b'),
            c: {
              'package.json': JSON.stringify({
                name: 'c',
                version: '1.0.0',
              }),
            },
            d: t.fixture('symlink', '../d'),
            e: t.fixture('symlink', '../group/e'),
            f: t.fixture('symlink', '../group/f'),
            foo: {
              'package.json': JSON.stringify({
                name: 'foo',
                version: '1.1.1',
                dependencies: {
                  bar: '^1.0.0',
                },
              }),
            },
            bar: {
              'package.json': JSON.stringify({ name: 'bar', version: '1.0.0' }),
            },
            baz: {
              'package.json': JSON.stringify({ name: 'baz', version: '1.0.0' }),
            },
            pacote: {
              'package.json': JSON.stringify({ name: 'pacote', version: '1.0.0' }),
            },
          },
          a: {
            'package.json': JSON.stringify({
              name: 'a',
              version: '1.0.0',
              dependencies: {
                c: '^1.0.0',
                d: '^1.0.0',
              },
              devDependencies: {
                baz: '^1.0.0',
              },
            }),
          },
          b: {
            'package.json': JSON.stringify({
              name: 'b',
              version: '1.0.0',
            }),
          },
          d: {
            'package.json': JSON.stringify({
              name: 'd',
              version: '1.0.0',
              dependencies: {
                foo: '^1.1.1',
              },
            }),
          },
          group: {
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
              }),
            },
          },
        },
      })

      await sbom.exec(exec)

      t.matchSnapshot(result())
    }

    t.test('should list workspaces properly with default configs', t => mockWorkspaces(t, [], {
      'sbom-format': 'spdx',
    }))

    t.test('should not list workspaces with --no-workspaces', t => mockWorkspaces(t, [], {
      'sbom-format': 'spdx',
      workspaces: false,
    }))

    t.test('should filter worksapces with --workspace', t => mockWorkspaces(t, [], {
      'sbom-format': 'spdx',
      workspace: 'a',
    }))

    t.test('should filter workspaces with multiple --workspace flags', t => mockWorkspaces(t, [], {
      'sbom-format': 'spdx',
      workspace: ['e', 'f'],
    }))
  })
})
