try {
  const { enableCompileCache } = require('node:module')
  /* istanbul ignore next */
  if (enableCompileCache) {
    enableCompileCache()
  }
} catch (e) { /* istanbul ignore next */ }

const validateEngines = require('./cli/validate-engines.js')
const cliEntry = require('node:path').resolve(__dirname, 'cli/entry.js')

module.exports = (process) => validateEngines(process, () => require(cliEntry))
