'use strict'

const log = require('./log')
const semver = require('semver')
const { execFile } = require('./util')
const win = process.platform === 'win32'

function getOsUserInfo () {
  try {
    return require('os').userInfo().username
  } catch {}
}

const systemDrive = process.env.SystemDrive || 'C:'
const username = process.env.USERNAME || process.env.USER || getOsUserInfo()
const localAppData = process.env.LOCALAPPDATA || `${systemDrive}\\${username}\\AppData\\Local`
const foundLocalAppData = process.env.LOCALAPPDATA || username
const programFiles = process.env.ProgramW6432 || process.env.ProgramFiles || `${systemDrive}\\Program Files`
const programFilesX86 = process.env['ProgramFiles(x86)'] || `${programFiles} (x86)`

const winDefaultLocationsArray = []
for (const majorMinor of ['311', '310', '39', '38']) {
  if (foundLocalAppData) {
    winDefaultLocationsArray.push(
      `${localAppData}\\Programs\\Python\\Python${majorMinor}\\python.exe`,
      `${programFiles}\\Python${majorMinor}\\python.exe`,
      `${localAppData}\\Programs\\Python\\Python${majorMinor}-32\\python.exe`,
      `${programFiles}\\Python${majorMinor}-32\\python.exe`,
      `${programFilesX86}\\Python${majorMinor}-32\\python.exe`
    )
  } else {
    winDefaultLocationsArray.push(
      `${programFiles}\\Python${majorMinor}\\python.exe`,
      `${programFiles}\\Python${majorMinor}-32\\python.exe`,
      `${programFilesX86}\\Python${majorMinor}-32\\python.exe`
    )
  }
}

class PythonFinder {
  static findPython = (...args) => new PythonFinder(...args).findPython()

  log = log.withPrefix('find Python')
  argsExecutable = ['-c', 'import sys; sys.stdout.buffer.write(sys.executable.encode(\'utf-8\'));']
  argsVersion = ['-c', 'import sys; print("%s.%s.%s" % sys.version_info[:3]);']
  semverRange = '>=3.6.0'

  // These can be overridden for testing:
  execFile = execFile
  env = process.env
  win = win
  pyLauncher = 'py.exe'
  winDefaultLocations = winDefaultLocationsArray

  constructor (configPython) {
    this.configPython = configPython
    this.errorLog = []
  }

  // Logs a message at verbose level, but also saves it to be displayed later
  // at error level if an error occurs. This should help diagnose the problem.
  addLog (message) {
    this.log.verbose(message)
    this.errorLog.push(message)
  }

  // Find Python by trying a sequence of possibilities.
  // Ignore errors, keep trying until Python is found.
  async findPython () {
    const SKIP = 0
    const FAIL = 1
    const toCheck = (() => {
      if (this.env.NODE_GYP_FORCE_PYTHON) {
        return [{
          before: () => {
            this.addLog(
              'checking Python explicitly set from NODE_GYP_FORCE_PYTHON')
            this.addLog('- process.env.NODE_GYP_FORCE_PYTHON is ' +
              `"${this.env.NODE_GYP_FORCE_PYTHON}"`)
          },
          check: () => this.checkCommand(this.env.NODE_GYP_FORCE_PYTHON)
        }]
      }

      const checks = [
        {
          before: () => {
            if (!this.configPython) {
              this.addLog(
                'Python is not set from command line or npm configuration')
              return SKIP
            }
            this.addLog('checking Python explicitly set from command line or ' +
              'npm configuration')
            this.addLog('- "--python=" or "npm config get python" is ' +
              `"${this.configPython}"`)
          },
          check: () => this.checkCommand(this.configPython)
        },
        {
          before: () => {
            if (!this.env.PYTHON) {
              this.addLog('Python is not set from environment variable ' +
                'PYTHON')
              return SKIP
            }
            this.addLog('checking Python explicitly set from environment ' +
              'variable PYTHON')
            this.addLog(`- process.env.PYTHON is "${this.env.PYTHON}"`)
          },
          check: () => this.checkCommand(this.env.PYTHON)
        }
      ]

      if (this.win) {
        checks.push({
          before: () => {
            this.addLog(
              'checking if the py launcher can be used to find Python 3')
          },
          check: () => this.checkPyLauncher()
        })
      }

      checks.push(...[
        {
          before: () => { this.addLog('checking if "python3" can be used') },
          check: () => this.checkCommand('python3')
        },
        {
          before: () => { this.addLog('checking if "python" can be used') },
          check: () => this.checkCommand('python')
        }
      ])

      if (this.win) {
        for (let i = 0; i < this.winDefaultLocations.length; ++i) {
          const location = this.winDefaultLocations[i]
          checks.push({
            before: () => this.addLog(`checking if Python is ${location}`),
            check: () => this.checkExecPath(location)
          })
        }
      }

      return checks
    })()

    for (const check of toCheck) {
      const before = check.before()
      if (before === SKIP) {
        continue
      }
      if (before === FAIL) {
        return this.fail()
      }
      try {
        return await check.check()
      } catch (err) {
        this.log.silly('runChecks: err = %j', (err && err.stack) || err)
      }
    }

    return this.fail()
  }

  // Check if command is a valid Python to use.
  // Will exit the Python finder on success.
  // If on Windows, run in a CMD shell to support BAT/CMD launchers.
  async checkCommand (command) {
    let exec = command
    let args = this.argsExecutable
    let shell = false
    if (this.win) {
      // Arguments have to be manually quoted
      exec = `"${exec}"`
      args = args.map(a => `"${a}"`)
      shell = true
    }

    this.log.verbose(`- executing "${command}" to get executable path`)
    // Possible outcomes:
    // - Error: not in PATH, not executable or execution fails
    // - Gibberish: the next command to check version will fail
    // - Absolute path to executable
    try {
      const execPath = await this.run(exec, args, shell)
      this.addLog(`- executable path is "${execPath}"`)
      return this.checkExecPath(execPath)
    } catch (err) {
      this.addLog(`- "${command}" is not in PATH or produced an error`)
      throw err
    }
  }

  // Check if the py launcher can find a valid Python to use.
  // Will exit the Python finder on success.
  // Distributions of Python on Windows by default install with the "py.exe"
  // Python launcher which is more likely to exist than the Python executable
  // being in the $PATH.
  // Because the Python launcher supports Python 2 and Python 3, we should
  // explicitly request a Python 3 version. This is done by supplying "-3" as
  // the first command line argument. Since "py.exe -3" would be an invalid
  // executable for "execFile", we have to use the launcher to figure out
  // where the actual "python.exe" executable is located.
  async checkPyLauncher () {
    this.log.verbose(`- executing "${this.pyLauncher}" to get Python 3 executable path`)
    // Possible outcomes: same as checkCommand
    try {
      const execPath = await this.run(this.pyLauncher, ['-3', ...this.argsExecutable], false)
      this.addLog(`- executable path is "${execPath}"`)
      return this.checkExecPath(execPath)
    } catch (err) {
      this.addLog(`- "${this.pyLauncher}" is not in PATH or produced an error`)
      throw err
    }
  }

  // Check if a Python executable is the correct version to use.
  // Will exit the Python finder on success.
  async checkExecPath (execPath) {
    this.log.verbose(`- executing "${execPath}" to get version`)
    // Possible outcomes:
    // - Error: executable can not be run (likely meaning the command wasn't
    //   a Python executable and the previous command produced gibberish)
    // - Gibberish: somehow the last command produced an executable path,
    //   this will fail when verifying the version
    // - Version of the Python executable
    try {
      const version = await this.run(execPath, this.argsVersion, false)
      this.addLog(`- version is "${version}"`)

      const range = new semver.Range(this.semverRange)
      let valid = false
      try {
        valid = range.test(version)
      } catch (err) {
        this.log.silly('range.test() threw:\n%s', err.stack)
        this.addLog(`- "${execPath}" does not have a valid version`)
        this.addLog('- is it a Python executable?')
        throw err
      }
      if (!valid) {
        this.addLog(`- version is ${version} - should be ${this.semverRange}`)
        this.addLog('- THIS VERSION OF PYTHON IS NOT SUPPORTED')
        throw new Error(`Found unsupported Python version ${version}`)
      }
      return this.succeed(execPath, version)
    } catch (err) {
      this.addLog(`- "${execPath}" could not be run`)
      throw err
    }
  }

  // Run an executable or shell command, trimming the output.
  async run (exec, args, shell) {
    const env = Object.assign({}, this.env)
    env.TERM = 'dumb'
    const opts = { env, shell }

    this.log.silly('execFile: exec = %j', exec)
    this.log.silly('execFile: args = %j', args)
    this.log.silly('execFile: opts = %j', opts)
    try {
      const [err, stdout, stderr] = await this.execFile(exec, args, opts)
      this.log.silly('execFile result: err = %j', (err && err.stack) || err)
      this.log.silly('execFile result: stdout = %j', stdout)
      this.log.silly('execFile result: stderr = %j', stderr)
      return stdout.trim()
    } catch (err) {
      this.log.silly('execFile: threw:\n%s', err.stack)
      throw err
    }
  }

  succeed (execPath, version) {
    this.log.info(`using Python version ${version} found at "${execPath}"`)
    return execPath
  }

  fail () {
    const errorLog = this.errorLog.join('\n')

    const pathExample = this.win
      ? 'C:\\Path\\To\\python.exe'
      : '/path/to/pythonexecutable'
    // For Windows 80 col console, use up to the column before the one marked
    // with X (total 79 chars including logger prefix, 58 chars usable here):
    //                                                           X
    const info = [
      '**********************************************************',
      'You need to install the latest version of Python.',
      'Node-gyp should be able to find and use Python. If not,',
      'you can try one of the following options:',
      `- Use the switch --python="${pathExample}"`,
      '  (accepted by both node-gyp and npm)',
      '- Set the environment variable PYTHON',
      '- Set the npm configuration variable python:',
      `  npm config set python "${pathExample}"`,
      'For more information consult the documentation at:',
      'https://github.com/nodejs/node-gyp#installation',
      '**********************************************************'
    ].join('\n')

    this.log.error(`\n${errorLog}\n\n${info}\n`)
    throw new Error('Could not find any Python installation to use')
  }
}

module.exports = PythonFinder
