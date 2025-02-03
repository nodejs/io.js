const reifyOutput = require('./reify-output.js')
const ini = require('ini')
const { writeFile } = require('node:fs/promises')
const { resolve } = require('node:path')

const reifyFinish = async (npm, arb) => {
  await saveBuiltinConfig(npm, arb)
  reifyOutput(npm, arb)
}

const saveBuiltinConfig = async (npm, arb) => {
  const { options: { global }, actualTree } = arb
  if (!global) {
    return
  }

  // if we are using a builtin config, and just installed npm as
  // a top-level global package, we have to preserve that config.
  const npmNode = actualTree.inventory.get('node_modules/npm')
  if (!npmNode) {
    return
  }

  const builtinConf = npm.config.data.get('builtin')
  if (builtinConf.loadError) {
    return
  }

  const content = ini.stringify(builtinConf.raw).trim() + '\n'
  await writeFile(resolve(npmNode.path, 'npmrc'), content)
}

module.exports = reifyFinish
