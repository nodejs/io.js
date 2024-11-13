const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')
const { cleanZlib } = require('../../fixtures/clean-snapshot')
const MockRegistry = require('@npmcli/mock-registry')
const pacote = require('pacote')
const Arborist = require('@npmcli/arborist')
const path = require('node:path')
const fs = require('node:fs')
const npa = require('npm-package-arg')

const pkg = 'test-package'
const token = 'test-auth-token'
const auth = { '//registry.npmjs.org/:_authToken': token }
const alternateRegistry = 'https://other.registry.npmjs.org'
const basic = Buffer.from('test-user:test-password').toString('base64')

const pkgJson = {
  name: pkg,
  description: 'npm test package',
  version: '1.0.0',
}

t.cleanSnapshot = data => cleanZlib(data)

t.test('respects publishConfig.registry, runs appropriate scripts', async t => {
  const { npm, joinedOutput, prefix } = await loadMockNpm(t, {
    config: {
      loglevel: 'silent',
      [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        ...pkgJson,
        scripts: {
          prepublishOnly: 'touch scripts-prepublishonly',
          prepublish: 'touch scripts-prepublish', // should NOT run this one
          publish: 'touch scripts-publish',
          postpublish: 'touch scripts-postpublish',
        },
        publishConfig: { registry: alternateRegistry },
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  registry.nock.put(`/${pkg}`, body => {
    return t.match(body, {
      _id: pkg,
      name: pkg,
      'dist-tags': { latest: '1.0.0' },
      access: null,
      versions: {
        '1.0.0': {
          name: pkg,
          version: '1.0.0',
          _id: `${pkg}@1.0.0`,
          dist: {
            shasum: /\.*/,
            tarball: `http:${alternateRegistry.slice(6)}/test-package/-/test-package-1.0.0.tgz`,
          },
          publishConfig: {
            registry: alternateRegistry,
          },
        },
      },
      _attachments: {
        [`${pkg}-1.0.0.tgz`]: {},
      },
    })
  }).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-prepublishonly')), true, 'ran prepublishOnly')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-prepublish')), false, 'did not run prepublish')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-publish')), true, 'ran publish')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-postpublish')), true, 'ran postpublish')
})

t.test('re-loads publishConfig.registry if added during script process', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
      // Keep output from leaking into tap logs for readability
      'foreground-scripts': false,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        ...pkgJson,
        scripts: {
          prepare: 'cp new.json package.json',
        },
      }, null, 2),
      'new.json': JSON.stringify({
        ...pkgJson,
        publishConfig: { registry: alternateRegistry },
      }),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  registry.nock.put(`/${pkg}`, body => {
    return t.match(body, {
      _id: pkg,
      name: pkg,
      'dist-tags': { latest: '1.0.0' },
      access: null,
      versions: {
        '1.0.0': {
          name: pkg,
          version: '1.0.0',
          _id: `${pkg}@1.0.0`,
          dist: {
            shasum: /\.*/,
            tarball: `http:${alternateRegistry.slice(6)}/test-package/-/test-package-1.0.0.tgz`,
          },
          publishConfig: {
            registry: alternateRegistry,
          },
        },
      },
      _attachments: {
        [`${pkg}-1.0.0.tgz`]: {},
      },
    })
  }).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('prioritize CLI flags over publishConfig', async t => {
  const publishConfig = { registry: 'http://publishconfig' }
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
      // Keep output from leaking into tap logs for readability
      'foreground-scripts': false,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        ...pkgJson,
        scripts: {
          prepare: 'cp new.json package.json',
        },
      }, null, 2),
      'new.json': JSON.stringify({
        ...pkgJson,
        publishConfig,
      }),
    },
    argv: ['--registry', alternateRegistry],
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  registry.nock.put(`/${pkg}`, body => {
    return t.match(body, {
      _id: pkg,
      name: pkg,
      'dist-tags': { latest: '1.0.0' },
      access: null,
      versions: {
        '1.0.0': {
          name: pkg,
          version: '1.0.0',
          _id: `${pkg}@1.0.0`,
          dist: {
            shasum: /\.*/,
            tarball: `http:${alternateRegistry.slice(6)}/test-package/-/test-package-1.0.0.tgz`,
          },
          publishConfig,
        },
      },
      _attachments: {
        [`${pkg}-1.0.0.tgz`]: {},
      },
    })
  }).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('json', async t => {
  const { joinedOutput, npm, logs } = await loadMockNpm(t, {
    config: {
      json: true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  registry.nock.put(`/${pkg}`).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(logs.notice)
  t.matchSnapshot(joinedOutput(), 'new package json')
})

t.test('dry-run', async t => {
  const { joinedOutput, npm, logs } = await loadMockNpm(t, {
    config: {
      'dry-run': true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  await npm.exec('publish', [])
  t.equal(joinedOutput(), `+ ${pkg}@1.0.0`)
  t.matchSnapshot(logs.notice)
})

t.test('foreground-scripts defaults to true', async t => {
  const { outputs, npm, logs } = await loadMockNpm(t, {
    config: {
      'dry-run': true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'test-fg-scripts',
        version: '0.0.0',
        scripts: {
          prepack: 'echo prepack!',
          postpack: 'echo postpack!',
        },
      }
      ),
    },
  })

  await npm.exec('publish', [])

  t.matchSnapshot(logs.notice)

  t.strictSame(
    outputs,
    [
      '\n> test-fg-scripts@0.0.0 prepack\n> echo prepack!\n',
      '\n> test-fg-scripts@0.0.0 postpack\n> echo postpack!\n',
      `+ test-fg-scripts@0.0.0`,
    ],
    'prepack and postpack log to stdout')
})

t.test('foreground-scripts can still be set to false', async t => {
  const { outputs, npm, logs } = await loadMockNpm(t, {
    config: {
      'dry-run': true,
      'foreground-scripts': false,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'test-fg-scripts',
        version: '0.0.0',
        scripts: {
          prepack: 'echo prepack!',
          postpack: 'echo postpack!',
        },
      }
      ),
    },
  })

  await npm.exec('publish', [])

  t.matchSnapshot(logs.notice)

  t.strictSame(
    outputs,
    [`+ test-fg-scripts@0.0.0`],
    'prepack and postpack do not log to stdout')
})

t.test('shows usage with wrong set of arguments', async t => {
  const { publish } = await loadMockNpm(t, { command: 'publish' })
  await t.rejects(publish.exec(['a', 'b', 'c']), publish.usage)
})

t.test('throws when invalid tag is semver', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      tag: '0.0.13',
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  await t.rejects(
    npm.exec('publish', []),
    { message: 'Tag name must not be a valid SemVer range: 0.0.13' }
  )
})

t.test('throws when invalid tag when not url encodable', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      tag: '@test',
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  await t.rejects(
    npm.exec('publish', []),
    {
      /* eslint-disable-next-line max-len */
      message: 'Invalid tag name "@test" of package "test-package@@test": Tags may not have any characters that encodeURIComponent encodes.',
    }
  )
})

t.test('tarball', async t => {
  const { npm, joinedOutput, logs, home } = await loadMockNpm(t, {
    config: {
      'fetch-retries': 0,
      ...auth,
    },
    homeDir: {
      'package.json': JSON.stringify({
        name: 'test-tar-package',
        description: 'this was from a tarball',
        version: '1.0.0',
      }, null, 2),
      'index.js': 'console.log("hello world"}',
    },
  })
  const tarball = await pacote.tarball(home, { Arborist })
  const tarFilename = path.join(home, 'tarball.tgz')
  fs.writeFileSync(tarFilename, tarball)
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  registry.nock.put('/test-tar-package', body => {
    return t.match(body, {
      name: 'test-tar-package',
    })
  }).reply(200, {})
  await npm.exec('publish', [tarFilename])
  t.matchSnapshot(logs.notice)
  t.matchSnapshot(joinedOutput(), 'new package json')
})

t.test('no auth default registry', async t => {
  const { npm } = await loadMockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  await t.rejects(
    npm.exec('publish', []),
    {
      message: 'This command requires you to be logged in to https://registry.npmjs.org/',
      code: 'ENEEDAUTH',
    }
  )
})

t.test('no auth dry-run', async t => {
  const { npm, joinedOutput, logs } = await loadMockNpm(t, {
    config: {
      'dry-run': true,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput())
  t.matchSnapshot(logs.warn, 'warns about auth being needed')
})

t.test('no auth for configured registry', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      registry: alternateRegistry,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  await t.rejects(
    npm.exec('publish', []),
    {
      message: `This command requires you to be logged in to ${alternateRegistry}`,
      code: 'ENEEDAUTH',
    }
  )
})

t.test('no auth for scope configured registry', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      scope: '@npm',
      registry: alternateRegistry,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  await t.rejects(
    npm.exec('publish', []),
    {
      message: `This command requires you to be logged in to ${alternateRegistry}`,
      code: 'ENEEDAUTH',
    }
  )
})

t.test('has token auth for scope configured registry', async t => {
  const spec = npa('@npm/test-package')
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      scope: '@npm',
      registry: alternateRegistry,
      [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-scope-token',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-scope-token',
  })
  registry.nock.put(`/${spec.escapedName}`, body => {
    return t.match(body, { name: '@npm/test-package' })
  }).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('has mTLS auth for scope configured registry', async t => {
  const spec = npa('@npm/test-package')
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      scope: '@npm',
      registry: alternateRegistry,
      [`${alternateRegistry.slice(6)}/:certfile`]: '/some.cert',
      [`${alternateRegistry.slice(6)}/:keyfile`]: '/some.key',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
  })
  registry.nock.put(`/${spec.escapedName}`, body => {
    return t.match(body, { name: '@npm/test-package' })
  }).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('workspaces', t => {
  const dir = {
    'package.json': JSON.stringify(
      {
        ...pkgJson,
        workspaces: ['workspace-a', 'workspace-b', 'workspace-c', 'workspace-p'],
      }, null, 2),
    'workspace-a': {
      'package.json': JSON.stringify({
        name: 'workspace-a',
        version: '1.2.3-a',
        repository: 'http://repo.workspace-a/',
      }),
    },
    'workspace-b': {
      'package.json': JSON.stringify({
        name: 'workspace-b',
        version: '1.2.3-n',
        repository: 'https://github.com/npm/workspace-b',
      }),
    },
    'workspace-c': {
      'package.json': JSON.stringify({
        name: 'workspace-n',
        version: '1.2.3-n',
      }),
    },
    'workspace-p': {
      'package.json': JSON.stringify({
        name: 'workspace-p',
        version: '1.2.3-p',
        private: true,
      }),
    },
  }

  t.test('all workspaces - no color', async t => {
    const { npm, joinedOutput, logs } = await loadMockNpm(t, {
      config: {
        color: false,
        ...auth,
        workspaces: true,
      },
      prefixDir: dir,
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.nock
      .put('/workspace-a', body => {
        return t.match(body, { name: 'workspace-a' })
      }).reply(200, {})
      .put('/workspace-b', body => {
        return t.match(body, { name: 'workspace-b' })
      }).reply(200, {})
      .put('/workspace-n', body => {
        return t.match(body, { name: 'workspace-n' })
      }).reply(200, {})
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'all public workspaces')
    t.matchSnapshot(logs.warn, 'warns about skipped private workspace')
  })

  t.test('all workspaces - color', async t => {
    const { npm, joinedOutput, logs } = await loadMockNpm(t, {
      config: {
        ...auth,
        color: 'always',
        workspaces: true,
      },
      prefixDir: dir,
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.nock
      .put('/workspace-a', body => {
        return t.match(body, { name: 'workspace-a' })
      }).reply(200, {})
      .put('/workspace-b', body => {
        return t.match(body, { name: 'workspace-b' })
      }).reply(200, {})
      .put('/workspace-n', body => {
        return t.match(body, { name: 'workspace-n' })
      }).reply(200, {})
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'all public workspaces')
    t.matchSnapshot(logs.warn, 'warns about skipped private workspace in color')
  })

  t.test('one workspace - success', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, {
      config: {
        ...auth,
        workspace: ['workspace-a'],
      },
      prefixDir: dir,
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.nock
      .put('/workspace-a', body => {
        return t.match(body, { name: 'workspace-a' })
      }).reply(200, {})
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'single workspace')
  })

  t.test('one workspace - failure', async t => {
    const { npm } = await loadMockNpm(t, {
      config: {
        ...auth,
        workspace: ['workspace-a'],
      },
      prefixDir: dir,
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.nock
      .put('/workspace-a', body => {
        return t.match(body, { name: 'workspace-a' })
      }).reply(404, {})
    await t.rejects(npm.exec('publish', []), { code: 'E404' })
  })

  t.test('all workspaces - some marked private', async t => {
    const testDir = {
      'package.json': JSON.stringify(
        {
          ...pkgJson,
          workspaces: ['workspace-a', 'workspace-p'],
        }, null, 2),
      'workspace-a': {
        'package.json': JSON.stringify({
          name: 'workspace-a',
          version: '1.2.3-a',
        }),
      },
      'workspace-p': {
        'package.json': JSON.stringify({
          name: '@scoped/workspace-p',
          private: true,
          version: '1.2.3-p-scoped',
        }),
      },
    }

    const { npm, joinedOutput } = await loadMockNpm(t, {
      config: {
        ...auth,
        workspaces: true,
      },
      prefixDir: testDir,
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.nock
      .put('/workspace-a', body => {
        return t.match(body, { name: 'workspace-a' })
      }).reply(200, {})
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'one marked private')
  })

  t.test('invalid workspace', async t => {
    const { npm } = await loadMockNpm(t, {
      config: {
        ...auth,
        workspace: ['workspace-x'],
      },
      prefixDir: dir,
    })
    await t.rejects(
      npm.exec('publish', []),
      { message: 'No workspaces found:\n  --workspace=workspace-x' }
    )
  })

  t.test('json', async t => {
    const { npm, joinedOutput } = await loadMockNpm(t, {
      config: {
        ...auth,
        workspaces: true,
        json: true,
      },
      prefixDir: dir,
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.nock
      .put('/workspace-a', body => {
        return t.match(body, { name: 'workspace-a' })
      }).reply(200, {})
      .put('/workspace-b', body => {
        return t.match(body, { name: 'workspace-b' })
      }).reply(200, {})
      .put('/workspace-n', body => {
        return t.match(body, { name: 'workspace-n' })
      }).reply(200, {})
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'all workspaces in json')
  })

  t.test('differet package spec', async t => {
    const testDir = {
      'package.json': JSON.stringify(
        {
          ...pkgJson,
          workspaces: ['workspace-a'],
        }, null, 2),
      'workspace-a': {
        'package.json': JSON.stringify({
          name: 'workspace-a',
          version: '1.2.3-a',
        }),
      },
      'dir/pkg': {
        'package.json': JSON.stringify({
          name: 'pkg',
          version: '1.2.3',
        }),
      },
    }

    const { npm, joinedOutput } = await loadMockNpm(t, {
      config: {
        ...auth,
      },
      prefixDir: testDir,
      chdir: ({ prefix }) => path.resolve(prefix, './workspace-a'),
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: token,
    })
    registry.nock
      .put('/pkg', body => {
        return t.match(body, { name: 'pkg' })
      }).reply(200, {})
    await npm.exec('publish', ['../dir/pkg'])
    t.matchSnapshot(joinedOutput(), 'publish different package spec')
  })

  t.end()
})

t.test('ignore-scripts', async t => {
  const { npm, joinedOutput, prefix } = await loadMockNpm(t, {
    config: {
      ...auth,
      'ignore-scripts': true,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        ...pkgJson,
        scripts: {
          prepublishOnly: 'touch scripts-prepublishonly',
          prepublish: 'touch scripts-prepublish', // should NOT run this one
          publish: 'touch scripts-publish',
          postpublish: 'touch scripts-postpublish',
        },
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  registry.nock.put(`/${pkg}`).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
  t.equal(
    fs.existsSync(path.join(prefix, 'scripts-prepublishonly')),
    false,
    'did not run prepublishOnly'
  )
  t.equal(
    fs.existsSync(path.join(prefix, 'scripts-prepublish')),
    false,
    'did not run prepublish'
  )
  t.equal(
    fs.existsSync(path.join(prefix, 'scripts-publish')),
    false,
    'did not run publish'
  )
  t.equal(
    fs.existsSync(path.join(prefix, 'scripts-postpublish')),
    false,
    'did not run postpublish'
  )
})

t.test('_auth config default registry', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      '//registry.npmjs.org/:_auth': basic,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    basic,
  })
  registry.nock.put(`/${pkg}`).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('bare _auth and registry config', async t => {
  const spec = npa('@npm/test-package')
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      registry: alternateRegistry,
      '//other.registry.npmjs.org/:_auth': basic,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    basic,
  })
  registry.nock.put(`/${spec.escapedName}`).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('bare _auth config scoped registry', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      scope: '@npm',
      registry: alternateRegistry,
      _auth: basic,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  await t.rejects(
    npm.exec('publish', []),
    { message: `This command requires you to be logged in to ${alternateRegistry}` }
  )
})

t.test('scoped _auth config scoped registry', async t => {
  const spec = npa('@npm/test-package')
  const { npm, joinedOutput } = await loadMockNpm(t, {
    config: {
      scope: '@npm',
      registry: alternateRegistry,
      [`${alternateRegistry.slice(6)}/:_auth`]: basic,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    basic,
  })
  registry.nock.put(`/${spec.escapedName}`).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('restricted access', async t => {
  const spec = npa('@npm/test-package')
  const { npm, joinedOutput, logs } = await loadMockNpm(t, {
    config: {
      ...auth,
      access: 'restricted',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  registry.nock.put(`/${spec.escapedName}`, body => {
    t.equal(body.access, 'restricted', 'access is explicitly set to restricted')
    return true
  }).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
  t.matchSnapshot(logs.notice)
})

t.test('public access', async t => {
  const spec = npa('@npm/test-package')
  const { npm, joinedOutput, logs } = await loadMockNpm(t, {
    config: {
      ...auth,
      access: 'public',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: '@npm/test-package',
        version: '1.0.0',
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: token,
  })
  registry.nock.put(`/${spec.escapedName}`, body => {
    t.equal(body.access, 'public', 'access is explicitly set to public')
    return true
  }).reply(200, {})
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
  t.matchSnapshot(logs.notice)
})

t.test('manifest', async t => {
  // https://github.com/npm/cli/pull/6470#issuecomment-1571234863

  // snapshot test that was generated against v9.6.7 originally to ensure our
  // own manifest does not change unexpectedly when publishing. this test
  // asserts a bunch of keys are there that will change often and then snapshots
  // the rest of the manifest.

  const root = path.resolve(__dirname, '../../..')
  const npmPkg = require(path.join(root, 'package.json'))

  t.cleanSnapshot = (s) => s.replace(new RegExp(npmPkg.version, 'g'), '{VERSION}')

  let manifest = null
  const { npm } = await loadMockNpm(t, {
    config: {
      ...auth,
      'foreground-scripts': false,
    },
    chdir: () => root,
    mocks: {
      libnpmpublish: {
        publish: (m) => manifest = m,
      },
    },
  })
  await npm.exec('publish', [])

  const okKeys = [
    'contributors',
    'bundleDependencies',
    'dependencies',
    'devDependencies',
    'templateOSS',
    'scripts',
    'tap',
    'readme',
    'engines',
    'workspaces',
  ]

  for (const k of okKeys) {
    t.ok(manifest[k], k)
    delete manifest[k]
  }
  delete manifest.gitHead

  manifest.man.sort()

  t.matchSnapshot(manifest, 'manifest')
})
