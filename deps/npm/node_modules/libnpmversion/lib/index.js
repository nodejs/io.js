const readJson = require('./read-json.js')
const version = require('./version.js')

module.exports = async (newversion, opts = {}) => {
  const {
    path = process.cwd(),
    allowSameVersion = false,
    tagVersionPrefix = 'v',
    commitHooks = true,
    gitTagVersion = true,
    signGitCommit = false,
    signGitTag = false,
    force = false,
    ignoreScripts = false,
    scriptShell = undefined,
    preid = null,
    message = 'v%s',
  } = opts

  const pkg = opts.pkg || await readJson(path + '/package.json')

  return version(newversion, {
    path,
    cwd: path,
    allowSameVersion,
    tagVersionPrefix,
    commitHooks,
    gitTagVersion,
    signGitCommit,
    signGitTag,
    force,
    ignoreScripts,
    scriptShell,
    preid,
    pkg,
    message,
  })
}
