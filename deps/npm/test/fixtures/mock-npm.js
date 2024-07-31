const os = require('node:os')
const fs = require('node:fs').promises
const fsSync = require('node:fs')
const path = require('node:path')
const tap = require('tap')
const mockLogs = require('./mock-logs.js')
const mockGlobals = require('@npmcli/mock-globals')
const tmock = require('./tmock')
const MockRegistry = require('@npmcli/mock-registry')
const defExitCode = process.exitCode

const changeDir = (dir) => {
  if (dir) {
    const cwd = process.cwd()
    process.chdir(dir)
    return () => process.chdir(cwd)
  }
  return () => {}
}

const setGlobalNodeModules = (globalDir) => {
  const updateSymlinks = (obj, visit) => {
    for (const [key, value] of Object.entries(obj)) {
      if (/Fixture<symlink>/.test(value.toString())) {
        obj[key] = tap.fixture('symlink', path.join('..', value.content))
      } else if (typeof value === 'object') {
        obj[key] = updateSymlinks(value, visit)
      }
    }
    return obj
  }

  if (globalDir.lib) {
    throw new Error('`globalPrefixDir` should not have a top-level `lib/` directory, only a ' +
      'top-level `node_modules/` dir that gets set in the correct location based on platform. ' +
      `Received the following top level entries: ${Object.keys(globalDir).join(', ')}.`
    )
  }

  if (process.platform !== 'win32' && globalDir.node_modules) {
    const { node_modules: nm, ...rest } = globalDir
    return {
      ...rest,
      lib: { node_modules: updateSymlinks(nm) },
    }
  }

  return globalDir
}

const buildMocks = (t, mocks) => {
  const allMocks = { ...mocks }
  // The definitions must be mocked since they are a singleton that reads from
  // process and environs to build defaults in order to break the requiure
  // cache. We also need to mock them with any mocks that were passed in for the
  // test in case those mocks are for things like ci-info which is used there.
  const definitions = '@npmcli/config/lib/definitions'
  allMocks[definitions] = tmock(t, definitions, allMocks)
  return allMocks
}

const getMockNpm = async (t, { mocks, init, load, npm: npmOpts }) => {
  const { streams, logs } = mockLogs()
  const allMocks = buildMocks(t, mocks)
  const Npm = tmock(t, '{LIB}/npm.js', allMocks)

  class MockNpm extends Npm {
    constructor (opts) {
      super({
        ...opts,
        ...streams,
        ...npmOpts,
      })
    }

    async load () {
      const res = await super.load()
      // Wait for any promises (currently only log file cleaning) to be
      // done before returning from load in tests. This helps create more
      // deterministic testing behavior because in reality that promise
      // is left hanging on purpose as a best-effort and the process gets
      // closed regardless of if it has finished or not.
      await Promise.all(this.unrefPromises)
      return res
    }
  }

  const npm = init ? new MockNpm() : null
  if (npm && load) {
    await npm.load()
  }

  return {
    Npm: MockNpm,
    npm,
    ...logs,
  }
}

const mockNpms = new Map()

const setupMockNpm = async (t, {
  init = true,
  load = init,
  // preload a command
  command = null, // string name of the command
  exec = null, // optionally exec the command before returning
  // test dirs
  prefixDir = {},
  homeDir = {},
  cacheDir = {},
  globalPrefixDir = { node_modules: {} },
  otherDirs = {},
  chdir = ({ prefix }) => prefix,
  // setup config, env vars, mocks, npm opts
  config: _config = {},
  mocks = {},
  globals = {},
  npm: npmOpts = {},
  argv: rawArgv = [],
} = {}) => {
  // easy to accidentally forget to pass in tap
  if (!(t instanceof tap.Test)) {
    throw new Error('first argument must be a tap instance')
  }

  // mockNpm is designed to only be run once per test chain so we assign it to
  // the test in the cache and error if it is attempted to run again
  let tapInstance = t
  while (tapInstance) {
    if (mockNpms.has(tapInstance)) {
      throw new Error('mockNpm can only be called once in each t.test chain')
    }
    tapInstance = tapInstance.parent
  }
  mockNpms.set(t, true)

  if (!init && load) {
    throw new Error('cant `load` without `init`')
  }

  // These are globals manipulated by npm itself that we need to reset to their
  // original values between tests
  const npmEnvs = Object.keys(process.env).filter(k => k.startsWith('npm_'))
  mockGlobals(t, {
    process: {
      title: process.title,
      execPath: process.execPath,
      env: {
        NODE_ENV: process.env.NODE_ENV,
        COLOR: process.env.COLOR,
        // further, these are npm controlled envs that we need to zero out before
        // before the test. setting them to undefined ensures they are not set and
        // also returned to their original value after the test
        ...npmEnvs.reduce((acc, k) => {
          acc[k] = undefined
          return acc
        }, {}),
      },
    },
  })

  const dir = t.testdir({
    home: homeDir,
    prefix: prefixDir,
    cache: cacheDir,
    global: setGlobalNodeModules(globalPrefixDir),
    other: otherDirs,
  })

  const dirs = {
    testdir: dir,
    prefix: path.join(dir, 'prefix'),
    cache: path.join(dir, 'cache'),
    globalPrefix: path.join(dir, 'global'),
    home: path.join(dir, 'home'),
    other: path.join(dir, 'other'),
  }

  // Option objects can also be functions that are called with all the dir paths
  // so they can be used to set configs that need to be based on paths
  const withDirs = (v) => typeof v === 'function' ? v(dirs) : v

  const teardownDir = changeDir(withDirs(chdir))

  const defaultConfigs = {
    // We want to fail fast when writing tests. Default this to 0 unless it was
    // explicitly set in a test.
    'fetch-retries': 0,
    cache: dirs.cache,
    // This will give us all the loglevels including timing in a non-colorized way
    // so we can easily assert their contents. Individual tests can overwrite these
    // with my passing in configs if they need to test other forms of output.
    loglevel: 'silly',
    color: false,
  }

  const { argv, env, config } = Object.entries({ ...defaultConfigs, ...withDirs(_config) })
    .reduce((acc, [key, value]) => {
      // nerfdart configs passed in need to be set via env var instead of argv
      // and quoted with `"` so mock globals will ignore that it contains dots
      if (key.startsWith('//')) {
        acc.env[`process.env."npm_config_${key}"`] = value
      } else if (value !== undefined) {
        const values = [].concat(value)
        acc.argv.push(...values.flatMap(v => v === '' ? `--${key}` : `--${key}=${v.toString()}`))
      }
      if (value !== undefined) {
        acc.config[key] = value
      }
      return acc
    }, { argv: [...rawArgv], env: {}, config: {} })

  const mockedGlobals = mockGlobals(t, {
    'process.env.HOME': dirs.home,
    // global prefix cannot be (easily) set via argv so this is the easiest way
    // to set it that also closely mimics the behavior a user would see since it
    // will already be set while `npm.load()` is being run
    // Note that this only sets the global prefix and the prefix is set via chdir
    'process.env.PREFIX': dirs.globalPrefix,
    ...withDirs(globals),
    ...env,
  })

  const { npm, ...mockNpm } = await getMockNpm(t, {
    init,
    load,
    mocks: withDirs(mocks),
    npm: {
      argv: command ? [command, ...argv] : argv,
      excludeNpmCwd: true,
      ...withDirs(npmOpts),
    },
  })

  if (config.omit?.includes('prod')) {
    // XXX(HACK): --omit=prod is not a valid config according to the definitions but
    // it was being hacked in via flatOptions for older tests so this is to
    // preserve that behavior and reduce churn in the snapshots. this should be
    // removed or fixed in the future
    npm.flatOptions.omit.push('prod')
  }

  t.teardown(() => {
    if (npm) {
      npm.unload()
    }
    // only set exitCode back if we're passing tests
    if (t.passing()) {
      process.exitCode = defExitCode
    }
    teardownDir()
  })

  const mockCommand = {}
  if (command) {
    const Cmd = mockNpm.Npm.cmd(command)
    mockCommand.cmd = new Cmd(npm)
    mockCommand[command] = {
      usage: Cmd.describeUsage,
      exec: (args) => npm.exec(command, args),
      completion: (args) => Cmd.completion(args, npm),
    }
    if (exec) {
      await mockCommand[command].exec(exec === true ? [] : exec)
      // assign string output to the command now that we have it
      // for easier testing
      mockCommand[command].output = mockNpm.joinedOutput()
    }
  }

  return {
    npm,
    mockedGlobals,
    ...mockNpm,
    ...dirs,
    ...mockCommand,
    debugFile: async () => {
      const readFiles = npm.logFiles.map(f => fs.readFile(f))
      const logFiles = await Promise.all(readFiles)
      return logFiles
        .flatMap((d) => d.toString().trim().split(os.EOL))
        .filter(Boolean)
        .join('\n')
    },
    timingFile: async () => {
      const data = await fs.readFile(npm.logPath + 'timing.json', 'utf8')
      return JSON.parse(data)
    },
  }
}

const loadNpmWithRegistry = async (t, opts) => {
  const mock = await setupMockNpm(t, opts)
  const registry = new MockRegistry({
    tap: t,
    registry: mock.npm.config.get('registry'),
    strict: true,
  })

  const fileShouldExist = (filePath) => {
    t.equal(
      fsSync.existsSync(path.join(mock.npm.prefix, filePath)), true, `${filePath} should exist`
    )
  }

  const fileShouldNotExist = (filePath) => {
    t.equal(
      fsSync.existsSync(path.join(mock.npm.prefix, filePath)), false, `${filePath} should not exist`
    )
  }

  const packageVersionMatches = (filePath, version) => {
    t.equal(
      JSON.parse(fsSync.readFileSync(path.join(mock.npm.prefix, filePath), 'utf8')).version, version
    )
  }

  const packageInstalled = (target) => {
    const spec = path.basename(target)
    const dirname = path.dirname(target)
    const [name, version = '1.0.0'] = spec.split('@')
    fileShouldNotExist(`${dirname}/${name}/${name}@${version}.txt`)
    packageVersionMatches(`${dirname}/${name}/package.json`, version)
    fileShouldExist(`${dirname}/${name}/index.js`)
  }

  const packageMissing = (target) => {
    const spec = path.basename(target)
    const dirname = path.dirname(target)
    const [name, version = '1.0.0'] = spec.split('@')
    fileShouldNotExist(`${dirname}/${name}/${name}@${version}.txt`)
    fileShouldNotExist(`${dirname}/${name}/package.json`)
    fileShouldNotExist(`${dirname}/${name}/index.js`)
  }

  const packageDirty = (target) => {
    const spec = path.basename(target)
    const dirname = path.dirname(target)
    const [name, version = '1.0.0'] = spec.split('@')
    fileShouldExist(`${dirname}/${name}/${name}@${version}.txt`)
    packageVersionMatches(`${dirname}/${name}/package.json`, version)
    fileShouldNotExist(`${dirname}/${name}/index.js`)
  }

  const assert = {
    fileShouldExist,
    fileShouldNotExist,
    packageVersionMatches,
    packageInstalled,
    packageMissing,
    packageDirty,
  }

  return { registry, assert, ...mock }
}

/** breaks down a spec "abbrev@1.1.1" into different parts for mocking */
function dependencyDetails (spec, opt = {}) {
  const [name, version = '1.0.0'] = spec.split('@')
  const { parent, hoist = true, ws, clean = true } = opt
  const modulePathPrefix = !hoist && parent ? `${parent}/` : ''
  const modulePath = `${modulePathPrefix}node_modules/${name}`
  const resolved = `https://registry.npmjs.org/${name}/-/${name}-${version}.tgz`
  // deps
  const wsEntries = Object.entries({ ...ws })
  const depsMap = wsEntries.map(([s, o]) => dependencyDetails(s, { ...o, parent: name }))
  const dependencies = Object.assign({}, ...depsMap.map(d => d.packageDependency))
  const spreadDependencies = depsMap.length ? { dependencies } : {}
  // package and lock objects
  const packageDependency = { [name]: version }
  const packageLockEntry = { [modulePath]: { version, resolved } }
  const packageLockLink = { [modulePath]: { resolved: name, link: true } }
  const packageLockLocal = { [name]: { version, dependencies } }
  // build package.js
  const packageJSON = { name, version, ...spreadDependencies }
  const packageJSONString = JSON.stringify(packageJSON)
  const packageJSONFile = { 'package.json': packageJSONString }
  // build index.js
  const indexJSString = 'module.exports = "hello world"'
  const indexJSFile = { 'index.js': indexJSString }
  // tarball
  const packageFiles = { ...packageJSONFile, ...indexJSFile }
  const nodeModules = Object.assign({}, ...depsMap.map(d => d.hoist ? {} : d.dirtyOrCleanDir))
  const nodeModulesDir = { node_modules: nodeModules }
  const packageDir = { [name]: { ...packageFiles, ...nodeModulesDir } }
  const tarballDir = { [`${name}@${version}`]: packageFiles }
  // dirty files
  const dirtyFile = { [`${name}@${version}.txt`]: 'dirty file' }
  const dirtyFiles = { ...packageJSONFile, ...dirtyFile }
  const dirtyDir = { [name]: dirtyFiles }
  const dirtyOrCleanDir = clean ? {} : dirtyDir

  return {
    packageDependency,
    hoist,
    depsMap,
    dirtyOrCleanDir,
    tarballDir,
    packageDir,
    packageLockEntry,
    packageLockLink,
    packageLockLocal,
  }
}

function workspaceMock (t, opts) {
  const toObject = [(a, c) => ({ ...a, ...c }), {}]
  const { workspaces: workspacesDef, ...rest } = { clean: true, ...opts }
  const workspaces = Object.fromEntries(Object.entries(workspacesDef).map(([name, ws]) => {
    return [name, Object.fromEntries(Object.entries(ws).map(([wsPackageDep, wsPackageDepOpts]) => {
      return [wsPackageDep, { ...rest, ...wsPackageDepOpts }]
    }))]
  }))
  const root = 'workspace-root'
  const version = '1.0.0'
  const names = Object.keys(workspaces)
  const ws = Object.entries(workspaces).map(([name, _ws]) => dependencyDetails(name, { ws: _ws }))
  const deps = ws.map(({ depsMap }) => depsMap).flat()
  const tarballs = deps.map(w => w.tarballDir).reduce(...toObject)
  const symlinks = names
    .map((name) => ({ [name]: t.fixture('symlink', `../${name}`) })).reduce(...toObject)
  const hoisted = deps.filter(d => d.hoist).map(w => w.dirtyOrCleanDir).reduce(...toObject)
  const workspaceFolders = ws.map(w => w.packageDir).reduce(...toObject)
  const packageJSON = { name: root, version, workspaces: names }
  const packageLockJSON = ({
    name: root,
    version,
    lockfileVersion: 3,
    requires: true,
    packages: {
      '': { name: root, version, workspaces: names },
      ...deps.filter(d => d.hoist).map(d => d.packageLockEntry).reduce(...toObject),
      ...ws.map(d => d.packageLockEntry).flat().reduce(...toObject),
      ...ws.map(d => d.packageLockLink).flat().reduce(...toObject),
      ...ws.map(d => d.packageLockLocal).flat().reduce(...toObject),
      ...deps.filter(d => !d.hoist).map(d => d.packageLockEntry).reduce(...toObject),
    },
  })
  return {
    tarballs,
    node_modules: {
      ...hoisted,
      ...symlinks,
    },
    'package-lock.json': JSON.stringify(packageLockJSON),
    'package.json': JSON.stringify(packageJSON),
    ...workspaceFolders,
  }
}

module.exports = setupMockNpm
module.exports.load = setupMockNpm
module.exports.setGlobalNodeModules = setGlobalNodeModules
module.exports.loadNpmWithRegistry = loadNpmWithRegistry
module.exports.workspaceMock = workspaceMock
