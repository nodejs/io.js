const { resolve, dirname, join } = require('node:path')
const Config = require('@npmcli/config')
const which = require('which')
const fs = require('node:fs/promises')
const { definitions, flatten, shorthands } = require('@npmcli/config/lib/definitions')
const usage = require('./utils/npm-usage.js')
const LogFile = require('./utils/log-file.js')
const Timers = require('./utils/timers.js')
const Display = require('./utils/display.js')
const { log, time, output, META } = require('proc-log')
const { redactLog: replaceInfo } = require('@npmcli/redact')
const pkg = require('../package.json')
const { deref } = require('./utils/cmd-list.js')
const { jsonError, outputError } = require('./utils/output-error.js')

class Npm {
  static get version () {
    return pkg.version
  }

  static cmd (c) {
    const command = deref(c)
    if (!command) {
      throw Object.assign(new Error(`Unknown command ${c}`), {
        code: 'EUNKNOWNCOMMAND',
        command: c,
      })
    }
    return require(`./commands/${command}.js`)
  }

  unrefPromises = []
  updateNotification = null
  argv = []

  #command = null
  #runId = new Date().toISOString().replace(/[.:]/g, '_')
  #title = 'npm'
  #argvClean = []
  #npmRoot = null

  #display = null
  #logFile = new LogFile()
  #timers = new Timers()

  // all these options are only used by tests in order to make testing more
  // closely resemble real world usage. for now, npm has no programmatic API so
  // it is ok to add stuff here, but we should not rely on it more than
  // necessary. XXX: make these options not necessary by refactoring @npmcli/config
  //   - npmRoot: this is where npm looks for docs files and the builtin config
  //   - argv: this allows tests to extend argv in the same way the argv would
  //     be passed in via a CLI arg.
  //   - excludeNpmCwd: this is a hack to get @npmcli/config to stop walking up
  //     dirs to set a local prefix when it encounters the `npmRoot`. this
  //     allows tests created by tap inside this repo to not set the local
  //     prefix to `npmRoot` since that is the first dir it would encounter when
  //     doing implicit detection
  constructor ({
    stdout = process.stdout,
    stderr = process.stderr,
    npmRoot = dirname(__dirname),
    argv = [],
    excludeNpmCwd = false,
  } = {}) {
    this.#display = new Display({ stdout, stderr })
    this.#npmRoot = npmRoot
    this.config = new Config({
      npmPath: this.#npmRoot,
      definitions,
      flatten,
      shorthands,
      argv: [...process.argv, ...argv],
      excludeNpmCwd,
    })
  }

  async load () {
    let err
    try {
      return await time.start('npm:load', () => this.#load())
    } catch (e) {
      err = e
    }
    return this.#handleError(err)
  }

  async #load () {
    await time.start('npm:load:whichnode', async () => {
      // TODO should we throw here?
      const node = await which(process.argv[0]).catch(() => {})
      if (node && node.toUpperCase() !== process.execPath.toUpperCase()) {
        log.verbose('node symlink', node)
        process.execPath = node
        this.config.execPath = node
      }
    })

    await time.start('npm:load:configload', () => this.config.load())

    // npm --versions
    if (this.config.get('versions', 'cli')) {
      this.argv = ['version']
      this.config.set('usage', false, 'cli')
    } else {
      this.argv = [...this.config.parsedArgv.remain]
    }

    // Remove first argv since that is our command as typed
    // Note that this might not be the actual name of the command
    // due to aliases, etc. But we use the raw form of it later
    // in user output so it must be preserved as is.
    const commandArg = this.argv.shift()

    // This is the actual name of the command that will be run or
    // undefined if deref could not find a match
    const command = deref(commandArg)

    await this.#display.load({
      command,
      loglevel: this.config.get('loglevel'),
      stdoutColor: this.color,
      stderrColor: this.logColor,
      timing: this.config.get('timing'),
      unicode: this.config.get('unicode'),
      progress: this.flatOptions.progress,
      json: this.config.get('json'),
      heading: this.config.get('heading'),
    })
    process.env.COLOR = this.color ? '1' : '0'

    // npm -v
    // return from here early so we dont create any caches/logfiles/timers etc
    if (this.config.get('version', 'cli')) {
      output.standard(this.version)
      return { exec: false }
    }

    // mkdir this separately since the logs dir can be set to
    // a different location. if this fails, then we don't have
    // a cache dir, but we don't want to fail immediately since
    // the command might not need a cache dir (like `npm --version`)
    await time.start('npm:load:mkdirpcache', () =>
      fs.mkdir(this.cache, { recursive: true })
        .catch((e) => log.verbose('cache', `could not create cache: ${e}`)))

    // it's ok if this fails. user might have specified an invalid dir
    // which we will tell them about at the end
    if (this.config.get('logs-max') > 0) {
      await time.start('npm:load:mkdirplogs', () =>
        fs.mkdir(this.#logsDir, { recursive: true })
          .catch((e) => log.verbose('logfile', `could not create logs-dir: ${e}`)))
    }

    // note: this MUST be shorter than the actual argv length, because it
    // uses the same memory, so node will truncate it if it's too long.
    // We time this because setting process.title is slow sometimes but we
    // have to do it for security reasons. But still helpful to know how slow it is.
    time.start('npm:load:setTitle', () => {
      const { parsedArgv: { cooked, remain } } = this.config
      // Secrets are mostly in configs, so title is set using only the positional args
      // to keep those from being leaked.  We still do a best effort replaceInfo.
      this.#title = ['npm'].concat(replaceInfo(remain)).join(' ').trim()
      process.title = this.#title
      // The cooked argv is also logged separately for debugging purposes. It is
      // cleaned as a best effort by replacing known secrets like basic auth
      // password and strings that look like npm tokens. XXX: for this to be
      // safer the config should create a sanitized version of the argv as it
      // has the full context of what each option contains.
      this.#argvClean = replaceInfo(cooked)
      log.verbose('title', this.title)
      log.verbose('argv', this.#argvClean.map(JSON.stringify).join(' '))
    })

    // logFile.load returns a promise that resolves when old logs are done being cleaned.
    // We save this promise to an array so that we can await it in tests to ensure more
    // deterministic logging behavior. The process will also hang open if this were to
    // take a long time to resolve, but that is why process.exit is called explicitly
    // in the exit-handler.
    this.unrefPromises.push(this.#logFile.load({
      command,
      path: this.logPath,
      logsMax: this.config.get('logs-max'),
      timing: this.config.get('timing'),
    }))

    this.#timers.load({
      path: this.logPath,
      timing: this.config.get('timing'),
    })

    const configScope = this.config.get('scope')
    if (configScope && !/^@/.test(configScope)) {
      this.config.set('scope', `@${configScope}`, this.config.find('scope'))
    }

    if (this.config.get('force')) {
      log.warn('using --force', 'Recommended protections disabled.')
    }

    return { exec: true, command: commandArg, args: this.argv }
  }

  async exec (cmd, args = this.argv) {
    if (!this.#command) {
      let err
      try {
        await this.#exec(cmd, args)
      } catch (e) {
        err = e
      }
      return this.#handleError(err)
    } else {
      return this.#exec(cmd, args)
    }
  }

  // Call an npm command
  async #exec (cmd, args) {
    const Command = this.constructor.cmd(cmd)
    const command = new Command(this)

    // since 'test', 'start', 'stop', etc. commands re-enter this function
    // to call the run-script command, we need to only set it one time.
    if (!this.#command) {
      this.#command = command
      process.env.npm_command = this.command
    }

    if (this.config.get('usage')) {
      return output.standard(command.usage)
    }

    let execWorkspaces = false
    const hasWsConfig = this.config.get('workspaces') || this.config.get('workspace').length
    // if cwd is a workspace, the default is set to [that workspace]
    const implicitWs = this.config.get('workspace', 'default').length
    // (-ws || -w foo) && (cwd is not a workspace || command is not ignoring implicit workspaces)
    if (hasWsConfig && (!implicitWs || !Command.ignoreImplicitWorkspace)) {
      if (this.global) {
        throw new Error('Workspaces not supported for global packages')
      }
      if (!Command.workspaces) {
        throw Object.assign(new Error('This command does not support workspaces.'), {
          code: 'ENOWORKSPACES',
        })
      }
      execWorkspaces = true
    }

    return time.start(`command:${cmd}`, () =>
      execWorkspaces ? command.execWorkspaces(args) : command.exec(args))
  }

  // This gets called at the end of the exit handler and
  // during any tests to cleanup all of our listeners
  // Everything in here should be synchronous
  unload () {
    this.#timers.off()
    this.#display.off()
    this.#logFile.off()
  }

  finish (err) {
    // Finish all our timer work, this will write the file if requested, end timers, etc
    this.#timers.finish({
      id: this.#runId,
      command: this.#argvClean,
      logfiles: this.logFiles,
      version: this.version,
    })

    output.flush({
      [META]: true,
      // json can be set during a command so we send the
      // final value of it to the display layer here
      json: this.loaded && this.config.get('json'),
      jsonError: jsonError(err, this),
    })
  }

  exitErrorMessage () {
    if (this.logFiles.length) {
      return `A complete log of this run can be found in: ${this.logFiles}`
    }

    const logsMax = this.config.get('logs-max')
    if (logsMax <= 0) {
      // user specified no log file
      return `Log files were not written due to the config logs-max=${logsMax}`
    }

    // could be an error writing to the directory
    return `Log files were not written due to an error writing to the directory: ${this.#logsDir}` +
      '\nYou can rerun the command with `--loglevel=verbose` to see the logs in your terminal'
  }

  async #handleError (err) {
    if (err) {
      // Get the local package if it exists for a more helpful error message
      const localPkg = await require('@npmcli/package-json')
        .normalize(this.localPrefix)
        .then(p => p.content)
        .catch(() => null)
      Object.assign(err, this.#getError(err, { pkg: localPkg }))
    }

    this.finish(err)

    if (err) {
      throw err
    }
  }

  #getError (rawErr, opts) {
    const { files = [], ...error } = require('./utils/error-message.js').getError(rawErr, {
      npm: this,
      command: this.#command,
      ...opts,
    })

    const { writeFileSync } = require('node:fs')
    for (const [file, content] of files) {
      const filePath = `${this.logPath}${file}`
      const fileContent = `'Log files:\n${this.logFiles.join('\n')}\n\n${content.trim()}\n`
      try {
        writeFileSync(filePath, fileContent)
        error.detail.push(['', `\n\nFor a full report see:\n${filePath}`])
      } catch (fileErr) {
        log.warn('', `Could not write error message to ${file} due to ${fileErr}`)
      }
    }

    outputError(error)

    return error
  }

  get title () {
    return this.#title
  }

  get loaded () {
    return this.config.loaded
  }

  get version () {
    return this.constructor.version
  }

  get command () {
    return this.#command?.name
  }

  get flatOptions () {
    const { flat } = this.config
    flat.nodeVersion = process.version
    flat.npmVersion = pkg.version
    if (this.command) {
      flat.npmCommand = this.command
    }
    return flat
  }

  // color and logColor are a special derived values that takes into
  // consideration not only the config, but whether or not we are operating
  // in a tty with the associated output (stdout/stderr)
  get color () {
    return this.flatOptions.color
  }

  get logColor () {
    return this.flatOptions.logColor
  }

  get noColorChalk () {
    return this.#display.chalk.noColor
  }

  get chalk () {
    return this.#display.chalk.stdout
  }

  get logChalk () {
    return this.#display.chalk.stderr
  }

  get global () {
    return this.config.get('global') || this.config.get('location') === 'global'
  }

  get silent () {
    return this.flatOptions.silent
  }

  get lockfileVersion () {
    return 2
  }

  get started () {
    return this.#timers.started
  }

  get logFiles () {
    return this.#logFile.files
  }

  get #logsDir () {
    return this.config.get('logs-dir') || join(this.cache, '_logs')
  }

  get logPath () {
    return resolve(this.#logsDir, `${this.#runId}-`)
  }

  get npmRoot () {
    return this.#npmRoot
  }

  get cache () {
    return this.config.get('cache')
  }

  get globalPrefix () {
    return this.config.globalPrefix
  }

  get localPrefix () {
    return this.config.localPrefix
  }

  get localPackage () {
    return this.config.localPackage
  }

  get globalDir () {
    return process.platform !== 'win32'
      ? resolve(this.globalPrefix, 'lib', 'node_modules')
      : resolve(this.globalPrefix, 'node_modules')
  }

  get localDir () {
    return resolve(this.localPrefix, 'node_modules')
  }

  get dir () {
    return this.global ? this.globalDir : this.localDir
  }

  get globalBin () {
    const b = this.globalPrefix
    return process.platform !== 'win32' ? resolve(b, 'bin') : b
  }

  get localBin () {
    return resolve(this.dir, '.bin')
  }

  get bin () {
    return this.global ? this.globalBin : this.localBin
  }

  get prefix () {
    return this.global ? this.globalPrefix : this.localPrefix
  }

  get usage () {
    return usage(this)
  }
}

module.exports = Npm
