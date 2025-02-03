const git = require('@npmcli/git')
const { log } = require('proc-log')

// returns true if it's cool to do git stuff
// throws if it's unclean, and not forced.
module.exports = async opts => {
  const { force } = opts
  let hadError = false
  const clean = await git.isClean(opts).catch(er => {
    if (er.code === 'ENOGIT') {
      log.warn(
        'version',
        'This is a Git checkout, but the git command was not found.',
        'npm could not create a Git tag for this release!'
      )
      hadError = true
      // how can merges be real if our git isn't real?
      return true
    } else {
      throw er
    }
  })

  if (!clean) {
    if (!force) {
      throw new Error('Git working directory not clean.')
    }
    log.warn('version', 'Git working directory not clean, proceeding forcefully.')
  }

  return !hadError
}
