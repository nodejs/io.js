const { resolve } = require('node:path')
const { stripVTControlCharacters } = require('node:util')
const pacote = require('pacote')
const table = require('text-table')
const npa = require('npm-package-arg')
const pickManifest = require('npm-pick-manifest')
const { output } = require('proc-log')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const ArboristWorkspaceCmd = require('../arborist-cmd.js')

const safeNpa = (spec) => {
  try {
    return npa(spec)
  } catch {
    return null
  }
}

// This string is load bearing and is shared with Arborist
const MISSING = 'MISSING'

class Outdated extends ArboristWorkspaceCmd {
  static description = 'Check for outdated packages'
  static name = 'outdated'
  static usage = ['[<package-spec> ...]']
  static params = [
    'all',
    'json',
    'long',
    'parseable',
    'global',
    'workspace',
  ]

  #tree
  #list = []
  #edges = new Set()
  #filterSet

  async exec (args) {
    const Arborist = require('@npmcli/arborist')
    const arb = new Arborist({
      ...this.npm.flatOptions,
      path: this.npm.global ? resolve(this.npm.globalDir, '..') : this.npm.prefix,
    })
    this.#tree = await arb.loadActual()

    if (this.workspaceNames?.length) {
      this.#filterSet = arb.workspaceDependencySet(
        this.#tree,
        this.workspaceNames,
        this.npm.flatOptions.includeWorkspaceRoot
      )
    } else if (!this.npm.flatOptions.workspacesEnabled) {
      this.#filterSet = arb.excludeWorkspacesDependencySet(this.#tree)
    }

    if (args.length) {
      for (const arg of args) {
        // specific deps
        this.#getEdges(this.#tree.inventory.query('name', arg), 'edgesIn')
      }
    } else {
      if (this.npm.config.get('all')) {
        // all deps in tree
        this.#getEdges(this.#tree.inventory.values(), 'edgesOut')
      }
      // top-level deps
      this.#getEdges()
    }

    await Promise.all([...this.#edges].map((e) => this.#getOutdatedInfo(e)))

    // sorts list alphabetically by name and then dependent
    const outdated = this.#list
      .sort((a, b) => localeCompare(a.name, b.name) || localeCompare(a.dependent, b.dependent))

    if (outdated.length) {
      process.exitCode = 1
    }

    if (this.npm.config.get('json')) {
      output.buffer(this.#json(outdated))
      return
    }

    const res = this.npm.config.get('parseable')
      ? this.#parseable(outdated)
      : this.#pretty(outdated)

    if (res) {
      output.standard(res)
    }
  }

  #getEdges (nodes, type) {
    // when no nodes are provided then it should only read direct deps
    // from the root node and its workspaces direct dependencies
    if (!nodes) {
      this.#getEdgesOut(this.#tree)
      this.#getWorkspacesEdges()
      return
    }

    for (const node of nodes) {
      if (type === 'edgesOut') {
        this.#getEdgesOut(node)
      } else {
        this.#getEdgesIn(node)
      }
    }
  }

  #getEdgesIn (node) {
    for (const edge of node.edgesIn) {
      this.#trackEdge(edge)
    }
  }

  #getEdgesOut (node) {
    // TODO: normalize usage of edges and avoid looping through nodes here
    const edges = this.npm.global ? node.children.values() : node.edgesOut.values()
    for (const edge of edges) {
      this.#trackEdge(edge)
    }
  }

  #trackEdge (edge) {
    if (edge.from && this.#filterSet?.size > 0 && !this.#filterSet.has(edge.from.target)) {
      return
    }
    this.#edges.add(edge)
  }

  #getWorkspacesEdges () {
    if (this.npm.global) {
      return
    }

    for (const edge of this.#tree.edgesOut.values()) {
      if (edge?.to?.target?.isWorkspace) {
        this.#getEdgesOut(edge.to.target)
      }
    }
  }

  async #getPackument (spec) {
    return pacote.packument(spec, {
      ...this.npm.flatOptions,
      fullMetadata: this.npm.config.get('long'),
      preferOnline: true,
    })
  }

  async #getOutdatedInfo (edge) {
    const alias = safeNpa(edge.spec)?.subSpec
    const spec = npa(alias ? alias.name : edge.name)
    const node = edge.to || edge
    const { path, location, package: { version: current } = {} } = node

    const type = edge.optional ? 'optionalDependencies'
      : edge.peer ? 'peerDependencies'
      : edge.dev ? 'devDependencies'
      : 'dependencies'

    for (const omitType of this.npm.flatOptions.omit) {
      if (node[omitType]) {
        return
      }
    }

    // deps different from prod not currently
    // on disk are not included in the output
    if (edge.error === MISSING && type !== 'dependencies') {
      return
    }

    // if it's not a range, version, or tag, skip it
    if (!safeNpa(`${edge.name}@${edge.spec}`)?.registry) {
      return null
    }

    try {
      const packument = await this.#getPackument(spec)
      const expected = alias ? alias.fetchSpec : edge.spec
      const wanted = pickManifest(packument, expected, this.npm.flatOptions)
      const latest = pickManifest(packument, '*', this.npm.flatOptions)
      if (!current || current !== wanted.version || wanted.version !== latest.version) {
        this.#list.push({
          name: alias ? edge.spec.replace('npm', edge.name) : edge.name,
          path,
          type,
          current,
          location,
          wanted: wanted.version,
          latest: latest.version,
          workspaceDependent: edge.from?.isWorkspace ? edge.from.pkgid : null,
          dependent: edge.from?.name ?? 'global',
          homepage: packument.homepage,
        })
      }
    } catch (err) {
      // silently catch and ignore ETARGET, E403 &
      // E404 errors, deps are just skipped
      if (!['ETARGET', 'E404', 'E404'].includes(err.code)) {
        throw err
      }
    }
  }

  // formatting functions

  #pretty (list) {
    if (!list.length) {
      return
    }

    const long = this.npm.config.get('long')
    const { bold, yellow, red, cyan, blue } = this.npm.chalk

    return table([
      [
        'Package',
        'Current',
        'Wanted',
        'Latest',
        'Location',
        'Depended by',
        ...long ? ['Package Type', 'Homepage'] : [],
      ].map(h => bold.underline(h)),
      ...list.map((d) => [
        d.current === d.wanted ? yellow(d.name) : red(d.name),
        d.current ?? 'MISSING',
        cyan(d.wanted),
        blue(d.latest),
        d.location ?? '-',
        d.workspaceDependent ? blue(d.workspaceDependent) : d.dependent,
        ...long ? [d.type, blue(d.homepage ?? '')] : [],
      ]),
    ], {
      align: ['l', 'r', 'r', 'r', 'l'],
      stringLength: s => stripVTControlCharacters(s).length,
    })
  }

  // --parseable creates output like this:
  // <fullpath>:<name@wanted>:<name@installed>:<name@latest>:<dependedby>
  #parseable (list) {
    return list.map(d => [
      d.path,
      `${d.name}@${d.wanted}`,
      d.current ? `${d.name}@${d.current}` : 'MISSING',
      `${d.name}@${d.latest}`,
      d.dependent,
      ...this.npm.config.get('long') ? [d.type, d.homepage] : [],
    ].join(':')).join('\n')
  }

  #json (list) {
    // TODO(BREAKING_CHANGE): this should just return an array. It's a list and
    // turing it into an object with keys is lossy since multiple items in the
    // list could have the same key. For now we hack that by only changing
    // top level values into arrays if they have multiple outdated items
    return list.reduce((acc, d) => {
      const dep = {
        current: d.current,
        wanted: d.wanted,
        latest: d.latest,
        dependent: d.dependent,
        location: d.path,
        ...this.npm.config.get('long') ? { type: d.type, homepage: d.homepage } : {},
      }
      acc[d.name] = acc[d.name]
        // If this item alread has an outdated dep then we turn it into an array
        ? (Array.isArray(acc[d.name]) ? acc[d.name] : [acc[d.name]]).concat(dep)
        : dep
      return acc
    }, {})
  }
}

module.exports = Outdated
