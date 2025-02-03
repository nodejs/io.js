const archy = require('archy')
const pacote = require('pacote')
const semver = require('semver')
const { output } = require('proc-log')
const npa = require('npm-package-arg')
const { depth } = require('treeverse')
const { readTree: getFundingInfo, normalizeFunding, isValidFunding } = require('libnpmfund')
const { openUrl } = require('../utils/open-url.js')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')

const getPrintableName = ({ name, version }) => {
  const printableVersion = version ? `@${version}` : ''
  return `${name}${printableVersion}`
}

const errCode = (msg, code) => Object.assign(new Error(msg), { code })

class Fund extends ArboristWorkspaceCmd {
  static description = 'Retrieve funding information'
  static name = 'fund'
  static params = ['json', 'browser', 'unicode', 'workspace', 'which']
  static usage = ['[<package-spec>]']

  // XXX: maybe worth making this generic for all commands?
  usageMessage (paramsObj = {}) {
    let msg = `\`npm ${this.constructor.name}`
    const params = Object.entries(paramsObj)
    if (params.length) {
      msg += ` ${this.constructor.usage}`
    }
    for (const [key, value] of params) {
      msg += ` --${key}=${value}`
    }
    return `${msg}\``
  }

  // TODO
  /* istanbul ignore next */
  static async completion (opts, npm) {
    const completion = require('../utils/installed-deep.js')
    return completion(npm, opts)
  }

  async exec (args) {
    const spec = args[0]

    let fundingSourceNumber = this.npm.config.get('which')
    if (fundingSourceNumber != null) {
      fundingSourceNumber = parseInt(fundingSourceNumber, 10)
      if (isNaN(fundingSourceNumber) || fundingSourceNumber < 1) {
        throw errCode(
          `${this.usageMessage({ which: 'fundingSourceNumber' })} must be given a positive integer`,
          'EFUNDNUMBER'
        )
      }
    }

    if (this.npm.global) {
      throw errCode(
        `${this.usageMessage()} does not support global packages`,
        'EFUNDGLOBAL'
      )
    }

    const where = this.npm.prefix
    const Arborist = require('@npmcli/arborist')
    const arb = new Arborist({ ...this.npm.flatOptions, path: where })
    const tree = await arb.loadActual()

    if (spec) {
      await this.openFundingUrl({
        path: where,
        tree,
        spec,
        fundingSourceNumber,
      })
      return
    }

    // TODO: add !workspacesEnabled option handling to libnpmfund
    const fundingInfo = getFundingInfo(tree, {
      ...this.flatOptions,
      Arborist,
      workspaces: this.workspaceNames,
    })

    if (this.npm.config.get('json')) {
      output.buffer(fundingInfo)
    } else {
      output.standard(this.printHuman(fundingInfo))
    }
  }

  printHuman (fundingInfo) {
    const unicode = this.npm.config.get('unicode')
    const seenUrls = new Map()

    const tree = obj => archy(obj, '', { unicode })

    const result = depth({
      tree: fundingInfo,

      // composes human readable package name
      // and creates a new archy item for readable output
      visit: ({ name, version, funding }) => {
        const [fundingSource] = [].concat(normalizeFunding(funding)).filter(isValidFunding)
        const { url } = fundingSource || {}
        const pkgRef = getPrintableName({ name, version })

        if (!url) {
          return { label: pkgRef }
        }
        let item
        if (seenUrls.has(url)) {
          item = seenUrls.get(url)
          item.label += `${this.npm.chalk.dim(',')} ${pkgRef}`
          return null
        }
        item = {
          label: tree({
            label: this.npm.chalk.blue(url),
            nodes: [pkgRef],
          }).trim(),
        }

        // stacks all packages together under the same item
        seenUrls.set(url, item)
        return item
      },

      // puts child nodes back into returned archy
      // output while also filtering out missing items
      leave: (item, children) => {
        if (item) {
          item.nodes = children.filter(Boolean)
        }

        return item
      },

      // turns tree-like object return by libnpmfund
      // into children to be properly read by treeverse
      getChildren: node =>
        Object.keys(node.dependencies || {}).map(key => ({
          name: key,
          ...node.dependencies[key],
        })),
    })

    const res = tree(result)
    return res
  }

  async openFundingUrl ({ path, tree, spec, fundingSourceNumber }) {
    const arg = npa(spec, path)

    const retrievePackageMetadata = () => {
      if (arg.type === 'directory') {
        if (tree.path === arg.fetchSpec) {
          // matches cwd, e.g: npm fund .
          return tree.package
        } else {
          // matches any file path within current arborist inventory
          for (const item of tree.inventory.values()) {
            if (item.path === arg.fetchSpec) {
              return item.package
            }
          }
        }
      } else {
        // tries to retrieve a package from arborist inventory
        // by matching resulted package name from the provided spec
        const [item] = [...tree.inventory.query('name', arg.name)]
          .filter(i => semver.valid(i.package.version))
          .sort((a, b) => semver.rcompare(a.package.version, b.package.version))

        if (item) {
          return item.package
        }
      }
    }

    const { funding } =
      retrievePackageMetadata() ||
      (await pacote.manifest(arg, this.npm.flatOptions).catch(() => ({})))

    const validSources = [].concat(normalizeFunding(funding)).filter(isValidFunding)

    if (!validSources.length) {
      throw errCode(`No valid funding method available for: ${spec}`, 'ENOFUND')
    }

    const fundSource = fundingSourceNumber
      ? validSources[fundingSourceNumber - 1]
      : validSources.length === 1 ? validSources[0]
      : null

    if (fundSource) {
      return openUrl(this.npm, ...this.urlMessage(fundSource))
    }

    const ambiguousUrlMsg = [
      ...validSources.map((s, i) => `${i + 1}: ${this.urlMessage(s).reverse().join(': ')}`),
      `Run ${this.usageMessage({ which: '1' })}` +
      ', for example, to open the first funding URL listed in that package',
    ]
    if (fundingSourceNumber) {
      ambiguousUrlMsg.unshift(`--which=${fundingSourceNumber} is not a valid index`)
    }
    output.standard(ambiguousUrlMsg.join('\n'))
  }

  urlMessage (source) {
    const { type, url } = source
    const typePrefix = type ? `${type} funding` : 'Funding'
    const message = `${typePrefix} available at the following URL`
    return [url, message]
  }
}

module.exports = Fund
