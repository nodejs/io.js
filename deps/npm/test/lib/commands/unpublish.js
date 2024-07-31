const t = require('tap')
const { load: loadMockNpm } = require('../../fixtures/mock-npm')

const MockRegistry = require('@npmcli/mock-registry')
const user = 'test-user'
const pkg = 'test-package'
const auth = { '//registry.npmjs.org/:_authToken': 'test-auth-token' }

t.test('no args --force success', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
        version: '1.0.0',
      }, null, 2),
    },
  })

  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({ manifest, query: { write: true }, times: 2 })
  registry.unpublish({ manifest })
  await npm.exec('unpublish', [])
  t.equal(joinedOutput(), '- test-package')
})

t.test('no args --force missing package.json', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      force: true,
    },
  })

  await t.rejects(
    npm.exec('unpublish', []),
    { code: 'EUSAGE' },
    'should throw usage instructions on missing package.json'
  )
})

t.test('no args --force error reading package.json', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      force: true,
    },
    prefixDir: {
      'package.json': '{ not valid json ]',
    },
  })

  await t.rejects(
    npm.exec('unpublish', []),
    /Invalid package.json/,
    'should throw error from reading package.json'
  )
})

t.test('with args --force error reading package.json', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      force: true,
    },
    prefixDir: {
      'package.json': '{ not valid json ]',
    },
  })

  await t.rejects(
    npm.exec('unpublish', [pkg]),
    /Invalid package.json/,
    'should throw error from reading package.json'
  )
})

t.test('no force entire project', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('unpublish', ['@npmcli/unpublish-test']),
    /Refusing to delete entire project/
  )
})

t.test('too many args', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('unpublish', ['a', 'b']),
    { code: 'EUSAGE' },
    'should throw usage instructions if too many args'
  )
})

t.test('range', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('unpublish', ['a@>1.0.0']),
    { code: 'EUSAGE' },
    /single version/
  )
})

t.test('tag', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('unpublish', ['a@>1.0.0']),
    { code: 'EUSAGE' },
    /single version/
  )
})

t.test('unpublish <pkg>@version not the last version', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      ...auth,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({
    name: pkg,
    packuments: ['1.0.0', '1.0.1'],
  })
  await registry.package({ manifest, query: { write: true }, times: 3 })
  registry.nock.put(`/${pkg}/-rev/${manifest._rev}`, body => {
    // sets latest and deletes version 1.0.1
    return body['dist-tags'].latest === '1.0.0' && body.versions['1.0.1'] === undefined
  }).reply(201)
    .intercept(`/${pkg}/-/${pkg}-1.0.1.tgz/-rev/${manifest._rev}`, 'DELETE').reply(201)

  await npm.exec('unpublish', ['test-package@1.0.1'])
  t.equal(joinedOutput(), '- test-package@1.0.1')
})

t.test('unpublish <pkg>@version last version', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      ...auth,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({ manifest, query: { write: true } })

  await t.rejects(
    npm.exec('unpublish', ['test-package@1.0.0']),
    /Refusing to delete the last version of the package/
  )
})

t.test('no version found in package.json no force', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
      }, null, 2),
    },
  })
  await t.rejects(
    npm.exec('unpublish', []),
    /Refusing to delete entire project/
  )
})

t.test('no version found in package.json with force', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({ manifest, query: { write: true }, times: 2 })
  registry.unpublish({ manifest })

  await npm.exec('unpublish', [])
  t.equal(joinedOutput(), '- test-package')
})

t.test('unpublish <pkg> --force no version set', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      ...auth,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({ manifest, query: { write: true }, times: 2 })
  registry.unpublish({ manifest })

  await npm.exec('unpublish', ['test-package'])
  t.equal(joinedOutput(), '- test-package')
})

t.test('silent', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      loglevel: 'silent',
      ...auth,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({
    name: pkg,
    packuments: ['1.0.0', '1.0.1'],
  })
  await registry.package({ manifest, query: { write: true }, times: 3 })
  registry.nock.put(`/${pkg}/-rev/${manifest._rev}`, body => {
    // sets latest and deletes version 1.0.1
    return body['dist-tags'].latest === '1.0.0' && body.versions['1.0.1'] === undefined
  }).reply(201)
    .delete(`/${pkg}/-/${pkg}-1.0.1.tgz/-rev/${manifest._rev}`).reply(201)

  await npm.exec('unpublish', ['test-package@1.0.1'])
  t.equal(joinedOutput(), '')
})

t.test('workspaces', async t => {
  const prefixDir = {
    'package.json': JSON.stringify({
      name: pkg,
      version: '1.0.0',
      workspaces: ['workspace-a', 'workspace-b', 'workspace-c'],
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
        version: '1.2.3-b',
        repository: 'https://github.com/npm/workspace-b',
      }),
    },
    'workspace-c': {
      'package.json': JSON.stringify({
        name: 'workspace-n',
        version: '1.2.3-n',
      }),
    },
  }

  t.test('with package name no force', async t => {
    const { npm } = await loadMockNpm(t, {
      config: {
        workspace: ['workspace-a'],
      },
      prefixDir,
    })
    await t.rejects(
      npm.exec('unpublish', ['workspace-a']),
      /Refusing to delete entire project/
    )
  })

  t.test('all workspaces last version --force', async t => {
    const { joinedOutput, npm } = await loadMockNpm(t, {
      config: {
        workspaces: true,
        force: true,
        ...auth,
      },
      prefixDir,
    })
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: 'test-auth-token',
    })
    const manifestA = registry.manifest({ name: 'workspace-a', versions: ['1.2.3-a'] })
    const manifestB = registry.manifest({ name: 'workspace-b', versions: ['1.2.3-b'] })
    const manifestN = registry.manifest({ name: 'workspace-n', versions: ['1.2.3-n'] })
    await registry.package({ manifest: manifestA, query: { write: true }, times: 2 })
    await registry.package({ manifest: manifestB, query: { write: true }, times: 2 })
    await registry.package({ manifest: manifestN, query: { write: true }, times: 2 })
    registry.nock.delete(`/workspace-a/-rev/${manifestA._rev}`).reply(201)
      .delete(`/workspace-b/-rev/${manifestB._rev}`).reply(201)
      .delete(`/workspace-n/-rev/${manifestN._rev}`).reply(201)

    await npm.exec('unpublish', [])
    t.equal(joinedOutput(), '- workspace-a\n- workspace-b\n- workspace-n')
  })
})

t.test('dryRun with spec', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      'dry-run': true,
      ...auth,
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({
    name: pkg,
    packuments: ['1.0.0', '1.0.1'],
  })
  await registry.package({ manifest, query: { write: true } })

  await npm.exec('unpublish', ['test-package@1.0.1'])
  t.equal(joinedOutput(), '- test-package@1.0.1')
})

t.test('dryRun with no args', async t => {
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      'dry-run': true,
      ...auth,
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
        version: '1.0.0',
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: npm.config.get('registry'),
    authorization: 'test-auth-token',
  })
  const manifest = registry.manifest({
    name: pkg,
    packuments: ['1.0.0', '1.0.1'],
  })
  await registry.package({ manifest, query: { write: true } })

  await npm.exec('unpublish', [])
  t.equal(joinedOutput(), '- test-package@1.0.0')
})

t.test('publishConfig no spec', async t => {
  const alternateRegistry = 'https://other.registry.npmjs.org'
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      '//other.registry.npmjs.org/:_authToken': 'test-other-token',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
        version: '1.0.0',
        publishConfig: {
          registry: alternateRegistry,
        },
      }, null, 2),
    },
  })

  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({ manifest, query: { write: true }, times: 2 })
  registry.unpublish({ manifest })
  await npm.exec('unpublish', [])
  t.equal(joinedOutput(), '- test-package')
})

t.test('prioritize CLI flags over publishConfig no spec', async t => {
  const alternateRegistry = 'https://other.registry.npmjs.org'
  const publishConfig = { registry: 'http://publishconfig' }
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      '//other.registry.npmjs.org/:_authToken': 'test-other-token',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
        version: '1.0.0',
        publishConfig,
      }, null, 2),
    },
    argv: ['--registry', alternateRegistry],
  })

  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({ manifest, query: { write: true }, times: 2 })
  registry.unpublish({ manifest })
  await npm.exec('unpublish', [])
  t.equal(joinedOutput(), '- test-package')
})

t.test('publishConfig with spec', async t => {
  const alternateRegistry = 'https://other.registry.npmjs.org'
  const { joinedOutput, npm } = await loadMockNpm(t, {
    config: {
      force: true,
      '//other.registry.npmjs.org/:_authToken': 'test-other-token',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
        version: '1.0.0',
        publishConfig: {
          registry: alternateRegistry,
        },
      }, null, 2),
    },
  })

  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  const manifest = registry.manifest({ name: pkg })
  await registry.package({ manifest, query: { write: true }, times: 2 })
  registry.unpublish({ manifest })
  await npm.exec('unpublish', ['test-package'])
  t.equal(joinedOutput(), '- test-package')
})

t.test('scoped registry config', async t => {
  const scopedPkg = `@npm/test-package`
  const alternateRegistry = 'https://other.registry.npmjs.org'
  const { npm } = await loadMockNpm(t, {
    config: {
      force: true,
      '@npm:registry': alternateRegistry,
      '//other.registry.npmjs.org/:_authToken': 'test-other-token',
    },
    prefixDir: {
      'package.json': JSON.stringify({
        name: pkg,
        version: '1.0.0',
        publishConfig: {
          registry: alternateRegistry,
        },
      }, null, 2),
    },
  })
  const registry = new MockRegistry({
    tap: t,
    registry: alternateRegistry,
    authorization: 'test-other-token',
  })
  const manifest = registry.manifest({ name: scopedPkg })
  await registry.package({ manifest, query: { write: true }, times: 2 })
  registry.unpublish({ manifest })
  await npm.exec('unpublish', [scopedPkg])
})

t.test('completion', async t => {
  const { npm, unpublish } = await loadMockNpm(t, {
    command: 'unpublish',
    config: {
      ...auth,
    },
  })

  const testComp =
    async (t, { argv, partialWord, expect, title }) => {
      const res = await unpublish.completion(
        { conf: { argv: { remain: argv } }, partialWord }
      )
      t.strictSame(res, expect, title || argv.join(' '))
    }

  t.test('completing with multiple versions from the registry', async t => {
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: 'test-auth-token',
    })
    const manifest = registry.manifest({
      name: pkg,
      packuments: ['1.0.0', '1.0.1'],
    })
    await registry.package({ manifest, query: { write: true } })
    registry.whoami({ username: user })
    registry.getPackages({ team: user, packages: { [pkg]: 'write' } })

    await testComp(t, {
      argv: ['npm', 'unpublish'],
      partialWord: 'test-package',
      expect: [
        'test-package@1.0.0',
        'test-package@1.0.1',
      ],
    })
  })

  t.test('no versions retrieved', async t => {
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: 'test-auth-token',
    })
    const manifest = registry.manifest({ name: pkg })
    manifest.versions = {}
    await registry.package({ manifest, query: { write: true } })
    registry.whoami({ username: user })
    registry.getPackages({ team: user, packages: { [pkg]: 'write' } })

    await testComp(t, {
      argv: ['npm', 'unpublish'],
      partialWord: pkg,
      expect: [
        pkg,
      ],
      title: 'should autocomplete package name only',
    })
  })

  t.test('packages starting with same letters', async t => {
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: 'test-auth-token',
    })
    registry.whoami({ username: user })
    registry.getPackages({ team: user,
      packages: {
        [pkg]: 'write',
        [`${pkg}a`]: 'write',
        [`${pkg}b`]: 'write',
      } })

    await testComp(t, {
      argv: ['npm', 'unpublish'],
      partialWord: pkg,
      expect: [
        pkg,
        `${pkg}a`,
        `${pkg}b`,
      ],
    })
  })

  t.test('no packages retrieved', async t => {
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: 'test-auth-token',
    })
    registry.whoami({ username: user })
    registry.getPackages({ team: user, packages: {} })

    await testComp(t, {
      argv: ['npm', 'unpublish'],
      partialWord: 'pkg',
      expect: [],
      title: 'should have no autocompletion',
    })
  })

  t.test('no pkg name to complete', async t => {
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: 'test-auth-token',
    })
    registry.whoami({ username: user })
    registry.getPackages({ team: user,
      packages: {
        [pkg]: 'write',
        [`${pkg}a`]: 'write',
      } })

    await testComp(t, {
      argv: ['npm', 'unpublish'],
      partialWord: undefined,
      expect: [pkg, `${pkg}a`],
      title: 'should autocomplete with available package names from user',
    })
  })

  t.test('logged out user', async t => {
    const registry = new MockRegistry({
      tap: t,
      registry: npm.config.get('registry'),
      authorization: 'test-auth-token',
    })
    registry.whoami({ responseCode: 404 })

    await testComp(t, {
      argv: ['npm', 'unpublish'],
      partialWord: pkg,
      expect: [],
    })
  })

  t.test('too many args', async t => {
    await testComp(t, {
      argv: ['npm', 'unpublish', 'foo'],
      partialWord: undefined,
      expect: [],
    })
  })
})
