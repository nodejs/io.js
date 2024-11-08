const { output } = require('proc-log')
const pkgJson = require('@npmcli/package-json')
const BaseCommand = require('../base-cmd.js')
const { getError } = require('../utils/error-message.js')
const { outputError } = require('../utils/output-error.js')

class RunScript extends BaseCommand {
  static description = 'Run arbitrary package scripts'
  static params = [
    'workspace',
    'workspaces',
    'include-workspace-root',
    'if-present',
    'ignore-scripts',
    'foreground-scripts',
    'script-shell',
  ]

  static name = 'run-script'
  static usage = ['<command> [-- <args>]']
  static workspaces = true
  static ignoreImplicitWorkspace = false
  static isShellout = true
  static checkDevEngines = true

  static async completion (opts, npm) {
    const argv = opts.conf.argv.remain
    if (argv.length === 2) {
      const { content: { scripts = {} } } = await pkgJson.normalize(npm.localPrefix)
        .catch(() => ({ content: {} }))
      if (opts.isFish) {
        return Object.keys(scripts).map(s => `${s}\t${scripts[s].slice(0, 30)}`)
      }
      return Object.keys(scripts)
    }
  }

  async exec (args) {
    if (args.length) {
      await this.#run(args, { path: this.npm.localPrefix })
    } else {
      await this.#list(this.npm.localPrefix)
    }
  }

  async execWorkspaces (args) {
    await this.setWorkspaces()

    const ws = [...this.workspaces.entries()]
    for (const [workspace, path] of ws) {
      const last = path === ws.at(-1)[1]

      if (!args.length) {
        const newline = await this.#list(path, { workspace })
        if (newline && !last) {
          output.standard('')
        }
        continue
      }

      const pkg = await pkgJson.normalize(path).then(p => p.content)
      try {
        await this.#run(args, { path, pkg, workspace })
      } catch (e) {
        const err = getError(e, { npm: this.npm, command: null })
        outputError({
          ...err,
          error: [
            ['', `Lifecycle script \`${args[0]}\` failed with error:`],
            ...err.error,
            ['workspace', pkg._id || pkg.name],
            ['location', path],
          ],
        })
        process.exitCode = err.exitCode
        if (!last) {
          output.error('')
        }
      }
    }
  }

  async #run ([event, ...args], { path, pkg, workspace }) {
    const runScript = require('@npmcli/run-script')

    pkg ??= await pkgJson.normalize(path).then(p => p.content)

    const { scripts = {} } = pkg

    if (event === 'restart' && !scripts.restart) {
      scripts.restart = 'npm stop --if-present && npm start'
    } else if (event === 'env' && !scripts.env) {
      const { isWindowsShell } = require('../utils/is-windows.js')
      scripts.env = isWindowsShell ? 'SET' : 'env'
    }

    pkg.scripts = scripts

    if (
      !Object.prototype.hasOwnProperty.call(scripts, event) &&
      !(event === 'start' && (await runScript.isServerPackage(path)))
    ) {
      if (this.npm.config.get('if-present')) {
        return
      }

      const suggestions = require('../utils/did-you-mean.js')(pkg, event)
      const wsArg = workspace && path !== this.npm.localPrefix
        ? ` --workspace=${pkg._id || pkg.name}`
        : ''
      throw new Error([
        `Missing script: "${event}"${suggestions}\n`,
        'To see a list of scripts, run:',
        `  npm run${wsArg}`,
      ].join('\n'))
    }

    // positional args only added to the main event, not pre/post
    const events = [[event, args]]
    if (!this.npm.config.get('ignore-scripts')) {
      if (scripts[`pre${event}`]) {
        events.unshift([`pre${event}`, []])
      }

      if (scripts[`post${event}`]) {
        events.push([`post${event}`, []])
      }
    }

    for (const [ev, evArgs] of events) {
      await runScript({
        path,
        // this || undefined is because runScript will be unhappy with the
        // default null value
        scriptShell: this.npm.config.get('script-shell') || undefined,
        stdio: 'inherit',
        pkg,
        event: ev,
        args: evArgs,
      })
    }
  }

  async #list (path, { workspace } = {}) {
    const { scripts = {}, name, _id } = await pkgJson.normalize(path).then(p => p.content)
    const scriptEntries = Object.entries(scripts)

    if (this.npm.silent) {
      return
    }

    if (this.npm.config.get('json')) {
      output.buffer(workspace ? { [workspace]: scripts } : scripts)
      return
    }

    if (!scriptEntries.length) {
      return
    }

    if (this.npm.config.get('parseable')) {
      output.standard(scriptEntries
        .map((s) => (workspace ? [workspace, ...s] : s).join(':'))
        .join('\n')
        .trim())
      return
    }

    const cmdList = [
      'prepare', 'prepublishOnly',
      'prepack', 'postpack',
      'dependencies',
      'preinstall', 'install', 'postinstall',
      'prepublish', 'publish', 'postpublish',
      'prerestart', 'restart', 'postrestart',
      'prestart', 'start', 'poststart',
      'prestop', 'stop', 'poststop',
      'pretest', 'test', 'posttest',
      'preuninstall', 'uninstall', 'postuninstall',
      'preversion', 'version', 'postversion',
    ]
    const [cmds, runScripts] = scriptEntries.reduce((acc, s) => {
      acc[cmdList.includes(s[0]) ? 0 : 1].push(s)
      return acc
    }, [[], []])

    const { reset, bold, cyan, dim, blue } = this.npm.chalk
    const pkgId = `in ${cyan(_id || name)}`
    const title = (t) => reset(bold(t))

    if (cmds.length) {
      output.standard(`${title('Lifecycle scripts')} included ${pkgId}:`)
      for (const [k, v] of cmds) {
        output.standard(`  ${k}`)
        output.standard(`    ${dim(v)}`)
      }
    }

    if (runScripts.length) {
      const via = `via \`${blue('npm run-script')}\`:`
      if (!cmds.length) {
        output.standard(`${title('Scripts')} available ${pkgId} ${via}`)
      } else {
        output.standard(`available ${via}`)
      }
      for (const [k, v] of runScripts) {
        output.standard(`  ${k}`)
        output.standard(`    ${dim(v)}`)
      }
    }

    // Return true to indicate that something was output for this path
    // that should be separated from others
    return true
  }
}

module.exports = RunScript
