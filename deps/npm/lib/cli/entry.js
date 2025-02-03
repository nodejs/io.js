// Separated out for easier unit testing
module.exports = async (process, validateEngines) => {
  // set it here so that regardless of what happens later, we don't
  // leak any private CLI configs to other programs
  process.title = 'npm'

  // Patch the global fs module here at the app level
  require('graceful-fs').gracefulify(require('node:fs'))

  const satisfies = require('semver/functions/satisfies')
  const ExitHandler = require('./exit-handler.js')
  const exitHandler = new ExitHandler({ process })
  const Npm = require('../npm.js')
  const npm = new Npm()
  exitHandler.setNpm(npm)

  // only log node and npm paths in argv initially since argv can contain sensitive info. a cleaned version will be logged later
  const { log, output } = require('proc-log')
  log.verbose('cli', process.argv.slice(0, 2).join(' '))
  log.info('using', 'npm@%s', npm.version)
  log.info('using', 'node@%s', process.version)

  // At this point we've required a few files and can be pretty sure we dont contain invalid syntax for this version of node. It's possible a lazy require would, but that's unlikely enough that it's not worth catching anymore and we attach the more important exit handlers.
  validateEngines.off()
  exitHandler.registerUncaughtHandlers()

  // It is now safe to log a warning if they are using a version of node that is not going to fail on syntax errors but is still unsupported and untested and might not work reliably. This is safe to use the logger now which we want since this will show up in the error log too.
  if (!satisfies(validateEngines.node, validateEngines.engines)) {
    log.warn('cli', validateEngines.unsupportedMessage)
  }

  // Now actually fire up npm and run the command.
  // This is how to use npm programmatically:
  try {
    const { exec, command, args } = await npm.load()

    if (!exec) {
      return exitHandler.exit()
    }

    if (!command) {
      output.standard(npm.usage)
      process.exitCode = 1
      return exitHandler.exit()
    }

    // Options are prefixed by a hyphen-minus (-, \u2d).
    // Other dash-type chars look similar but are invalid.
    const nonDashArgs = npm.argv.filter(a => /^[\u2010-\u2015\u2212\uFE58\uFE63\uFF0D]/.test(a))
    if (nonDashArgs.length) {
      log.error(
        'arg',
        'Argument starts with non-ascii dash, this is probably invalid:',
        require('@npmcli/redact').redactLog(nonDashArgs.join(', '))
      )
    }

    const execPromise = npm.exec(command, args)

    // this is async but we dont await it, since its ok if it doesnt
    // finish before the command finishes running. it uses command and argv
    // so it must be initiated here, after the command name is set
    const updateNotifier = require('./update-notifier.js')
    // eslint-disable-next-line promise/catch-or-return
    updateNotifier(npm).then((msg) => (npm.updateNotification = msg))

    await execPromise
    return exitHandler.exit()
  } catch (err) {
    return exitHandler.exit(err)
  }
}
