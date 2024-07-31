const localeCompare = require('@isaacs/string-locale-compare')('en')
const BaseCommand = require('../base-cmd.js')
const { log, output } = require('proc-log')
const { cyclonedxOutput } = require('../utils/sbom-cyclonedx.js')
const { spdxOutput } = require('../utils/sbom-spdx.js')

const SBOM_FORMATS = ['cyclonedx', 'spdx']

class SBOM extends BaseCommand {
  #response = {} // response is the sbom response

  static description = 'Generate a Software Bill of Materials (SBOM)'
  static name = 'sbom'
  static workspaces = true

  static params = [
    'omit',
    'package-lock-only',
    'sbom-format',
    'sbom-type',
    'workspace',
    'workspaces',
  ]

  async exec () {
    const sbomFormat = this.npm.config.get('sbom-format')
    const packageLockOnly = this.npm.config.get('package-lock-only')

    if (!sbomFormat) {
      /* eslint-disable-next-line max-len */
      throw this.usageError(`Must specify --sbom-format flag with one of: ${SBOM_FORMATS.join(', ')}.`)
    }

    const opts = {
      ...this.npm.flatOptions,
      path: this.npm.prefix,
      forceActual: true,
    }
    const Arborist = require('@npmcli/arborist')
    const arb = new Arborist(opts)

    const tree = packageLockOnly ? await arb.loadVirtual(opts).catch(() => {
      /* eslint-disable-next-line max-len */
      throw this.usageError('A package lock or shrinkwrap file is required in package-lock-only mode')
    }) : await arb.loadActual(opts)

    // Collect the list of selected workspaces in the project
    const wsNodes = this.workspaceNames?.length
      ? arb.workspaceNodes(tree, this.workspaceNames)
      : null

    // Build the selector and query the tree for the list of nodes
    const selector = this.#buildSelector({ wsNodes })
    log.info('sbom', `Using dependency selector: ${selector}`)
    const items = await tree.querySelectorAll(selector)

    const errors = items.flatMap(node => detectErrors(node))
    if (errors.length) {
      throw Object.assign(new Error([...new Set(errors)].join('\n')), {
        code: 'ESBOMPROBLEMS',
      })
    }

    // Populate the response with the list of unique nodes (sorted by location)
    this.#buildResponse(items.sort((a, b) => localeCompare(a.location, b.location)))

    // TODO(BREAKING_CHANGE): all sbom output is in json mode but setting it before
    // any of the errors will cause those to be thrown in json mode.
    this.npm.config.set('json', true)
    output.buffer(this.#response)
  }

  async execWorkspaces (args) {
    await this.setWorkspaces()
    return this.exec(args)
  }

  // Build the selector from all of the specified filter options
  #buildSelector ({ wsNodes }) {
    let selector
    const omit = this.npm.flatOptions.omit
    const workspacesEnabled = this.npm.flatOptions.workspacesEnabled

    // If omit is specified, omit all nodes and their children which match the
    // specified selectors
    const omits = omit.reduce((acc, o) => `${acc}:not(.${o})`, '')

    if (!workspacesEnabled) {
      // If workspaces are disabled, omit all workspace nodes and their children
      selector = `:root > :not(.workspace)${omits},:root > :not(.workspace) *${omits},:extraneous`
    } else if (wsNodes && wsNodes.length > 0) {
      // If one or more workspaces are selected, select only those workspaces and their children
      selector = wsNodes.map(ws => `#${ws.name},#${ws.name} *${omits}`).join(',')
    } else {
      selector = `:root *${omits},:extraneous`
    }

    // Always include the root node
    return `:root,${selector}`
  }

  // builds a normalized inventory
  #buildResponse (items) {
    const sbomFormat = this.npm.config.get('sbom-format')
    const packageType = this.npm.config.get('sbom-type')
    const packageLockOnly = this.npm.config.get('package-lock-only')

    this.#response = sbomFormat === 'cyclonedx'
      ? cyclonedxOutput({ npm: this.npm, nodes: items, packageType, packageLockOnly })
      : spdxOutput({ npm: this.npm, nodes: items, packageType })
  }
}

const detectErrors = (node) => {
  const errors = []

  // Look for missing dependencies (that are NOT optional), or invalid dependencies
  for (const edge of node.edgesOut.values()) {
    if (edge.missing && !(edge.type === 'optional' || edge.type === 'peerOptional')) {
      errors.push(`missing: ${edge.name}@${edge.spec}, required by ${edge.from.pkgid}`)
    }

    if (edge.invalid) {
      /* istanbul ignore next */
      const spec = edge.spec || '*'
      const from = edge.from.pkgid
      errors.push(`invalid: ${edge.to.pkgid}, ${spec} required by ${from}`)
    }
  }

  return errors
}

module.exports = SBOM
