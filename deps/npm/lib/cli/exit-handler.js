const { log, output, META } = require('proc-log')
const { errorMessage, getExitCodeFromError } = require('../utils/error-message.js')

class ExitHandler {
  #npm = null
  #process = null
  #exited = false
  #exitErrorMessage = false

  #noNpmError = false

  get #hasNpm () {
    return !!this.#npm
  }

  get #loaded () {
    return !!this.#npm?.loaded
  }

  get #showExitErrorMessage () {
    if (!this.#loaded) {
      return false
    }
    if (!this.#exited) {
      return true
    }
    return this.#exitErrorMessage
  }

  get #notLoadedOrExited () {
    return !this.#loaded && !this.#exited
  }

  setNpm (npm) {
    this.#npm = npm
  }

  constructor ({ process }) {
    this.#process = process
    this.#process.on('exit', this.#handleProcesExitAndReset)
  }

  registerUncaughtHandlers () {
    this.#process.on('uncaughtException', this.#handleExit)
    this.#process.on('unhandledRejection', this.#handleExit)
  }

  exit (err) {
    this.#handleExit(err)
  }

  #handleProcesExitAndReset = (code) => {
    this.#handleProcessExit(code)

    // Reset all the state. This is only relevant for tests since
    // in reality the process fully exits here.
    this.#process.off('exit', this.#handleProcesExitAndReset)
    this.#process.off('uncaughtException', this.#handleExit)
    this.#process.off('unhandledRejection', this.#handleExit)
    if (this.#loaded) {
      this.#npm.unload()
    }
    this.#npm = null
    this.#exited = false
    this.#exitErrorMessage = false
  }

  #handleProcessExit (code) {
    // Force exit code to a number if it has not been set
    const exitCode = typeof code === 'number' ? code : (this.#exited ? 0 : 1)
    this.#process.exitCode = exitCode

    if (this.#notLoadedOrExited) {
      // Exit handler was not called and npm was not loaded so we have to log something
      this.#logConsoleError(new Error(`Process exited unexpectedly with code: ${exitCode}`))
      return
    }

    if (this.#logNoNpmError()) {
      return
    }

    const os = require('node:os')
    log.verbose('cwd', this.#process.cwd())
    log.verbose('os', `${os.type()} ${os.release()}`)
    log.verbose('node', this.#process.version)
    log.verbose('npm ', `v${this.#npm.version}`)

    // only show the notification if it finished
    if (typeof this.#npm.updateNotification === 'string') {
      log.notice('', this.#npm.updateNotification, { [META]: true, force: true })
    }

    if (!this.#exited) {
      log.error('', 'Exit handler never called!')
      log.error('', 'This is an error with npm itself. Please report this error at:')
      log.error('', '  <https://github.com/npm/cli/issues>')
      if (this.#npm.silent) {
        output.error('')
      }
    }

    log.verbose('exit', exitCode)

    if (exitCode) {
      log.verbose('code', exitCode)
    } else {
      log.info('ok')
    }

    if (this.#showExitErrorMessage) {
      log.error('', this.#npm.exitErrorMessage())
    }
  }

  #logConsoleError (err) {
    // Run our error message formatters on all errors even if we
    // have no npm or an unloaded npm. This will clean the error
    // and possible return a formatted message about EACCESS or something.
    const { summary, detail } = errorMessage(err, this.#npm)
    const formatted = [...new Set([...summary, ...detail].flat().filter(Boolean))].join('\n')
    // If we didn't get anything from the formatted message then just display the full stack
    // eslint-disable-next-line no-console
    console.error(formatted === err.message ? err.stack : formatted)
  }

  #logNoNpmError (err) {
    if (this.#hasNpm) {
      return false
    }
    // Make sure we only log this error once
    if (!this.#noNpmError) {
      this.#noNpmError = true
      this.#logConsoleError(
        new Error(`Exit prior to setting npm in exit handler`, err ? { cause: err } : {})
      )
    }
    return true
  }

  #handleExit = (err) => {
    this.#exited = true

    // No npm at all
    if (this.#logNoNpmError(err)) {
      return this.#process.exit(this.#process.exitCode || getExitCodeFromError(err) || 1)
    }

    // npm was never loaded but we still might have a config loading error or
    // something similar that we can run through the error message formatter
    // to give the user a clue as to what happened.s
    if (!this.#loaded) {
      this.#logConsoleError(new Error('Exit prior to config file resolving', { cause: err }))
      return this.#process.exit(this.#process.exitCode || getExitCodeFromError(err) || 1)
    }

    this.#exitErrorMessage = err?.suppressError === true ? false : !!err

    // Prefer the exit code of the error, then the current process exit code,
    // then set it to 1 if we still have an error. Otherwise we call process.exit
    // with undefined so that it can determine the final exit code
    const exitCode = err?.exitCode ?? this.#process.exitCode ?? (err ? 1 : undefined)

    // explicitly call process.exit now so we don't hang on things like the
    // update notifier, also flush stdout/err beforehand because process.exit doesn't
    // wait for that to happen.
    this.#process.stderr.write('', () => this.#process.stdout.write('', () => {
      this.#process.exit(exitCode)
    }))
  }
}

module.exports = ExitHandler
