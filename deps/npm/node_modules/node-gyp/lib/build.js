'use strict'

const gracefulFs = require('graceful-fs')
const fs = gracefulFs.promises
const path = require('path')
const { glob } = require('glob')
const log = require('./log')
const which = require('which')
const win = process.platform === 'win32'

async function build (gyp, argv) {
  let platformMake = 'make'
  if (process.platform === 'aix') {
    platformMake = 'gmake'
  } else if (process.platform === 'os400') {
    platformMake = 'gmake'
  } else if (process.platform.indexOf('bsd') !== -1) {
    platformMake = 'gmake'
  } else if (win && argv.length > 0) {
    argv = argv.map(function (target) {
      return '/t:' + target
    })
  }

  const makeCommand = gyp.opts.make || process.env.MAKE || platformMake
  let command = win ? 'msbuild' : makeCommand
  const jobs = gyp.opts.jobs || process.env.JOBS
  let buildType
  let config
  let arch
  let nodeDir
  let guessedSolution
  let python
  let buildBinsDir

  await loadConfigGypi()

  /**
   * Load the "config.gypi" file that was generated during "configure".
   */

  async function loadConfigGypi () {
    let data
    try {
      const configPath = path.resolve('build', 'config.gypi')
      data = await fs.readFile(configPath, 'utf8')
    } catch (err) {
      if (err.code === 'ENOENT') {
        throw new Error('You must run `node-gyp configure` first!')
      } else {
        throw err
      }
    }

    config = JSON.parse(data.replace(/#.+\n/, ''))

    // get the 'arch', 'buildType', and 'nodeDir' vars from the config
    buildType = config.target_defaults.default_configuration
    arch = config.variables.target_arch
    nodeDir = config.variables.nodedir
    python = config.variables.python

    if ('debug' in gyp.opts) {
      buildType = gyp.opts.debug ? 'Debug' : 'Release'
    }
    if (!buildType) {
      buildType = 'Release'
    }

    log.verbose('build type', buildType)
    log.verbose('architecture', arch)
    log.verbose('node dev dir', nodeDir)
    log.verbose('python', python)

    if (win) {
      await findSolutionFile()
    } else {
      await doWhich()
    }
  }

  /**
   * On Windows, find the first build/*.sln file.
   */

  async function findSolutionFile () {
    const files = await glob('build/*.sln')
    if (files.length === 0) {
      if (gracefulFs.existsSync('build/Makefile') || (await glob('build/*.mk')).length !== 0) {
        command = makeCommand
        await doWhich(false)
        return
      } else {
        throw new Error('Could not find *.sln file or Makefile. Did you run "configure"?')
      }
    }
    guessedSolution = files[0]
    log.verbose('found first Solution file', guessedSolution)
    await doWhich(true)
  }

  /**
   * Uses node-which to locate the msbuild / make executable.
   */

  async function doWhich (msvs) {
    // On Windows use msbuild provided by node-gyp configure
    if (msvs) {
      if (!config.variables.msbuild_path) {
        throw new Error('MSBuild is not set, please run `node-gyp configure`.')
      }
      command = config.variables.msbuild_path
      log.verbose('using MSBuild:', command)
      await doBuild(msvs)
      return
    }

    // First make sure we have the build command in the PATH
    const execPath = await which(command)
    log.verbose('`which` succeeded for `' + command + '`', execPath)
    await doBuild(msvs)
  }

  /**
   * Actually spawn the process and compile the module.
   */

  async function doBuild (msvs) {
    // Enable Verbose build
    const verbose = log.logger.isVisible('verbose')
    let j

    if (!msvs && verbose) {
      argv.push('V=1')
    }

    if (msvs && !verbose) {
      argv.push('/clp:Verbosity=minimal')
    }

    if (msvs) {
      // Turn off the Microsoft logo on Windows
      argv.push('/nologo')
    }

    // Specify the build type, Release by default
    if (msvs) {
      // Convert .gypi config target_arch to MSBuild /Platform
      // Since there are many ways to state '32-bit Intel', default to it.
      // N.B. msbuild's Condition string equality tests are case-insensitive.
      const archLower = arch.toLowerCase()
      const p = archLower === 'x64'
        ? 'x64'
        : (archLower === 'arm'
            ? 'ARM'
            : (archLower === 'arm64' ? 'ARM64' : 'Win32'))
      argv.push('/p:Configuration=' + buildType + ';Platform=' + p)
      if (jobs) {
        j = parseInt(jobs, 10)
        if (!isNaN(j) && j > 0) {
          argv.push('/m:' + j)
        } else if (jobs.toUpperCase() === 'MAX') {
          argv.push('/m:' + require('os').cpus().length)
        }
      }
    } else {
      argv.push('BUILDTYPE=' + buildType)
      // Invoke the Makefile in the 'build' dir.
      argv.push('-C')
      argv.push('build')
      if (jobs) {
        j = parseInt(jobs, 10)
        if (!isNaN(j) && j > 0) {
          argv.push('--jobs')
          argv.push(j)
        } else if (jobs.toUpperCase() === 'MAX') {
          argv.push('--jobs')
          argv.push(require('os').cpus().length)
        }
      }
    }

    if (msvs) {
      // did the user specify their own .sln file?
      const hasSln = argv.some(function (arg) {
        return path.extname(arg) === '.sln'
      })
      if (!hasSln) {
        argv.unshift(gyp.opts.solution || guessedSolution)
      }
    }

    if (!win) {
      // Add build-time dependency symlinks (such as Python) to PATH
      buildBinsDir = path.resolve('build', 'node_gyp_bins')
      process.env.PATH = `${buildBinsDir}:${process.env.PATH}`
      await fs.mkdir(buildBinsDir, { recursive: true })
      const symlinkDestination = path.join(buildBinsDir, 'python3')
      try {
        await fs.unlink(symlinkDestination)
      } catch (err) {
        if (err.code !== 'ENOENT') throw err
      }
      await fs.symlink(python, symlinkDestination)
      log.verbose('bin symlinks', `created symlink to "${python}" in "${buildBinsDir}" and added to PATH`)
    }

    const proc = gyp.spawn(command, argv)
    await new Promise((resolve, reject) => proc.on('exit', async (code, signal) => {
      if (buildBinsDir) {
        // Clean up the build-time dependency symlinks:
        await fs.rm(buildBinsDir, { recursive: true })
      }

      if (code !== 0) {
        return reject(new Error('`' + command + '` failed with exit code: ' + code))
      }
      if (signal) {
        return reject(new Error('`' + command + '` got signal: ' + signal))
      }
      resolve()
    }))
  }
}

module.exports = build
module.exports.usage = 'Invokes `' + (win ? 'msbuild' : 'make') + '` and builds the module'
