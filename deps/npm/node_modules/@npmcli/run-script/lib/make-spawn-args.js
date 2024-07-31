/* eslint camelcase: "off" */
const setPATH = require('./set-path.js')
const { resolve } = require('path')
const npm_config_node_gyp = require.resolve('node-gyp/bin/node-gyp.js')

const makeSpawnArgs = options => {
  const {
    event,
    path,
    scriptShell = true,
    binPaths,
    env,
    stdio,
    cmd,
    args,
    stdioString,
  } = options

  const spawnEnv = setPATH(path, binPaths, {
    // we need to at least save the PATH environment var
    ...process.env,
    ...env,
    npm_package_json: resolve(path, 'package.json'),
    npm_lifecycle_event: event,
    npm_lifecycle_script: cmd,
    npm_config_node_gyp,
  })

  const spawnOpts = {
    env: spawnEnv,
    stdioString,
    stdio,
    cwd: path,
    shell: scriptShell,
  }

  return [cmd, args, spawnOpts]
}

module.exports = makeSpawnArgs
