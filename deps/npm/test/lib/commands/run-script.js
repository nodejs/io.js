const t = require('tap')
const { resolve } = require('node:path')
const realRunScript = require('@npmcli/run-script')
const mockNpm = require('../../fixtures/mock-npm')
const { cleanCwd } = require('../../fixtures/clean-snapshot')

const mockRs = async (t, { windows = false, runScript, ...opts } = {}) => {
  let RUN_SCRIPTS = []

  t.afterEach(() => RUN_SCRIPTS = [])

  const mock = await mockNpm(t, {
    ...opts,
    command: 'run-script',
    mocks: {
      '@npmcli/run-script': Object.assign(
        async rs => {
          if (runScript) {
            await runScript(rs)
          }
          RUN_SCRIPTS.push(rs)
        },
        realRunScript
      ),
      '{LIB}/utils/is-windows.js': { isWindowsShell: windows },
    },
  })

  return {
    ...mock,
    RUN_SCRIPTS: () => RUN_SCRIPTS,
    runScript: mock['run-script'],
    cleanLogs: () => mock.logs.error.map(cleanCwd).join('\n'),
  }
}

t.test('completion', async t => {
  const completion = async (t, remain, pkg, isFish = false) => {
    const { runScript } = await mockRs(t,
      pkg ? { prefixDir: { 'package.json': JSON.stringify(pkg) } } : {}
    )
    return runScript.completion({ conf: { argv: { remain } }, isFish })
  }

  t.test('already have a script name', async t => {
    const res = await completion(t, ['npm', 'run', 'x'])
    t.equal(res, undefined)
  })
  t.test('no package.json', async t => {
    const res = await completion(t, ['npm', 'run'])
    t.strictSame(res, [])
  })
  t.test('has package.json, no scripts', async t => {
    const res = await completion(t, ['npm', 'run'], {})
    t.strictSame(res, [])
  })
  t.test('has package.json, with scripts', async t => {
    const res = await completion(t, ['npm', 'run'], {
      scripts: { hello: 'echo hello', world: 'echo world' },
    })
    t.strictSame(res, ['hello', 'world'])
  })

  t.test('fish shell', async t => {
    const res = await completion(t, ['npm', 'run'], {
      scripts: { hello: 'echo hello', world: 'echo world' },
    }, true)
    t.strictSame(res, ['hello\techo hello', 'world\techo world'])
  })
})

t.test('fail if no package.json', async t => {
  const { runScript } = await mockRs(t)
  await t.rejects(runScript.exec([]), { code: 'ENOENT' })
  await t.rejects(runScript.exec(['test']), { code: 'ENOENT' })
})

t.test('default env, start, and restart scripts', async t => {
  const { npm, runScript, RUN_SCRIPTS } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({ name: 'x', version: '1.2.3' }),
      'server.js': 'console.log("hello, world")',
    },
  })

  t.test('start', async t => {
    await runScript.exec(['start'])
    t.match(RUN_SCRIPTS(), [
      {
        path: npm.localPrefix,
        args: [],
        scriptShell: undefined,
        stdio: 'inherit',
        pkg: { name: 'x', version: '1.2.3', _id: 'x@1.2.3', scripts: {} },
        event: 'start',
      },
    ])
  })

  t.test('env', async t => {
    await runScript.exec(['env'])
    t.match(RUN_SCRIPTS(), [
      {
        path: npm.localPrefix,
        args: [],
        scriptShell: undefined,
        stdio: 'inherit',
        pkg: {
          name: 'x',
          version: '1.2.3',
          _id: 'x@1.2.3',
          scripts: {
            env: 'env',
          },
        },
        event: 'env',
      },
    ])
  })

  t.test('restart', async t => {
    await runScript.exec(['restart'])

    t.match(RUN_SCRIPTS(), [
      {
        path: npm.localPrefix,
        args: [],
        scriptShell: undefined,
        stdio: 'inherit',
        pkg: {
          name: 'x',
          version: '1.2.3',
          _id: 'x@1.2.3',
          scripts: {
            restart: 'npm stop --if-present && npm start',
          },
        },
        event: 'restart',
      },
    ])
  })
})

t.test('default windows env', async t => {
  const { npm, runScript, RUN_SCRIPTS } = await mockRs(t, {
    windows: true,
    prefixDir: {
      'package.json': JSON.stringify({ name: 'x', version: '1.2.3' }),
      'server.js': 'console.log("hello, world")',
    },
  })
  await runScript.exec(['env'])
  t.match(RUN_SCRIPTS(), [
    {
      path: npm.localPrefix,
      args: [],
      scriptShell: undefined,
      stdio: 'inherit',
      pkg: {
        name: 'x',
        version: '1.2.3',
        _id: 'x@1.2.3',
        scripts: {
          env: 'SET',
        },
      },
      event: 'env',
    },
  ])
})

t.test('non-default env script', async t => {
  const { npm, runScript, RUN_SCRIPTS } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
        scripts: {
          env: 'hello',
        },
      }),
    },
  })

  t.test('env', async t => {
    await runScript.exec(['env'])
    t.match(RUN_SCRIPTS(), [
      {
        path: npm.localPrefix,
        args: [],
        scriptShell: undefined,
        stdio: 'inherit',
        pkg: {
          name: 'x',
          version: '1.2.3',
          _id: 'x@1.2.3',
          scripts: {
            env: 'hello',
          },
        },
        event: 'env',
      },
    ])
  })
})

t.test('non-default env script windows', async t => {
  const { npm, runScript, RUN_SCRIPTS } = await mockRs(t, {
    windows: true,
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
        scripts: {
          env: 'hello',
        },
      }),
    },
  })

  await runScript.exec(['env'])

  t.match(RUN_SCRIPTS(), [
    {
      path: npm.localPrefix,
      args: [],
      scriptShell: undefined,
      stdio: 'inherit',
      pkg: {
        name: 'x',
        version: '1.2.3',
        _id: 'x@1.2.3',
        scripts: {
          env: 'hello',
        },
      },
      event: 'env',
    },
  ])
})

t.test('try to run missing script', async t => {
  t.test('errors', async t => {
    const { runScript } = await mockRs(t, {
      prefixDir: {
        'package.json': JSON.stringify({
          scripts: { hello: 'world' },
          bin: { goodnight: 'moon' },
        }),
      },
    })
    t.test('no suggestions', async t => {
      await t.rejects(runScript.exec(['notevenclose']), 'Missing script: "notevenclose"')
    })
    t.test('script suggestions', async t => {
      await t.rejects(runScript.exec(['helo']), /Missing script: "helo"/)
      await t.rejects(runScript.exec(['helo']), /npm run hello/)
    })
    t.test('bin suggestions', async t => {
      await t.rejects(runScript.exec(['goodneght']), /Missing script: "goodneght"/)
      await t.rejects(runScript.exec(['goodneght']), /npm exec goodnight/)
    })
  })

  t.test('with --if-present', async t => {
    const { runScript, RUN_SCRIPTS } = await mockRs(t, {
      config: { 'if-present': true },
      prefixDir: {
        'package.json': JSON.stringify({
          scripts: { hello: 'world' },
          bin: { goodnight: 'moon' },
        }),
      },
    })
    await runScript.exec(['goodbye'])
    t.strictSame(RUN_SCRIPTS(), [], 'did not try to run anything')
  })
})

t.test('run pre/post hooks', async t => {
  const { npm, runScript, RUN_SCRIPTS } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
        scripts: {
          preenv: 'echo before the env',
          postenv: 'echo after the env',
        },
      }),
    },
  })

  await runScript.exec(['env'])

  t.match(RUN_SCRIPTS(), [
    { event: 'preenv' },
    {
      path: npm.localPrefix,
      args: [],
      scriptShell: undefined,
      stdio: 'inherit',
      pkg: {
        name: 'x',
        version: '1.2.3',
        _id: 'x@1.2.3',
        scripts: {
          env: 'env',
        },
      },
      event: 'env',
    },
    { event: 'postenv' },
  ])
})

t.test('skip pre/post hooks when using ignoreScripts', async t => {
  const { npm, runScript, RUN_SCRIPTS } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
        scripts: {
          preenv: 'echo before the env',
          postenv: 'echo after the env',
        },
      }),
    },
    config: { 'ignore-scripts': true },
  })

  await runScript.exec(['env'])

  t.same(RUN_SCRIPTS(), [
    {
      path: npm.localPrefix,
      args: [],
      scriptShell: undefined,
      stdio: 'inherit',
      pkg: {
        name: 'x',
        version: '1.2.3',
        _id: 'x@1.2.3',
        scripts: {
          preenv: 'echo before the env',
          postenv: 'echo after the env',
          env: 'env',
        },
      },
      event: 'env',
    },
  ])
})

t.test('run silent', async t => {
  const { npm, runScript, RUN_SCRIPTS } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
        scripts: {
          preenv: 'echo before the env',
          postenv: 'echo after the env',
        },
      }),
    },
    config: { silent: true },
  })

  await runScript.exec(['env'])
  t.match(RUN_SCRIPTS(), [
    {
      event: 'preenv',
      stdio: 'inherit',
    },
    {
      path: npm.localPrefix,
      args: [],
      scriptShell: undefined,
      stdio: 'inherit',
      pkg: {
        name: 'x',
        version: '1.2.3',
        _id: 'x@1.2.3',
        scripts: {
          env: 'env',
        },
      },
      event: 'env',
    },
    {
      event: 'postenv',
      stdio: 'inherit',
    },
  ])
})

t.test('list scripts', async t => {
  const scripts = {
    test: 'exit 2',
    start: 'node server.js',
    stop: 'node kill-server.js',
    preenv: 'echo before the env',
    postenv: 'echo after the env',
  }

  const mockList = async (t, config = {}) => {
    const mock = await mockRs(t, {
      prefixDir: {
        'package.json': JSON.stringify({
          name: 'x',
          version: '1.2.3',
          scripts,
        }),
      },
      config,
    })

    await mock.runScript.exec([])

    return mock.joinedOutput()
  }

  t.test('no args', async t => {
    const output = await mockList(t)
    t.matchSnapshot(output, 'basic report')
  })

  t.test('silent', async t => {
    const output = await mockList(t, { silent: true })
    t.strictSame(output, '')
  })
  t.test('warn json', async t => {
    const output = await mockList(t, { json: true })
    t.matchSnapshot(output, 'json report')
  })

  t.test('parseable', async t => {
    const output = await mockList(t, { parseable: true })
    t.matchSnapshot(output)
  })
})

t.test('list scripts when no scripts', async t => {
  const { runScript, joinedOutput } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
      }),
    },
  })

  await runScript.exec([])
  t.strictSame(joinedOutput(), '', 'nothing to report')
})

t.test('list scripts, only commands', async t => {
  const { runScript, joinedOutput } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
        scripts: { preversion: 'echo doing the version dance' },
      }),
    },
  })

  await runScript.exec([])
  t.matchSnapshot(joinedOutput())
})

t.test('list scripts, only non-commands', async t => {
  const { runScript, joinedOutput } = await mockRs(t, {
    prefixDir: {
      'package.json': JSON.stringify({
        name: 'x',
        version: '1.2.3',
        scripts: { glorp: 'echo doing the glerp glop' },
      }),
    },
  })

  await runScript.exec([])
  t.matchSnapshot(joinedOutput())
})

t.test('workspaces', async t => {
  const mockWorkspaces = async (t, {
    runScript,
    prefixDir,
    workspaces = true,
    exec = [],
    ...config
  } = {}) => {
    const mock = await mockRs(t, {
      prefixDir: prefixDir || {
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
      },
      config: {
        ...Array.isArray(workspaces) ? { workspace: workspaces } : { workspaces },
        ...config,
      },
      runScript,
    })
    if (exec) {
      await mock.runScript.exec(exec)
    }
    return mock
  }

  t.test('list all scripts', async t => {
    const { joinedOutput } = await mockWorkspaces(t)
    t.matchSnapshot(joinedOutput())
  })

  t.test('list regular scripts, filtered by name', async t => {
    const { joinedOutput } = await mockWorkspaces(t, { workspaces: ['a', 'b'] })
    t.matchSnapshot(joinedOutput())
  })

  t.test('list regular scripts, filtered by path', async t => {
    const { joinedOutput } = await mockWorkspaces(t, { workspaces: ['./packages/a'] })
    t.matchSnapshot(joinedOutput())
  })

  t.test('list regular scripts, filtered by parent folder', async t => {
    const { joinedOutput } = await mockWorkspaces(t, { workspaces: ['./packages'] })
    t.matchSnapshot(joinedOutput())
  })

  t.test('list all scripts with colors', async t => {
    const { joinedOutput } = await mockWorkspaces(t, { color: 'always' })
    t.matchSnapshot(joinedOutput())
  })

  t.test('list all scripts --json', async t => {
    const { joinedOutput } = await mockWorkspaces(t, { json: true })
    t.matchSnapshot(joinedOutput())
  })

  t.test('list all scripts --parseable', async t => {
    const { joinedOutput } = await mockWorkspaces(t, { parseable: true })
    t.matchSnapshot(joinedOutput())
  })

  t.test('list no scripts --loglevel=silent', async t => {
    const { joinedOutput } = await mockWorkspaces(t, { silent: true })
    t.strictSame(joinedOutput(), '')
  })

  t.test('run scripts across all workspaces', async t => {
    const { npm, RUN_SCRIPTS } = await mockWorkspaces(t, { exec: ['test'] })

    t.match(RUN_SCRIPTS(), [
      {
        path: resolve(npm.localPrefix, 'packages/c'),
        pkg: { name: 'c', version: '1.0.0' },
        event: 'test',
      },
      {
        path: resolve(npm.localPrefix, 'packages/c'),
        pkg: { name: 'c', version: '1.0.0' },
        event: 'posttest',
      },
      {
        path: resolve(npm.localPrefix, 'packages/d'),
        pkg: { name: 'd', version: '1.0.0' },
        event: 'test',
      },
      {
        path: resolve(npm.localPrefix, 'packages/d'),
        pkg: { name: 'd', version: '1.0.0' },
        event: 'posttest',
      },
      {
        path: resolve(npm.localPrefix, 'packages/e'),
        pkg: { name: 'e' },
        event: 'test',
      },
    ])
  })

  t.test('missing scripts in all workspaces', async t => {
    const { runScript, RUN_SCRIPTS, cleanLogs } = await mockWorkspaces(t, { exec: null })

    await runScript.exec(['missing-script'])
    t.match(RUN_SCRIPTS(), [])
    t.matchSnapshot(
      cleanLogs(),
      'should log error msgs for each workspace script'
    )
  })

  t.test('missing scripts in some workspaces', async t => {
    const { RUN_SCRIPTS, cleanLogs } = await mockWorkspaces(t, {
      exec: ['test'],
      workspaces: ['a', 'b', 'c', 'd'],
    })

    t.match(RUN_SCRIPTS(), [])
    t.matchSnapshot(
      cleanLogs(),
      'should log error msgs for each workspace script'
    )
  })

  t.test('no workspaces when filtering by user args', async t => {
    await t.rejects(
      mockWorkspaces(t, { workspaces: ['foo', 'bar'] }),
      'No workspaces found:\n  --workspace=foo --workspace=bar',
      'should throw error msg'
    )
  })

  t.test('no workspaces', async t => {
    await t.rejects(
      mockWorkspaces(t, {
        prefixDir: {
          'package.json': JSON.stringify({
            name: 'foo',
            version: '1.0.0',
          }),
        },
      }),
      /No workspaces found!/,
      'should throw error msg'
    )
  })

  t.test('single failed workspace run', async t => {
    const { cleanLogs } = await mockWorkspaces(t, {
      runScript: () => {
        throw new Error('err')
      },
      exec: ['test'],
      workspaces: ['c'],
    })

    t.matchSnapshot(
      cleanLogs(),
      'should log error msgs for each workspace script'
    )
  })

  t.test('failed workspace run with succeeded runs', async t => {
    const { cleanLogs, RUN_SCRIPTS, prefix } = await mockWorkspaces(t, {
      runScript: (opts) => {
        if (opts.pkg.name === 'a') {
          throw new Error('ERR')
        }
      },
      exec: ['glorp'],
      workspaces: ['a', 'b'],
    })

    t.matchSnapshot(
      cleanLogs(),
      'should log error msgs for each workspace script'
    )

    t.match(RUN_SCRIPTS(), [
      {
        path: resolve(prefix, 'packages/b'),
        pkg: { name: 'b', version: '2.0.0' },
        event: 'glorp',
      },
    ])
  })
})
