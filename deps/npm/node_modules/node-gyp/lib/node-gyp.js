'use strict'

const path = require('path')
const nopt = require('nopt')
const log = require('./log')
const childProcess = require('child_process')
const { EventEmitter } = require('events')

const commands = [
  // Module build commands
  'build',
  'clean',
  'configure',
  'rebuild',
  // Development Header File management commands
  'install',
  'list',
  'remove'
]

class Gyp extends EventEmitter {
  /**
   * Export the contents of the package.json.
   */
  package = require('../package.json')

  /**
   * nopt configuration definitions
   */
  configDefs = {
    help: Boolean, // everywhere
    arch: String, // 'configure'
    cafile: String, // 'install'
    debug: Boolean, // 'build'
    directory: String, // bin
    make: String, // 'build'
    'msvs-version': String, // 'configure'
    ensure: Boolean, // 'install'
    solution: String, // 'build' (windows only)
    proxy: String, // 'install'
    noproxy: String, // 'install'
    devdir: String, // everywhere
    nodedir: String, // 'configure'
    loglevel: String, // everywhere
    python: String, // 'configure'
    'dist-url': String, // 'install'
    tarball: String, // 'install'
    jobs: String, // 'build'
    thin: String, // 'configure'
    'force-process-config': Boolean // 'configure'
  }

  /**
   * nopt shorthands
   */
  shorthands = {
    release: '--no-debug',
    C: '--directory',
    debug: '--debug',
    j: '--jobs',
    silly: '--loglevel=silly',
    verbose: '--loglevel=verbose',
    silent: '--loglevel=silent'
  }

  /**
   * expose the command aliases for the bin file to use.
   */
  aliases = {
    ls: 'list',
    rm: 'remove'
  }

  constructor (...args) {
    super(...args)

    this.devDir = ''

    this.commands = commands.reduce((acc, command) => {
      acc[command] = (argv) => require('./' + command)(this, argv)
      return acc
    }, {})

    Object.defineProperty(this, 'version', {
      enumerable: true,
      get: function () { return this.package.version }
    })
  }

  /**
   * Parses the given argv array and sets the 'opts',
   * 'argv' and 'command' properties.
   */
  parseArgv (argv) {
    this.opts = nopt(this.configDefs, this.shorthands, argv)
    this.argv = this.opts.argv.remain.slice()

    const commands = this.todo = []

    // create a copy of the argv array with aliases mapped
    argv = this.argv.map((arg) => {
    // is this an alias?
      if (arg in this.aliases) {
        arg = this.aliases[arg]
      }
      return arg
    })

    // process the mapped args into "command" objects ("name" and "args" props)
    argv.slice().forEach((arg) => {
      if (arg in this.commands) {
        const args = argv.splice(0, argv.indexOf(arg))
        argv.shift()
        if (commands.length > 0) {
          commands[commands.length - 1].args = args
        }
        commands.push({ name: arg, args: [] })
      }
    })
    if (commands.length > 0) {
      commands[commands.length - 1].args = argv.splice(0)
    }

    // support for inheriting config env variables from npm
    const npmConfigPrefix = 'npm_config_'
    Object.keys(process.env).forEach((name) => {
      if (name.indexOf(npmConfigPrefix) !== 0) {
        return
      }
      const val = process.env[name]
      if (name === npmConfigPrefix + 'loglevel') {
        log.logger.level = val
      } else {
      // add the user-defined options to the config
        name = name.substring(npmConfigPrefix.length)
        // gyp@741b7f1 enters an infinite loop when it encounters
        // zero-length options so ensure those don't get through.
        if (name) {
        // convert names like force_process_config to force-process-config
          if (name.includes('_')) {
            name = name.replace(/_/g, '-')
          }
          this.opts[name] = val
        }
      }
    })

    if (this.opts.loglevel) {
      log.logger.level = this.opts.loglevel
    }
    log.resume()
  }

  /**
   * Spawns a child process and emits a 'spawn' event.
   */
  spawn (command, args, opts) {
    if (!opts) {
      opts = {}
    }
    if (!opts.silent && !opts.stdio) {
      opts.stdio = [0, 1, 2]
    }
    const cp = childProcess.spawn(command, args, opts)
    log.info('spawn', command)
    log.info('spawn args', args)
    return cp
  }

  /**
   * Returns the usage instructions for node-gyp.
   */
  usage () {
    return [
      '',
      '  Usage: node-gyp <command> [options]',
      '',
      '  where <command> is one of:',
      commands.map((c) => '    - ' + c + ' - ' + require('./' + c).usage).join('\n'),
      '',
      'node-gyp@' + this.version + '  ' + path.resolve(__dirname, '..'),
      'node@' + process.versions.node
    ].join('\n')
  }
}

module.exports = () => new Gyp()
module.exports.Gyp = Gyp
