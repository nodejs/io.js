const npmAuditReport = require('npm-audit-report')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')
const auditError = require('../utils/audit-error.js')
const { log, output } = require('proc-log')
const reifyFinish = require('../utils/reify-finish.js')
const VerifySignatures = require('../utils/verify-signatures.js')

class Audit extends ArboristWorkspaceCmd {
  static description = 'Run a security audit'
  static name = 'audit'
  static params = [
    'audit-level',
    'dry-run',
    'force',
    'json',
    'package-lock-only',
    'package-lock',
    'omit',
    'include',
    'foreground-scripts',
    'ignore-scripts',
    ...super.params,
  ]

  static usage = ['[fix|signatures]']

  static async completion (opts) {
    const argv = opts.conf.argv.remain

    if (argv.length === 2) {
      return ['fix', 'signatures']
    }

    switch (argv[2]) {
      case 'fix':
      case 'signatures':
        return []
      default:
        throw Object.assign(new Error(argv[2] + ' not recognized'), {
          code: 'EUSAGE',
        })
    }
  }

  async exec (args) {
    if (args[0] === 'signatures') {
      await this.auditSignatures()
    } else {
      await this.auditAdvisories(args)
    }
  }

  async auditAdvisories (args) {
    const fix = args[0] === 'fix'
    if (this.npm.config.get('package-lock') === false && fix) {
      throw this.usageError('fix can not be used without a package-lock')
    }
    const reporter = this.npm.config.get('json') ? 'json' : 'detail'
    const Arborist = require('@npmcli/arborist')
    const opts = {
      ...this.npm.flatOptions,
      audit: true,
      path: this.npm.prefix,
      reporter,
      workspaces: this.workspaceNames,
    }

    const arb = new Arborist(opts)
    await arb.audit({ fix })
    if (fix) {
      await reifyFinish(this.npm, arb)
    } else {
      // will throw if there's an error, because this is an audit command
      auditError(this.npm, arb.auditReport)
      const result = npmAuditReport(arb.auditReport, {
        ...opts,
        chalk: this.npm.chalk,
      })
      process.exitCode = process.exitCode || result.exitCode
      output.standard(result.report)
    }
  }

  async auditSignatures () {
    if (this.npm.global) {
      throw Object.assign(
        new Error('`npm audit signatures` does not support global packages'), {
          code: 'EAUDITGLOBAL',
        }
      )
    }

    log.verbose('audit', 'loading installed dependencies')
    const Arborist = require('@npmcli/arborist')
    const opts = {
      ...this.npm.flatOptions,
      path: this.npm.prefix,
      workspaces: this.workspaceNames,
    }

    const arb = new Arborist(opts)
    const tree = await arb.loadActual()
    let filterSet = new Set()
    if (opts.workspaces && opts.workspaces.length) {
      filterSet =
        arb.workspaceDependencySet(
          tree,
          opts.workspaces,
          this.npm.flatOptions.includeWorkspaceRoot
        )
    } else if (!this.npm.flatOptions.workspacesEnabled) {
      filterSet =
        arb.excludeWorkspacesDependencySet(tree)
    }

    const verify = new VerifySignatures(tree, filterSet, this.npm, { ...opts })
    await verify.run()
  }
}

module.exports = Audit
