#!/usr/bin/env node

const fs = require('node:fs')
const path = require('node:path')
const { time } = require('proc-log')

const { bin, arb: options } = require('./lib/options')
const version = require('../package.json').version

const usage = (message = '') => `Arborist - the npm tree doctor

Version: ${version}
${message && '\n' + message + '\n'}
# USAGE
  arborist <cmd> [path] [options...]

# COMMANDS

  * reify: reify ideal tree to node_modules (install, update, rm, ...)
  * prune: prune the ideal tree and reify (like npm prune)
  * ideal: generate and print the ideal tree
  * actual: read and print the actual tree in node_modules
  * virtual: read and print the virtual tree in the local shrinkwrap file
  * shrinkwrap: load a local shrinkwrap and print its data
  * audit: perform a security audit on project dependencies
  * funding: query funding information in the local package tree.  A second
    positional argument after the path name can limit to a package name.
  * license: query license information in the local package tree.  A second
    positional argument after the path name can limit to a license type.
  * help: print this text
  * version: print the version

# OPTIONS

  Most npm options are supported, but in camelCase rather than css-case.  For
  example, instead of '--dry-run', use '--dryRun'.

  Additionally:

  * --loglevel=warn|--quiet will supppress the printing of package trees
  * --logfile <file|bool> will output logs to a file
  * --timing will show timing information
  * Instead of 'npm install <pkg>', use 'arborist reify --add=<pkg>'.
    The '--add=<pkg>' option can be specified multiple times.
  * Instead of 'npm rm <pkg>', use 'arborist reify --rm=<pkg>'.
    The '--rm=<pkg>' option can be specified multiple times.
  * Instead of 'npm update', use 'arborist reify --update-all'.
  * 'npm audit fix' is 'arborist audit --fix'
`

const commands = {
  version: () => console.log(version),
  help: () => console.log(usage()),
  exit: () => {
    process.exitCode = 1
    console.error(
      usage(`Error: command '${bin.command}' does not exist.`)
    )
  },
}

const commandFiles = fs.readdirSync(__dirname).filter((f) => path.extname(f) === '.js' && f !== __filename)

for (const file of commandFiles) {
  const command = require(`./${file}`)
  const name = path.basename(file, '.js')
  const totalTime = `bin:${name}:init`
  const scriptTime = `bin:${name}:script`

  commands[name] = () => {
    const timers = require('./lib/timers')
    const log = require('./lib/logging')

    log.info(name, options)

    const timeEnd = time.start(totalTime)
    const scriptEnd = time.start(scriptTime)

    return command(options, (result) => {
      scriptEnd()
      return {
        result,
        timing: {
          seconds: `${timers.get(scriptTime) / 1e9}s`,
          ms: `${timers.get(scriptTime) / 1e6}ms`,
        },
      }
    })
      .then((result) => {
        log.info(result)
        return result
      })
      .catch((err) => {
        process.exitCode = 1
        log.error(err)
        return err
      })
      .then((r) => {
        timeEnd()
        if (bin.loglevel !== 'silent') {
          console[process.exitCode ? 'error' : 'log'](r)
        }
        return r
      })
  }
}

if (commands[bin.command]) {
  commands[bin.command]()
} else {
  commands.exit()
}
