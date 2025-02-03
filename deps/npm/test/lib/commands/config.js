const { join } = require('node:path')
const fs = require('node:fs/promises')
const ini = require('ini')
const tspawk = require('../../fixtures/tspawk')
const t = require('tap')
const { load: _loadMockNpm } = require('../../fixtures/mock-npm')
const { cleanCwd } = require('../../fixtures/clean-snapshot.js')

const spawk = tspawk(t)

const replaceJsonOrIni = (key) => [
  new RegExp(`(\\s(?:${key} = |"${key}": )"?)[^"\\n,]+`, 'g'),
  `$1{${key.toUpperCase()}}`,
]

const replaceIniComment = (key) => [
  new RegExp(`(; ${key} = ).*`, 'g'),
  `$1{${key.replaceAll(' ', '-').toUpperCase()}}`,
]

t.cleanSnapshot = (s) => cleanCwd(s)
  .replaceAll(...replaceIniComment('node version'))
  .replaceAll(...replaceIniComment('npm version'))
  .replaceAll(...replaceIniComment('node bin location'))
  .replaceAll(...replaceJsonOrIni('npm-version'))
  .replaceAll(...replaceJsonOrIni('viewer'))
  .replaceAll(...replaceJsonOrIni('shell'))
  .replaceAll(...replaceJsonOrIni('editor'))
  .replaceAll(...replaceJsonOrIni('progress'))
  .replaceAll(...replaceJsonOrIni('color'))
  .replaceAll(...replaceJsonOrIni('cache'))

const loadMockNpm = (t, opts = {}) => _loadMockNpm(t, {
  ...opts,
  config: {
    ...opts.config,
    // Reset configs that mock npm sets by default
    'fetch-retries': undefined,
    loglevel: undefined,
    color: false,
  },
})

t.test('config no args', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('config', []),
    {
      code: 'EUSAGE',
    },
    'rejects with usage'
  )
})

t.test('config ignores workspaces', async t => {
  const { npm } = await loadMockNpm(t, {
    config: {
      workspaces: true,
    },
  })

  await t.rejects(
    npm.exec('config'),
    {
      code: 'ENOWORKSPACES',
    },
    'rejects with usage'
  )
})

t.test('config list', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      '.npmrc': 'projectloaded=yes',
    },
    globalPrefixDir: {
      etc: {
        npmrc: 'globalloaded=yes',
      },
    },
    homeDir: {
      '.npmrc': [
        'userloaded=yes',
        'auth=bad',
        '_auth=bad',
        '//nerfdart:auth=bad',
        '//nerfdart:_auth=bad',
      ].join('\n'),
    },
  })

  await npm.exec('config', ['list'])

  const output = joinedOutput()

  t.match(output, 'projectloaded = "yes"')
  t.match(output, 'globalloaded = "yes"')
  t.match(output, 'userloaded = "yes"')

  t.matchSnapshot(output, 'output matches snapshot')
})

t.test('config list --long', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      '.npmrc': 'projectloaded=yes',
    },
    globalPrefixDir: {
      etc: {
        npmrc: 'globalloaded=yes',
      },
    },
    homeDir: {
      '.npmrc': 'userloaded=yes',
    },
    config: {
      long: true,
    },
  })

  await npm.exec('config', ['list'])

  const output = joinedOutput()

  t.match(output, 'projectloaded = "yes"')
  t.match(output, 'globalloaded = "yes"')
  t.match(output, 'userloaded = "yes"')

  t.matchSnapshot(output, 'output matches snapshot')
})

t.test('config list --json', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t, {
    prefixDir: {
      '.npmrc': 'projectloaded=yes',
    },
    globalPrefixDir: {
      etc: {
        npmrc: 'globalloaded=yes',
      },
    },
    homeDir: {
      '.npmrc': 'userloaded=yes',
    },
    config: {
      json: true,
    },
  })

  await npm.exec('config', ['list'])

  const output = joinedOutput()

  t.match(output, '"projectloaded": "yes",')
  t.match(output, '"globalloaded": "yes",')
  t.match(output, '"userloaded": "yes",')

  t.matchSnapshot(output, 'output matches snapshot')
})

t.test('config list with publishConfig', async t => {
  const loadMockNpmWithPublishConfig = (t, opts) => loadMockNpm(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        publishConfig: {
          registry: 'https://some.registry',
          _authToken: 'mytoken',
        },
      }),
    },
    ...opts,
  })

  t.test('local', async t => {
    const { npm, joinedOutput } = await loadMockNpmWithPublishConfig(t)

    await npm.exec('config', ['list'])

    const output = joinedOutput()

    t.match(output, 'registry = "https://some.registry"')

    t.matchSnapshot(output, 'output matches snapshot')
  })

  t.test('global', async t => {
    const { npm, joinedOutput } = await loadMockNpmWithPublishConfig(t, {
      config: {
        global: true,
      },
    })

    await npm.exec('config', ['list'])

    const output = joinedOutput()

    t.notMatch(output, 'registry = "https://some.registry"')

    t.matchSnapshot(output, 'output matches snapshot')
  })
})

t.test('config delete no args', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('config', ['delete']),
    {
      code: 'EUSAGE',
    },
    'rejects with usage'
  )
})

t.test('config delete single key', async t => {
  // location defaults to user, so we work with a userconfig
  const { npm, home } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': 'access=public\nall=true',
    },
  })

  await npm.exec('config', ['delete', 'access'])

  t.equal(npm.config.get('access'), null, 'acces should be defaulted')

  const contents = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.not(rc.access, 'access is not set')
})

t.test('config delete multiple keys', async t => {
  const { npm, home } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': 'access=public\nall=true\naudit=false',
    },
  })

  await npm.exec('config', ['delete', 'access', 'all'])

  t.equal(npm.config.get('access'), null, 'access should be defaulted')
  t.equal(npm.config.get('all'), false, 'all should be defaulted')

  const contents = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.not(rc.access, 'access is not set')
  t.not(rc.all, 'all is not set')
})

t.test('config delete key --location=global', async t => {
  const { npm, globalPrefix } = await loadMockNpm(t, {
    globalPrefixDir: {
      etc: {
        npmrc: 'access=public\nall=true',
      },
    },
    config: {
      location: 'global',
    },
  })
  await npm.exec('config', ['delete', 'access'])

  t.equal(npm.config.get('access', 'global'), undefined, 'access should be defaulted')

  const contents = await fs.readFile(join(globalPrefix, 'etc/npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.not(rc.access, 'access is not set')
})

t.test('config delete key --global', async t => {
  const { npm, globalPrefix } = await loadMockNpm(t, {
    globalPrefixDir: {
      etc: {
        npmrc: 'access=public\nall=true',
      },
    },
    config: {
      global: true,
    },
  })

  await npm.exec('config', ['delete', 'access'])

  t.equal(npm.config.get('access', 'global'), undefined, 'access should no longer be set')

  const contents = await fs.readFile(join(globalPrefix, 'etc/npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.not(rc.access, 'access is not set')
})

t.test('config set invalid option', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('config', ['set', 'nonexistantconfigoption', 'something']),
    /not a valid npm option/
  )
})

t.test('config set deprecated option', async t => {
  const { npm } = await loadMockNpm(t)
  await t.rejects(
    npm.exec('config', ['set', 'shrinkwrap', 'true']),
    /deprecated/
  )
})

t.test('config set nerf-darted option', async t => {
  const { npm } = await loadMockNpm(t)
  await npm.exec('config', ['set', '//npm.pkg.github.com/:_authToken', '0xdeadbeef'])
  t.equal(
    npm.config.get('//npm.pkg.github.com/:_authToken'),
    '0xdeadbeef',
    'nerf-darted config is set'
  )
})

t.test('config set scoped optoin', async t => {
  const { npm } = await loadMockNpm(t)
  await npm.exec('config', ['set', '@npm:registry', 'https://registry.npmjs.org'])
  t.equal(
    npm.config.get('@npm:registry'),
    'https://registry.npmjs.org',
    'scoped config is set'
  )
})

t.test('config set no args', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('config', ['set']),
    {
      code: 'EUSAGE',
    },
    'rejects with usage'
  )
})

t.test('config set key', async t => {
  const { npm, home } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': 'access=public',
    },
  })

  await npm.exec('config', ['set', 'access'])

  t.equal(npm.config.get('access'), null, 'set the value for access')

  await t.rejects(fs.stat(join(home, '.npmrc'), { encoding: 'utf8' }), 'removed empty config')
})

t.test('config set key value', async t => {
  const { npm, home } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': 'access=public',
    },
  })

  await npm.exec('config', ['set', 'access', 'restricted'])

  t.equal(npm.config.get('access'), 'restricted', 'set the value for access')

  const contents = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.equal(rc.access, 'restricted', 'access is set to restricted')
})

t.test('config set key value with equals', async t => {
  const { npm, home } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': 'access=public',
    },
  })

  await npm.exec('config', ['set', 'access=restricted'])

  t.equal(npm.config.get('access'), 'restricted', 'set the value for access')

  const contents = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.equal(rc.access, 'restricted', 'access is set to restricted')
})

t.test('config set key1 value1 key2=value2 key3', async t => {
  const { npm, home } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': 'access=public\nall=true\naudit=true',
    },
  })

  await npm.exec('config', ['set', 'access', 'restricted', 'all=false', 'audit'])

  t.equal(npm.config.get('access'), 'restricted', 'access was set')
  t.equal(npm.config.get('all'), false, 'all was set')
  t.equal(npm.config.get('audit'), true, 'audit was unset and restored to its default')

  const contents = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.equal(rc.access, 'restricted', 'access is set to restricted')
  t.equal(rc.all, false, 'all is set to false')
  t.not(contents.includes('audit='), 'config file does not set audit')
})

t.test('config set invalid key logs warning', async t => {
  const { npm, logs, home } = await loadMockNpm(t)

  // this doesn't reject, it only logs a warning
  await npm.exec('config', ['set', 'access=foo'])
  t.equal(logs.warn[0],
    `invalid config access="foo" set in ${join(home, '.npmrc')}`,
    'logged warning'
  )
})

t.test('config set key=value --location=global', async t => {
  const { npm, globalPrefix } = await loadMockNpm(t, {
    globalPrefixDir: {
      etc: {
        npmrc: 'access=public\nall=true',
      },
    },
    config: {
      location: 'global',
    },
  })

  await npm.exec('config', ['set', 'access=restricted'])

  t.equal(npm.config.get('access', 'global'), 'restricted', 'foo should be set')

  const contents = await fs.readFile(join(globalPrefix, 'etc/npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.equal(rc.access, 'restricted', 'access is set to restricted')
})

t.test('config set key=value --global', async t => {
  const { npm, globalPrefix } = await loadMockNpm(t, {
    globalPrefixDir: {
      etc: {
        npmrc: 'access=public\nall=true',
      },
    },
    config: {
      global: true,
    },
  })

  await npm.exec('config', ['set', 'access=restricted'])

  t.equal(npm.config.get('access', 'global'), 'restricted', 'access should be set')

  const contents = await fs.readFile(join(globalPrefix, 'etc/npmrc'), { encoding: 'utf8' })
  const rc = ini.parse(contents)
  t.equal(rc.access, 'restricted', 'access is set to restricted')
})

t.test('config get no args', async t => {
  const { npm, joinedOutput, clearOutput } = await loadMockNpm(t)

  await npm.exec('config', ['get'])
  const getOutput = joinedOutput()

  clearOutput()
  await npm.exec('config', ['list'])
  const listOutput = joinedOutput()

  t.equal(listOutput, getOutput, 'get with no args outputs list')
})

t.test('config get single key', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)

  await npm.exec('config', ['get', 'all'])
  t.equal(joinedOutput(), `${npm.config.get('all')}`, 'should get the value')
})

t.test('config get multiple keys', async t => {
  const { npm, joinedOutput } = await loadMockNpm(t)

  await npm.exec('config', ['get', 'yes', 'all'])
  t.equal(joinedOutput(), `yes=${npm.config.get('yes')}\nall=${npm.config.get('all')}`)
})

t.test('config get private key', async t => {
  const { npm } = await loadMockNpm(t)

  await t.rejects(
    npm.exec('config', ['get', '_authToken']),
    /_authToken option is protected/,
    'rejects with protected string'
  )

  await t.rejects(
    npm.exec('config', ['get', 'authToken']),
    /authToken option is protected/,
    'rejects with protected string'
  )

  await t.rejects(
    npm.exec('config', ['get', '//localhost:8080/:_password']),
    /_password option is protected/,
    'rejects with protected string'
  )
})

t.test('config redacted values', async t => {
  const { npm, joinedOutput, clearOutput } = await loadMockNpm(t)

  await npm.exec('config', ['set', 'proxy', 'https://proxy.npmjs.org/'])
  await npm.exec('config', ['get', 'proxy'])

  t.equal(joinedOutput(), 'https://proxy.npmjs.org/')
  clearOutput()

  await npm.exec('config', ['set', 'proxy', 'https://u:password@proxy.npmjs.org/'])

  await t.rejects(npm.exec('config', ['get', 'proxy']), /proxy option is protected/)

  await npm.exec('config', ['ls'])

  t.match(joinedOutput(), 'proxy = "https://u:***@proxy.npmjs.org/"')
  clearOutput()
})

t.test('config edit', async t => {
  const EDITOR = 'vim'
  const editor = spawk.spawn(EDITOR).exit(0)

  const { npm, home } = await loadMockNpm(t, {
    homeDir: {
      '.npmrc': 'foo=bar\nbar=baz',
    },
    config: {
      editor: EDITOR,
    },
  })

  await npm.exec('config', ['edit'])

  t.ok(editor.called, 'editor was spawned')
  t.same(
    editor.calledWith.args,
    [join(home, '.npmrc')],
    'editor opened the user config file'
  )

  const contents = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
  t.ok(contents.includes('foo=bar'), 'kept foo')
  t.ok(contents.includes('bar=baz'), 'kept bar')
  t.ok(contents.includes('shown below with default values'), 'appends defaults to file')
})

t.test('config edit - editor exits non-0', async t => {
  const EDITOR = 'vim'
  const editor = spawk.spawn(EDITOR).exit(1)

  const { npm, home } = await loadMockNpm(t, {
    config: {
      editor: EDITOR,
    },
  })

  await t.rejects(
    npm.exec('config', ['edit']),
    {
      message: 'editor process exited with code: 1',
    },
    'rejects with error about editor code'
  )

  t.ok(editor.called, 'editor was spawned')
  t.same(
    editor.calledWith.args,
    [join(home, '.npmrc')],
    'editor opened the user config file'
  )
})

t.test('config fix', (t) => {
  t.test('no problems', async (t) => {
    const { npm, joinedOutput } = await loadMockNpm(t, {
      homeDir: {
        '.npmrc': '',
      },
    })

    await npm.exec('config', ['fix'])
    t.equal(joinedOutput(), '', 'printed nothing')
  })

  t.test('repairs all configs by default', async (t) => {
    const { npm, home, globalPrefix, joinedOutput } = await loadMockNpm(t, {
      globalPrefixDir: {
        etc: {
          npmrc: '_authtoken=notatoken\n_authToken=afaketoken',
        },
      },
      homeDir: {
        '.npmrc': '_authtoken=thisisinvalid\n_auth=beef',
      },
    })

    const registry = `//registry.npmjs.org/`

    await npm.exec('config', ['fix'])

    // global config fixes
    t.match(joinedOutput(), '`_authtoken` deleted from global config',
      'output has deleted global _authtoken')
    t.match(joinedOutput(), `\`_authToken\` renamed to \`${registry}:_authToken\` in global config`,
      'output has renamed global _authToken')
    t.not(npm.config.get('_authtoken', 'global'), '_authtoken is not set globally')
    t.not(npm.config.get('_authToken', 'global'), '_authToken is not set globally')
    t.equal(npm.config.get(`${registry}:_authToken`, 'global'), 'afaketoken',
      'global _authToken was scoped')
    const globalConfig = await fs.readFile(join(globalPrefix, 'etc/npmrc'), { encoding: 'utf8' })
    t.equal(globalConfig, `${registry}:_authToken=afaketoken\n`, 'global config was written')

    // user config fixes
    t.match(joinedOutput(), '`_authtoken` deleted from user config',
      'output has deleted user _authtoken')
    t.match(joinedOutput(), `\`_auth\` renamed to \`${registry}:_auth\` in user config`,
      'output has renamed user _auth')
    t.not(npm.config.get('_authtoken', 'user'), '_authtoken is not set in user config')
    t.not(npm.config.get('_auth'), '_auth is not set in user config')
    t.equal(npm.config.get(`${registry}:_auth`, 'user'), 'beef', 'user _auth was scoped')
    const userConfig = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
    t.equal(userConfig, `${registry}:_auth=beef\n`, 'user config was written')
  })

  t.test('repairs only the config specified by --location if asked', async (t) => {
    const { npm, home, globalPrefix, joinedOutput } = await loadMockNpm(t, {
      globalPrefixDir: {
        etc: {
          npmrc: '_authtoken=notatoken\n_authToken=afaketoken',
        },
      },
      homeDir: {
        '.npmrc': '_authtoken=thisisinvalid\n_auth=beef',
      },
      config: {
        location: 'user',
      },
    })
    const registry = `//registry.npmjs.org/`

    await npm.exec('config', ['fix'])

    // global config should be untouched
    t.notMatch(joinedOutput(), '`_authtoken` deleted from global',
      'output has deleted global _authtoken')
    t.notMatch(joinedOutput(), `\`_authToken\` renamed to \`${registry}:_authToken\` in global`,
      'output has renamed global _authToken')
    t.equal(npm.config.get('_authtoken', 'global'), 'notatoken', 'global _authtoken untouched')
    t.equal(npm.config.get('_authToken', 'global'), 'afaketoken', 'global _authToken untouched')
    t.not(npm.config.get(`${registry}:_authToken`, 'global'), 'global _authToken not scoped')
    const globalConfig = await fs.readFile(join(globalPrefix, 'etc/npmrc'), { encoding: 'utf8' })
    t.equal(globalConfig, '_authtoken=notatoken\n_authToken=afaketoken',
      'global config was not written')

    // user config fixes
    t.match(joinedOutput(), '`_authtoken` deleted from user',
      'output has deleted user _authtoken')
    t.match(joinedOutput(), `\`_auth\` renamed to \`${registry}:_auth\` in user`,
      'output has renamed user _auth')
    t.not(npm.config.get('_authtoken', 'user'), '_authtoken is not set in user config')
    t.not(npm.config.get('_auth', 'user'), '_auth is not set in user config')
    t.equal(npm.config.get(`${registry}:_auth`, 'user'), 'beef', 'user _auth was scoped')
    const userConfig = await fs.readFile(join(home, '.npmrc'), { encoding: 'utf8' })
    t.equal(userConfig, `${registry}:_auth=beef\n`, 'user config was written')
  })

  t.end()
})

t.test('completion', async t => {
  const { config, npm } = await loadMockNpm(t, { command: 'config' })

  const allKeys = Object.keys(npm.config.definitions)

  const testComp = async (argv, expect, msg) => {
    const options = Array.isArray(argv) ? {
      conf: {
        argv: {
          remain: ['config', ...argv],
        },
      },
    } : argv
    options.conf.argv.remain.unshift('npm')
    const res = await config.completion(options)
    t.strictSame(res, expect, msg ?? argv.join(' '))
  }

  await testComp([], ['get', 'set', 'delete', 'ls', 'rm', 'edit', 'fix', 'list'])
  await testComp(['set', 'foo'], [])
  await testComp(['get'], allKeys)
  await testComp(['set'], allKeys)
  await testComp(['delete'], allKeys)
  await testComp(['rm'], allKeys)
  await testComp(['edit'], [])
  await testComp(['fix'], [])
  await testComp(['list'], [])
  await testComp(['ls'], [])

  await testComp({
    conf: { argv: { remain: ['get'] } },
  }, allKeys, 'also works for just npm get')

  await testComp({
    partialWord: 'l',
    conf: { argv: { remain: ['config'] } },
  }, ['get', 'set', 'delete', 'ls', 'rm', 'edit', 'fix'], 'and works on partials')
})
