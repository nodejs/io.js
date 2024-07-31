const pkgJson = require('@npmcli/package-json')
const runScript = require('@npmcli/run-script')
const { join, relative } = require('node:path')
const { log, output } = require('proc-log')
const completion = require('../utils/installed-shallow.js')
const BaseCommand = require('../base-cmd.js')

// npm explore <pkg>[@<version>]
// open a subshell to the package folder.
class Explore extends BaseCommand {
  static description = 'Browse an installed package'
  static name = 'explore'
  static usage = ['<pkg> [ -- <command>]']
  static params = ['shell']
  static ignoreImplicitWorkspace = false

  // TODO
  /* istanbul ignore next */
  static async completion (opts, npm) {
    return completion(npm, opts)
  }

  async exec (args) {
    if (args.length < 1 || !args[0]) {
      throw this.usageError()
    }

    const pkgname = args.shift()

    // detect and prevent any .. shenanigans
    const path = join(this.npm.dir, join('/', pkgname))
    if (relative(path, this.npm.dir) === '') {
      throw this.usageError()
    }

    // run as if running a script named '_explore', which we set to either
    // the set of arguments, or the shell config, and let @npmcli/run-script
    // handle all the escaping and PATH setup stuff.

    const { content: pkg } = await pkgJson.normalize(path).catch(er => {
      log.error('explore', `It doesn't look like ${pkgname} is installed.`)
      throw er
    })

    const { shell } = this.npm.flatOptions
    pkg.scripts = {
      ...(pkg.scripts || {}),
      _explore: args.join(' ').trim() || shell,
    }

    if (!args.length) {
      output.standard(`\nExploring ${path}\nType 'exit' or ^D when finished\n`)
    }

    return runScript({
      ...this.npm.flatOptions,
      pkg,
      path,
      event: '_explore',
      stdio: 'inherit',
    }).catch(er => {
      process.exitCode = typeof er.code === 'number' && er.code !== 0 ? er.code
        : 1
        // if it's not an exit error, or non-interactive, throw it
      const isProcExit = er.message === 'command failed' &&
          (typeof er.code === 'number' || /^SIG/.test(er.signal || ''))
      if (args.length || !isProcExit) {
        throw er
      }
    })
  }
}

module.exports = Explore
