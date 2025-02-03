'use strict'

const { spawn } = require('child_process')
const os = require('os')
const which = require('which')

const escape = require('./escape.js')

// 'extra' object is for decorating the error a bit more
const promiseSpawn = (cmd, args, opts = {}, extra = {}) => {
  if (opts.shell) {
    return spawnWithShell(cmd, args, opts, extra)
  }

  let resolve, reject
  const promise = new Promise((_resolve, _reject) => {
    resolve = _resolve
    reject = _reject
  })

  // Create error here so we have a more useful stack trace when rejecting
  const closeError = new Error('command failed')

  const stdout = []
  const stderr = []

  const getResult = (result) => ({
    cmd,
    args,
    ...result,
    ...stdioResult(stdout, stderr, opts),
    ...extra,
  })
  const rejectWithOpts = (er, erOpts) => {
    const resultError = getResult(erOpts)
    reject(Object.assign(er, resultError))
  }

  const proc = spawn(cmd, args, opts)
  promise.stdin = proc.stdin
  promise.process = proc

  proc.on('error', rejectWithOpts)

  if (proc.stdout) {
    proc.stdout.on('data', c => stdout.push(c))
    proc.stdout.on('error', rejectWithOpts)
  }

  if (proc.stderr) {
    proc.stderr.on('data', c => stderr.push(c))
    proc.stderr.on('error', rejectWithOpts)
  }

  proc.on('close', (code, signal) => {
    if (code || signal) {
      rejectWithOpts(closeError, { code, signal })
    } else {
      resolve(getResult({ code, signal }))
    }
  })

  return promise
}

const spawnWithShell = (cmd, args, opts, extra) => {
  let command = opts.shell
  // if shell is set to true, we use a platform default. we can't let the core
  // spawn method decide this for us because we need to know what shell is in use
  // ahead of time so that we can escape arguments properly. we don't need coverage here.
  if (command === true) {
    // istanbul ignore next
    command = process.platform === 'win32' ? process.env.ComSpec : 'sh'
  }

  const options = { ...opts, shell: false }
  const realArgs = []
  let script = cmd

  // first, determine if we're in windows because if we are we need to know if we're
  // running an .exe or a .cmd/.bat since the latter requires extra escaping
  const isCmd = /(?:^|\\)cmd(?:\.exe)?$/i.test(command)
  if (isCmd) {
    let doubleEscape = false

    // find the actual command we're running
    let initialCmd = ''
    let insideQuotes = false
    for (let i = 0; i < cmd.length; ++i) {
      const char = cmd.charAt(i)
      if (char === ' ' && !insideQuotes) {
        break
      }

      initialCmd += char
      if (char === '"' || char === "'") {
        insideQuotes = !insideQuotes
      }
    }

    let pathToInitial
    try {
      pathToInitial = which.sync(initialCmd, {
        path: (options.env && findInObject(options.env, 'PATH')) || process.env.PATH,
        pathext: (options.env && findInObject(options.env, 'PATHEXT')) || process.env.PATHEXT,
      }).toLowerCase()
    } catch (err) {
      pathToInitial = initialCmd.toLowerCase()
    }

    doubleEscape = pathToInitial.endsWith('.cmd') || pathToInitial.endsWith('.bat')
    for (const arg of args) {
      script += ` ${escape.cmd(arg, doubleEscape)}`
    }
    realArgs.push('/d', '/s', '/c', script)
    options.windowsVerbatimArguments = true
  } else {
    for (const arg of args) {
      script += ` ${escape.sh(arg)}`
    }
    realArgs.push('-c', script)
  }

  return promiseSpawn(command, realArgs, options, extra)
}

// open a file with the default application as defined by the user's OS
const open = (_args, opts = {}, extra = {}) => {
  const options = { ...opts, shell: true }
  const args = [].concat(_args)

  let platform = process.platform
  // process.platform === 'linux' may actually indicate WSL, if that's the case
  // open the argument with sensible-browser which is pre-installed
  // In WSL, set the default browser using, for example,
  // export BROWSER="/mnt/c/Program Files (x86)/Google/Chrome/Application/chrome.exe"
  // or
  // export BROWSER="/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
  // To permanently set the default browser, add the appropriate entry to your shell's
  // RC file, e.g. .bashrc or .zshrc.
  if (platform === 'linux' && os.release().toLowerCase().includes('microsoft')) {
    platform = 'wsl'
    if (!process.env.BROWSER) {
      return Promise.reject(
        new Error('Set the BROWSER environment variable to your desired browser.'))
    }
  }

  let command = options.command
  if (!command) {
    if (platform === 'win32') {
      // spawnWithShell does not do the additional os.release() check, so we
      // have to force the shell here to make sure we treat WSL as windows.
      options.shell = process.env.ComSpec
      // also, the start command accepts a title so to make sure that we don't
      // accidentally interpret the first arg as the title, we stick an empty
      // string immediately after the start command
      command = 'start ""'
    } else if (platform === 'wsl') {
      command = 'sensible-browser'
    } else if (platform === 'darwin') {
      command = 'open'
    } else {
      command = 'xdg-open'
    }
  }

  return spawnWithShell(command, args, options, extra)
}
promiseSpawn.open = open

const isPipe = (stdio = 'pipe', fd) => {
  if (stdio === 'pipe' || stdio === null) {
    return true
  }

  if (Array.isArray(stdio)) {
    return isPipe(stdio[fd], fd)
  }

  return false
}

const stdioResult = (stdout, stderr, { stdioString = true, stdio }) => {
  const result = {
    stdout: null,
    stderr: null,
  }

  // stdio is [stdin, stdout, stderr]
  if (isPipe(stdio, 1)) {
    result.stdout = Buffer.concat(stdout)
    if (stdioString) {
      result.stdout = result.stdout.toString().trim()
    }
  }

  if (isPipe(stdio, 2)) {
    result.stderr = Buffer.concat(stderr)
    if (stdioString) {
      result.stderr = result.stderr.toString().trim()
    }
  }

  return result
}

// case insensitive lookup in an object
const findInObject = (obj, key) => {
  key = key.toLowerCase()
  for (const objKey of Object.keys(obj).sort()) {
    if (objKey.toLowerCase() === key) {
      return obj[objKey]
    }
  }
}

module.exports = promiseSpawn
