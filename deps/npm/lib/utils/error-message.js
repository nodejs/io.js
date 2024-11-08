const { format } = require('node:util')
const { resolve } = require('node:path')
const { redactLog: replaceInfo } = require('@npmcli/redact')
const { log } = require('proc-log')

const errorMessage = (er, npm) => {
  const summary = []
  const detail = []
  const files = []

  er.message &&= replaceInfo(er.message)
  er.stack &&= replaceInfo(er.stack)

  switch (er.code) {
    case 'ERESOLVE': {
      const { report } = require('./explain-eresolve.js')
      summary.push(['ERESOLVE', er.message])
      detail.push(['', ''])
      // XXX(display): error messages are logged so we use the logColor since that is based
      // on stderr. This should be handled solely by the display layer so it could also be
      // printed to stdout if necessary.
      const { explanation, file } = report(er, npm.logChalk, npm.noColorChalk)
      detail.push(['', explanation])
      files.push(['eresolve-report.txt', file])
      break
    }

    case 'ENOLOCK': {
      const cmd = npm.command || ''
      summary.push([cmd, 'This command requires an existing lockfile.'])
      detail.push([cmd, 'Try creating one first with: npm i --package-lock-only'])
      detail.push([cmd, `Original error: ${er.message}`])
      break
    }

    case 'ENOAUDIT':
      summary.push(['audit', er.message])
      break

    case 'ECONNREFUSED':
      summary.push(['', er])
      detail.push(['', [
        '',
        'If you are behind a proxy, please make sure that the',
        "'proxy' config is set properly.  See: 'npm help config'",
      ].join('\n')])
      break

    case 'EACCES':
    case 'EPERM': {
      const isCachePath =
        typeof er.path === 'string' && npm.loaded && er.path.startsWith(npm.config.get('cache'))
      const isCacheDest =
        typeof er.dest === 'string' && npm.loaded && er.dest.startsWith(npm.config.get('cache'))

      if (process.platform !== 'win32' && (isCachePath || isCacheDest)) {
        // user probably doesn't need this, but still add it to the debug log
        log.verbose(er.stack)
        summary.push(['', [
          '',
          'Your cache folder contains root-owned files, due to a bug in',
          'previous versions of npm which has since been addressed.',
          '',
          'To permanently fix this problem, please run:',
          `  sudo chown -R ${process.getuid()}:${process.getgid()} "${npm.config.get('cache')}"`,
        ].join('\n')])
      } else {
        summary.push(['', er])
        detail.push(['', [
          '',
          'The operation was rejected by your operating system.',
          ...process.platform === 'win32' ? [
            "It's possible that the file was already in use (by a text editor or antivirus),",
            'or that you lack permissions to access it.',
          ] : [
            'It is likely you do not have the permissions to access this file as the current user',
          ],
          '',
          'If you believe this might be a permissions issue, please double-check the',
          'permissions of the file and its containing directories, or try running',
          'the command again as root/Administrator.',
        ].join('\n')])
      }
      break
    }

    case 'ENOGIT':
      summary.push(['', er.message])
      detail.push(['', [
        '',
        'Failed using git.',
        'Please check if you have git installed and in your PATH.',
      ].join('\n')])
      break

    case 'EJSONPARSE':
      // Check whether we ran into a conflict in our own package.json
      if (er.path === resolve(npm.prefix, 'package.json')) {
        const { isDiff } = require('parse-conflict-json')
        const txt = require('node:fs').readFileSync(er.path, 'utf8').replace(/\r\n/g, '\n')
        if (isDiff(txt)) {
          detail.push(['', [
            'Merge conflict detected in your package.json.',
            '',
            'Please resolve the package.json conflict and retry.',
          ].join('\n')])
          break
        }
      }
      summary.push(['JSON.parse', er.message])
      detail.push(['JSON.parse', [
        'Failed to parse JSON data.',
        'Note: package.json must be actual JSON, not just JavaScript.',
      ].join('\n')])
      break

    case 'EOTP':
    case 'E401':
      // E401 is for places where we accidentally neglect OTP stuff
      if (er.code === 'EOTP' || /one-time pass/.test(er.message)) {
        summary.push(['', 'This operation requires a one-time password from your authenticator.'])
        detail.push(['', [
          'You can provide a one-time password by passing --otp=<code> to the command you ran.',
          'If you already provided a one-time password then it is likely that you either typoed',
          'it, or it timed out. Please try again.',
        ].join('\n')])
      } else {
        // npm ERR! code E401
        // npm ERR! Unable to authenticate, need: Basic
        const auth = !er.headers || !er.headers['www-authenticate']
          ? []
          : er.headers['www-authenticate'].map(au => au.split(/[,\s]+/))[0]

        if (auth.includes('Bearer')) {
          summary.push(['',
            'Unable to authenticate, your authentication token seems to be invalid.',
          ])
          detail.push(['', [
            'To correct this please try logging in again with:',
            '  npm login',
          ].join('\n')])
        } else if (auth.includes('Basic')) {
          summary.push(['', 'Incorrect or missing password.'])
          detail.push(['', [
            'If you were trying to login, change your password, create an',
            'authentication token or enable two-factor authentication then',
            'that means you likely typed your password in incorrectly.',
            'Please try again, or recover your password at:',
            '  https://www.npmjs.com/forgot',
            '',
            'If you were doing some other operation then your saved credentials are',
            'probably out of date. To correct this please try logging in again with:',
            '  npm login',
          ].join('\n')])
        } else {
          summary.push(['', er.message || er])
        }
      }
      break

    case 'E404':
      // There's no need to have 404 in the message as well.
      summary.push(['404', er.message.replace(/^404\s+/, '')])
      if (er.pkgid && er.pkgid !== '-') {
        const pkg = er.pkgid.replace(/(?!^)@.*$/, '')

        detail.push(['404', ''])
        detail.push(['404', '', `'${replaceInfo(er.pkgid)}' is not in this registry.`])

        const nameValidator = require('validate-npm-package-name')
        const valResult = nameValidator(pkg)

        if (!valResult.validForNewPackages) {
          detail.push(['404', 'This package name is not valid, because', ''])

          const errorsArray = [...(valResult.errors || []), ...(valResult.warnings || [])]
          errorsArray.forEach((item, idx) => detail.push(['404', ' ' + (idx + 1) + '. ' + item]))
        }

        detail.push(['404', ''])
        detail.push(['404', 'Note that you can also install from a'])
        detail.push(['404', 'tarball, folder, http url, or git url.'])
      }
      break

    case 'EPUBLISHCONFLICT':
      summary.push(['publish fail', 'Cannot publish over existing version.'])
      detail.push(['publish fail', "Update the 'version' field in package.json and try again."])
      detail.push(['publish fail', ''])
      detail.push(['publish fail', 'To automatically increment version numbers, see:'])
      detail.push(['publish fail', '  npm help version'])
      break

    case 'EISGIT':
      summary.push(['git', er.message])
      summary.push(['git', `  ${er.path}`])
      detail.push(['git', [
        'Refusing to remove it. Update manually,',
        'or move it out of the way first.',
      ].join('\n')])
      break

    case 'EBADDEVENGINES': {
      const { current, required } = er
      summary.push(['EBADDEVENGINES', er.message])
      detail.push(['EBADDEVENGINES', { current, required }])
      break
    }

    case 'EBADPLATFORM': {
      const actual = er.current
      const expected = { ...er.required }
      const checkedKeys = []
      for (const key in expected) {
        if (Array.isArray(expected[key]) && expected[key].length > 0) {
          expected[key] = expected[key].join(',')
          checkedKeys.push(key)
        } else if (expected[key] === undefined ||
            Array.isArray(expected[key]) && expected[key].length === 0) {
          delete expected[key]
          delete actual[key]
        } else {
          checkedKeys.push(key)
        }
      }

      const longestKey = Math.max(...checkedKeys.map((key) => key.length))
      const detailEntry = []
      for (const key of checkedKeys) {
        const padding = key.length === longestKey
          ? 1
          : 1 + (longestKey - key.length)

        // padding + 1 because 'actual' is longer than 'valid'
        detailEntry.push(`Valid ${key}:${' '.repeat(padding + 1)}${expected[key]}`)
        detailEntry.push(`Actual ${key}:${' '.repeat(padding)}${actual[key]}`)
      }

      summary.push(['notsup', format(
        'Unsupported platform for %s: wanted %j (current: %j)',
        er.pkgid,
        expected,
        actual
      )])
      detail.push(['notsup', detailEntry.join('\n')])
      break
    }

    case 'EEXIST':
      summary.push(['', er.message])
      summary.push(['', 'File exists: ' + (er.dest || er.path)])
      detail.push(['', 'Remove the existing file and try again, or run npm'])
      detail.push(['', 'with --force to overwrite files recklessly.'])
      break

    case 'ENEEDAUTH':
      summary.push(['need auth', er.message])
      detail.push(['need auth', 'You need to authorize this machine using `npm adduser`'])
      break

    case 'ECONNRESET':
    case 'ENOTFOUND':
    case 'ETIMEDOUT':
    case 'ERR_SOCKET_TIMEOUT':
    case 'EAI_FAIL':
      summary.push(['network', er.message])
      detail.push(['network', [
        'This is a problem related to network connectivity.',
        'In most cases you are behind a proxy or have bad network settings.',
        '',
        'If you are behind a proxy, please make sure that the',
        "'proxy' config is set properly.  See: 'npm help config'",
      ].join('\n')])
      break

    case 'ETARGET':
      summary.push(['notarget', er.message])
      detail.push(['notarget', [
        'In most cases you or one of your dependencies are requesting',
        "a package version that doesn't exist.",
      ].join('\n')])
      break

    case 'E403':
      summary.push(['403', er.message])
      detail.push(['403', [
        'In most cases, you or one of your dependencies are requesting',
        'a package version that is forbidden by your security policy, or',
        'on a server you do not have access to.',
      ].join('\n')])
      break

    case 'EBADENGINE':
      summary.push(['engine', er.message])
      summary.push(['engine', 'Not compatible with your version of node/npm: ' + er.pkgid])
      detail.push(['notsup', [
        'Not compatible with your version of node/npm: ' + er.pkgid,
        'Required: ' + JSON.stringify(er.required),
        'Actual:   ' +
        JSON.stringify({ npm: npm.version, node: process.version }),
      ].join('\n')])
      break

    case 'ENOSPC':
      summary.push(['nospc', er.message])
      detail.push(['nospc', [
        'There appears to be insufficient space on your system to finish.',
        'Clear up some disk space and try again.',
      ].join('\n')])
      break

    case 'EROFS':
      summary.push(['rofs', er.message])
      detail.push(['rofs', [
        'Often virtualized file systems, or other file systems',
        "that don't support symlinks, give this error.",
      ].join('\n')])
      break

    case 'ENOENT':
      summary.push(['enoent', er.message])
      detail.push(['enoent', [
        'This is related to npm not being able to find a file.',
        er.file ? `\nCheck if the file '${er.file}' is present.` : '',
      ].join('\n')])
      break

    case 'EMISSINGARG':
    case 'EUNKNOWNTYPE':
    case 'EINVALIDTYPE':
    case 'ETOOMANYARGS':
      summary.push(['typeerror', er.stack])
      detail.push(['typeerror', [
        'This is an error with npm itself. Please report this error at:',
        '  https://github.com/npm/cli/issues',
      ].join('\n')])
      break

    default:
      summary.push(['', er.message || er])
      if (er.cause) {
        detail.push(['cause', er.cause.message])
      }
      if (er.signal) {
        detail.push(['signal', er.signal])
      }
      if (er.cmd && Array.isArray(er.args)) {
        detail.push(['command', ...[er.cmd, ...er.args.map(replaceInfo)]])
      }
      if (er.stdout) {
        detail.push(['', er.stdout.trim()])
      }
      if (er.stderr) {
        detail.push(['', er.stderr.trim()])
      }
      break
  }

  return {
    summary,
    detail,
    files,
  }
}

const getExitCodeFromError = (err) => {
  if (typeof err?.errno === 'number') {
    return err.errno
  } else if (typeof err?.code === 'number') {
    return err.code
  }
}

const getError = (err, { npm, command, pkg }) => {
  // if we got a command that just shells out to something else, then it
  // will presumably print its own errors and exit with a proper status
  // code if there's a problem.  If we got an error with a code=0, then...
  // something else went wrong along the way, so maybe an npm problem?
  if (command?.constructor?.isShellout && typeof err.code === 'number' && err.code) {
    return {
      exitCode: err.code,
      suppressError: true,
    }
  }

  // XXX: we should stop throwing strings
  if (typeof err === 'string') {
    return {
      exitCode: 1,
      suppressError: true,
      summary: [['', err]],
    }
  }

  // XXX: we should stop throwing other non-errors
  if (!(err instanceof Error)) {
    return {
      exitCode: 1,
      suppressError: true,
      summary: [['weird error', err]],
    }
  }

  if (err.code === 'EUNKNOWNCOMMAND') {
    const suggestions = require('./did-you-mean.js')(pkg, err.command)
    return {
      exitCode: 1,
      suppressError: true,
      standard: [
        `Unknown command: "${err.command}"`,
        suggestions,
        'To see a list of supported npm commands, run:',
        '  npm help',
      ],
    }
  }

  // Anything after this is not suppressed and get more logged information

  // add a code to the error if it doesnt have one and mutate some properties
  // so they have redacted information
  err.code ??= err.message.match(/^(?:Error: )?(E[A-Z]+)/)?.[1]
  // this mutates the error and redacts stack/message
  const { summary, detail, files } = errorMessage(err, npm)

  return {
    err,
    code: err.code,
    exitCode: getExitCodeFromError(err) || 1,
    suppressError: false,
    summary,
    detail,
    files,
    verbose: ['type', 'stack', 'statusCode', 'pkgid']
      .filter(k => err[k])
      .map(k => [k, replaceInfo(err[k])]),
    error: ['code', 'syscall', 'file', 'path', 'dest', 'errno']
      .filter(k => err[k])
      .map(k => [k, err[k]]),
  }
}

module.exports = {
  getExitCodeFromError,
  errorMessage,
  getError,
}
