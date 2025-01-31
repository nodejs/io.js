const t = require('tap')
const { loadNpmWithRegistry } = require('../../fixtures/mock-npm')
const { cleanZlib } = require('../../fixtures/clean-snapshot')
const pacote = require('pacote')
const Arborist = require('@npmcli/arborist')
const path = require('node:path')
const fs = require('node:fs')

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
  const packageJson = {
    ...pkgJson,
    scripts: {
      prepublishOnly: 'touch scripts-prepublishonly',
      prepublish: 'touch scripts-prepublish', // should NOT run this one
      publish: 'touch scripts-publish',
      postpublish: 'touch scripts-postpublish',
    },
    publishConfig: { registry: alternateRegistry },
  }
  const { npm, joinedOutput, prefix, registry } = await loadNpmWithRegistry(t, {
    config: {
      loglevel: 'silent',
      [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
    },
    prefixDir: {
      'package.json': JSON.stringify(packageJson, null, 2),
    },
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  registry.publish(pkg, { packageJson })
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-prepublishonly')), true, 'ran prepublishOnly')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-prepublish')), false, 'did not run prepublish')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-publish')), true, 'ran publish')
  t.equal(fs.existsSync(path.join(prefix, 'scripts-postpublish')), true, 'ran postpublish')
})

t.test('re-loads publishConfig.registry if added during script process', async t => {
  const initPackageJson = {
    ...pkgJson,
    scripts: {
      prepare: 'cp new.json package.json',
    },
  }
  const packageJson = {
    ...initPackageJson,
    publishConfig: { registry: alternateRegistry },
  }
  const { joinedOutput, npm, registry } = await loadNpmWithRegistry(t, {
    config: {
      [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
      // Keep output from leaking into tap logs for readability
      'foreground-scripts': false,
    },
    prefixDir: {
      'package.json': JSON.stringify(initPackageJson, null, 2),
      'new.json': JSON.stringify(packageJson, null, 2),
    },
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  registry.publish(pkg, { packageJson })
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('prioritize CLI flags over publishConfig', async t => {
  const initPackageJson = {
    ...pkgJson,
    scripts: {
      prepare: 'cp new.json package.json',
    },
  }
  const packageJson = {
    ...initPackageJson,
    publishConfig: { registry: alternateRegistry },
  }
  const { joinedOutput, npm, registry } = await loadNpmWithRegistry(t, {
    config: {
      [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
      // Keep output from leaking into tap logs for readability
      'foreground-scripts': false,
    },
    prefixDir: {
      'package.json': JSON.stringify(initPackageJson, null, 2),
      'new.json': JSON.stringify(packageJson, null, 2),
    },
    argv: ['--registry', alternateRegistry],
    registryUrl: alternateRegistry,
    authorization: 'test-other-token',
  })
  registry.publish(pkg, { packageJson })
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('json', async t => {
  const { joinedOutput, npm, logs, registry } = await loadNpmWithRegistry(t, {
    config: {
      json: true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
    authorization: token,
  })
  registry.publish(pkg)
  await npm.exec('publish', [])
  t.matchSnapshot(logs.notice)
  t.matchSnapshot(joinedOutput(), 'new package json')
})

t.test('dry-run', async t => {
  const { joinedOutput, npm, logs, registry } = await loadNpmWithRegistry(t, {
    config: {
      'dry-run': true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
    authorization: token,
  })
  registry.publish(pkg, { noPut: true })
  await npm.exec('publish', [])
  t.equal(joinedOutput(), `+ ${pkg}@1.0.0`)
  t.matchSnapshot(logs.notice)
})

t.test('foreground-scripts defaults to true', async t => {
  const { outputs, npm, logs, registry } = await loadNpmWithRegistry(t, {
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
  registry.publish('test-fg-scripts', { noPut: true })
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
  const { outputs, npm, logs, registry } = await loadNpmWithRegistry(t, {
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

  registry.publish('test-fg-scripts', { noPut: true })
  await npm.exec('publish', [])

  t.matchSnapshot(logs.notice)

  t.strictSame(
    outputs,
    [`+ test-fg-scripts@0.0.0`],
    'prepack and postpack do not log to stdout')
})

t.test('shows usage with wrong set of arguments', async t => {
  const { publish } = await loadNpmWithRegistry(t, { command: 'publish' })
  await t.rejects(publish.exec(['a', 'b', 'c']), publish.usage)
})

t.test('throws when invalid tag is semver', async t => {
  const { npm } = await loadNpmWithRegistry(t, {
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
  const { npm } = await loadNpmWithRegistry(t, {
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
  const { npm, joinedOutput, logs, home, registry } = await loadNpmWithRegistry(t, {
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
    authorization: token,
  })
  const tarball = await pacote.tarball(home, { Arborist })
  const tarFilename = path.join(home, 'tarball.tgz')
  fs.writeFileSync(tarFilename, tarball)
  registry.publish('test-tar-package')
  await npm.exec('publish', [tarFilename])
  t.matchSnapshot(logs.notice)
  t.matchSnapshot(joinedOutput(), 'new package json')
})

t.test('no auth default registry', async t => {
  const { npm } = await loadNpmWithRegistry(t, {
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
  const { npm, joinedOutput, logs, registry } = await loadNpmWithRegistry(t, {
    config: {
      'dry-run': true,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson, null, 2),
    },
  })
  registry.publish(pkg, { noPut: true })
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput())
  t.matchSnapshot(logs.warn, 'warns about auth being needed')
})

t.test('no auth for configured registry', async t => {
  const { npm } = await loadNpmWithRegistry(t, {
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
  const { npm } = await loadNpmWithRegistry(t, {
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
  const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
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
    registry: alternateRegistry,
    authorization: 'test-scope-token',
  })
  registry.publish('@npm/test-package')
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('has mTLS auth for scope configured registry', async t => {
  const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
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
    registry: alternateRegistry,
  })
  registry.publish('@npm/test-package')
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
    const { npm, joinedOutput, logs, registry } = await loadNpmWithRegistry(t, {
      config: {
        tag: 'latest',
        color: false,
        ...auth,
        workspaces: true,
      },
      prefixDir: dir,
      authorization: token,
    })
    ;['workspace-a', 'workspace-b', 'workspace-n'].forEach(name => {
      registry.publish(name)
    })
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'all public workspaces')
    t.matchSnapshot(logs.warn, 'warns about skipped private workspace')
  })

  t.test('all workspaces - color', async t => {
    const { npm, joinedOutput, logs, registry } = await loadNpmWithRegistry(t, {
      config: {
        ...auth,
        tag: 'latest',
        color: 'always',
        workspaces: true,
      },
      prefixDir: dir,
      authorization: token,
    })
    ;['workspace-a', 'workspace-b', 'workspace-n'].forEach(name => {
      registry.publish(name)
    })
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'all public workspaces')
    t.matchSnapshot(logs.warn, 'warns about skipped private workspace in color')
  })

  t.test('one workspace - success', async t => {
    const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
      config: {
        ...auth,
        tag: 'latest',
        workspace: ['workspace-a'],
      },
      prefixDir: dir,
      authorization: token,
    })
    ;['workspace-a'].forEach(name => {
      registry.publish(name)
    })
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'single workspace')
  })

  t.test('one workspace - failure', async t => {
    const { npm, registry } = await loadNpmWithRegistry(t, {
      config: {
        ...auth,
        tag: 'latest',
        workspace: ['workspace-a'],
      },
      prefixDir: dir,
      authorization: token,
    })
    registry.publish('workspace-a', { putCode: 404 })
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

    const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
      config: {
        ...auth,
        tag: 'latest',
        workspaces: true,
      },
      prefixDir: testDir,
      authorization: token,
    })
    registry.publish('workspace-a')
    await npm.exec('publish', [])
    t.matchSnapshot(joinedOutput(), 'one marked private')
  })

  t.test('invalid workspace', async t => {
    const { npm } = await loadNpmWithRegistry(t, {
      config: {
        ...auth,
        tag: 'latest',
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
    const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
      config: {
        ...auth,
        tag: 'latest',
        workspaces: true,
        json: true,
      },
      prefixDir: dir,
      authorization: token,
    })
    ;['workspace-a', 'workspace-b', 'workspace-n'].forEach(name => {
      registry.publish(name)
    })
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

    const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
      config: {
        ...auth,
        tag: 'latest',
      },
      prefixDir: testDir,
      chdir: ({ prefix }) => path.resolve(prefix, './workspace-a'),
      authorization: token,
    })
    registry.publish('pkg')
    await npm.exec('publish', ['../dir/pkg'])
    t.matchSnapshot(joinedOutput(), 'publish different package spec')
  })

  t.end()
})

t.test('ignore-scripts', async t => {
  const { npm, joinedOutput, prefix, registry } = await loadNpmWithRegistry(t, {
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
    authorization: token,
  })
  registry.publish(pkg)
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
  const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
    config: {
      '//registry.npmjs.org/:_auth': basic,
    },
    prefixDir: {
      'package.json': JSON.stringify(pkgJson),
    },
    basic,
  })
  registry.publish(pkg)
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('bare _auth and registry config', async t => {
  const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
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
    registry: alternateRegistry,
    basic,
  })
  registry.publish('@npm/test-package')
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('bare _auth config scoped registry', async t => {
  const { npm } = await loadNpmWithRegistry(t, {
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
  const { npm, joinedOutput, registry } = await loadNpmWithRegistry(t, {
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
    registry: alternateRegistry,
    basic,
  })
  registry.publish('@npm/test-package')
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
})

t.test('restricted access', async t => {
  const packageJson = {
    name: '@npm/test-package',
    version: '1.0.0',
  }
  const { npm, joinedOutput, logs, registry } = await loadNpmWithRegistry(t, {
    config: {
      ...auth,
      access: 'restricted',
    },
    prefixDir: {
      'package.json': JSON.stringify(packageJson, null, 2),
    },
    authorization: token,
  })
  registry.publish('@npm/test-package', { packageJson, access: 'restricted' })
  await npm.exec('publish', [])
  t.matchSnapshot(joinedOutput(), 'new package version')
  t.matchSnapshot(logs.notice)
})

t.test('public access', async t => {
  const { npm, joinedOutput, logs, registry } = await loadNpmWithRegistry(t, {
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
    authorization: token,
  })
  registry.publish('@npm/test-package', { access: 'public' })
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
  const { npm, registry } = await loadNpmWithRegistry(t, {
    config: {
      ...auth,
      tag: 'latest',
      'foreground-scripts': false,
    },
    chdir: () => root,
    mocks: {
      libnpmpublish: {
        publish: (m) => manifest = m,
      },
    },
  })

  registry.publish('npm', { noPut: true })

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

t.test('prerelease dist tag', (t) => {
  t.test('aborts when prerelease and no tag', async t => {
    const { npm } = await loadNpmWithRegistry(t, {
      config: {
        loglevel: 'silent',
        [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
      },
      prefixDir: {
        'package.json': JSON.stringify({
          ...pkgJson,
          version: '1.0.0-0',
          publishConfig: { registry: alternateRegistry },
        }, null, 2),
      },
    })
    await t.rejects(async () => {
      await npm.exec('publish', [])
    }, new Error('You must specify a tag using --tag when publishing a prerelease version'))
  })

  t.test('does not abort when prerelease and authored tag latest', async t => {
    const packageJson = {
      ...pkgJson,
      version: '1.0.0-0',
      publishConfig: { registry: alternateRegistry },
    }
    const { npm, registry } = await loadNpmWithRegistry(t, {
      config: {
        loglevel: 'silent',
        tag: 'latest',
        [`${alternateRegistry.slice(6)}/:_authToken`]: 'test-other-token',
      },
      prefixDir: {
        'package.json': JSON.stringify(packageJson, null, 2),
      },
      registry: alternateRegistry,
      authorization: 'test-other-token',
    })
    registry.publish(pkg, { packageJson })
    await npm.exec('publish', [])
  })

  t.end()
})

t.test('latest dist tag', (t) => {
  const init = (version) => ({
    config: {
      loglevel: 'silent',
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        ...pkgJson,
        version,
      }, null, 2),
    },
    authorization: token,
  })

  const packuments = [
    // this needs more than one item in it to cover the sort logic
    { version: '50.0.0' },
    { version: '100.0.0' },
    { version: '105.0.0-pre' },
  ]

  t.test('PREVENTS publish when latest version is HIGHER than publishing version', async t => {
    const version = '99.0.0'
    const { npm, registry } = await loadNpmWithRegistry(t, init(version))
    registry.publish(pkg, { noPut: true, packuments })
    await t.rejects(async () => {
      await npm.exec('publish', [])
      /* eslint-disable-next-line max-len */
    }, new Error('Cannot implicitly apply the "latest" tag because published version 100.0.0 is higher than the new version 99.0.0. You must specify a tag using --tag.'))
  })

  t.test('ALLOWS publish when latest is HIGHER than publishing version and flag', async t => {
    const version = '99.0.0'
    const { npm, registry } = await loadNpmWithRegistry(t, {
      ...init(version),
      argv: ['--tag', 'latest'],
    })
    registry.publish(pkg, { packuments })
    await npm.exec('publish', [])
  })

  t.test('ALLOWS publish when latest versions are LOWER than publishing version', async t => {
    const version = '101.0.0'
    const { npm, registry } = await loadNpmWithRegistry(t, init(version))
    registry.publish(pkg, { packuments })
    await npm.exec('publish', [])
  })

  t.test('ALLOWS publish when packument has empty versions (for coverage)', async t => {
    const version = '1.0.0'
    const { npm, registry } = await loadNpmWithRegistry(t, init(version))
    registry.publish(pkg, { manifest: { versions: { } } })
    await npm.exec('publish', [])
  })

  t.test('ALLOWS publish when packument has empty manifest (for coverage)', async t => {
    const version = '1.0.0'
    const { npm, registry } = await loadNpmWithRegistry(t, init(version))
    registry.publish(pkg, { manifest: {} })
    await npm.exec('publish', [])
  })

  t.end()
})
