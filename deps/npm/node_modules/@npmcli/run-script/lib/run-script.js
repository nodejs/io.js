const PackageJson = require('@npmcli/package-json')
const runScriptPkg = require('./run-script-pkg.js')
const validateOptions = require('./validate-options.js')
const isServerPackage = require('./is-server-package.js')

const runScript = async options => {
  validateOptions(options)
  if (options.pkg) {
    return runScriptPkg(options)
  }
  const { content: pkg } = await PackageJson.normalize(options.path)
  return runScriptPkg({ ...options, pkg })
}

module.exports = Object.assign(runScript, { isServerPackage })
