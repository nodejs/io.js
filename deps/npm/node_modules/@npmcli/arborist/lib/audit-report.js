// an object representing the set of vulnerabilities in a tree
/* eslint camelcase: "off" */

const localeCompare = require('@isaacs/string-locale-compare')('en')
const npa = require('npm-package-arg')
const pickManifest = require('npm-pick-manifest')

const Vuln = require('./vuln.js')
const Calculator = require('@npmcli/metavuln-calculator')

const _getReport = Symbol('getReport')
const _fixAvailable = Symbol('fixAvailable')
const _checkTopNode = Symbol('checkTopNode')
const _init = Symbol('init')
const _omit = Symbol('omit')
const { log, time } = require('proc-log')

const npmFetch = require('npm-registry-fetch')

class AuditReport extends Map {
  static load (tree, opts) {
    return new AuditReport(tree, opts).run()
  }

  get auditReportVersion () {
    return 2
  }

  toJSON () {
    const obj = {
      auditReportVersion: this.auditReportVersion,
      vulnerabilities: {},
      metadata: {
        vulnerabilities: {
          info: 0,
          low: 0,
          moderate: 0,
          high: 0,
          critical: 0,
          total: this.size,
        },
        dependencies: {
          prod: 0,
          dev: 0,
          optional: 0,
          peer: 0,
          peerOptional: 0,
          total: this.tree.inventory.size - 1,
        },
      },
    }

    for (const node of this.tree.inventory.values()) {
      const { dependencies } = obj.metadata
      let prod = true
      for (const type of [
        'dev',
        'optional',
        'peer',
        'peerOptional',
      ]) {
        if (node[type]) {
          dependencies[type]++
          prod = false
        }
      }
      if (prod) {
        dependencies.prod++
      }
    }

    // if it doesn't have any topVulns, then it's fixable with audit fix
    // for each topVuln, figure out if it's fixable with audit fix --force,
    // or if we have to just delete the thing, and if the fix --force will
    // require a semver major update.
    const vulnerabilities = []
    for (const [name, vuln] of this.entries()) {
      vulnerabilities.push([name, vuln.toJSON()])
      obj.metadata.vulnerabilities[vuln.severity]++
    }

    obj.vulnerabilities = vulnerabilities
      .sort(([a], [b]) => localeCompare(a, b))
      .reduce((set, [name, vuln]) => {
        set[name] = vuln
        return set
      }, {})

    return obj
  }

  constructor (tree, opts = {}) {
    super()
    const { omit } = opts
    this[_omit] = new Set(omit || [])
    this.topVulns = new Map()

    this.calculator = new Calculator(opts)
    this.error = null
    this.options = opts
    this.tree = tree
    this.filterSet = opts.filterSet
  }

  async run () {
    this.report = await this[_getReport]()
    log.silly('audit report', this.report)
    if (this.report) {
      await this[_init]()
    }
    return this
  }

  isVulnerable (node) {
    const vuln = this.get(node.packageName)
    return !!(vuln && vuln.isVulnerable(node))
  }

  async [_init] () {
    const timeEnd = time.start('auditReport:init')

    const promises = []
    for (const [name, advisories] of Object.entries(this.report)) {
      for (const advisory of advisories) {
        promises.push(this.calculator.calculate(name, advisory))
      }
    }

    // now the advisories are calculated with a set of versions
    // and the packument.  turn them into our style of vuln objects
    // which also have the affected nodes, and also create entries
    // for all the metavulns that we find from dependents.
    const advisories = new Set(await Promise.all(promises))
    const seen = new Set()
    for (const advisory of advisories) {
      const { name, range } = advisory
      const k = `${name}@${range}`

      const vuln = this.get(name) || new Vuln({ name, advisory })
      if (this.has(name)) {
        vuln.addAdvisory(advisory)
      }
      super.set(name, vuln)

      // don't flag the exact same name/range more than once
      // adding multiple advisories with the same range is fine, but no
      // need to search for nodes we already would have added.
      if (!seen.has(k)) {
        const p = []
        for (const node of this.tree.inventory.query('packageName', name)) {
          if (!shouldAudit(node, this[_omit], this.filterSet)) {
            continue
          }

          // if not vulnerable by this advisory, keep searching
          if (!advisory.testVersion(node.version)) {
            continue
          }

          // we will have loaded the source already if this is a metavuln
          if (advisory.type === 'metavuln') {
            vuln.addVia(this.get(advisory.dependency))
          }

          // already marked this one, no need to do it again
          if (vuln.nodes.has(node)) {
            continue
          }

          // haven't marked this one yet.  get its dependents.
          vuln.nodes.add(node)
          for (const { from: dep, spec } of node.edgesIn) {
            if (dep.isTop && !vuln.topNodes.has(dep)) {
              this[_checkTopNode](dep, vuln, spec)
            } else {
            // calculate a metavuln, if necessary
              const calc = this.calculator.calculate(dep.packageName, advisory)
              // eslint-disable-next-line promise/always-return
              p.push(calc.then(meta => {
                // eslint-disable-next-line promise/always-return
                if (meta.testVersion(dep.version, spec)) {
                  advisories.add(meta)
                }
              }))
            }
          }
        }
        await Promise.all(p)
        seen.add(k)
      }

      // make sure we actually got something.  if not, remove it
      // this can happen if you are loading from a lockfile created by
      // npm v5, since it lists the current version of all deps,
      // rather than the range that is actually depended upon,
      // or if using --omit with the older audit endpoint.
      if (this.get(name).nodes.size === 0) {
        this.delete(name)
        continue
      }

      // if the vuln is valid, but THIS advisory doesn't apply to any of
      // the nodes it references, then remove it from the advisory list.
      // happens when using omit with old audit endpoint.
      for (const advisory of vuln.advisories) {
        const relevant = [...vuln.nodes]
          .some(n => advisory.testVersion(n.version))
        if (!relevant) {
          vuln.deleteAdvisory(advisory)
        }
      }
    }

    timeEnd()
  }

  [_checkTopNode] (topNode, vuln, spec) {
    vuln.fixAvailable = this[_fixAvailable](topNode, vuln, spec)

    if (vuln.fixAvailable !== true) {
      // now we know the top node is vulnerable, and cannot be
      // upgraded out of the bad place without --force.  But, there's
      // no need to add it to the actual vulns list, because nothing
      // depends on root.
      this.topVulns.set(vuln.name, vuln)
      vuln.topNodes.add(topNode)
    }
  }

  // check whether the top node is vulnerable.
  // check whether we can get out of the bad place with --force, and if
  // so, whether that update is SemVer Major
  [_fixAvailable] (topNode, vuln, spec) {
    // this will always be set to at least {name, versions:{}}
    const paku = vuln.packument

    if (!vuln.testSpec(spec)) {
      return true
    }

    // similarly, even if we HAVE a packument, but we're looking for it
    // somewhere other than the registry, and we got something vulnerable,
    // then we're stuck with it.
    const specObj = npa(spec)
    if (!specObj.registry) {
      return false
    }

    if (specObj.subSpec) {
      spec = specObj.subSpec.rawSpec
    }

    // We don't provide fixes for top nodes other than root, but we
    // still check to see if the node is fixable with a different version,
    // and if that is a semver major bump.
    try {
      const {
        _isSemVerMajor: isSemVerMajor,
        version,
        name,
      } = pickManifest(paku, spec, {
        ...this.options,
        before: null,
        avoid: vuln.range,
        avoidStrict: true,
      })
      return { name, version, isSemVerMajor }
    } catch (er) {
      return false
    }
  }

  set () {
    throw new Error('do not call AuditReport.set() directly')
  }

  async [_getReport] () {
    // if we're not auditing, just return false
    if (this.options.audit === false || this.options.offline === true || this.tree.inventory.size === 1) {
      return null
    }

    const timeEnd = time.start('auditReport:getReport')
    try {
      const body = prepareBulkData(this.tree, this[_omit], this.filterSet)
      log.silly('audit', 'bulk request', body)

      // no sense asking if we don't have anything to audit,
      // we know it'll be empty
      if (!Object.keys(body).length) {
        return null
      }

      const res = await npmFetch('/-/npm/v1/security/advisories/bulk', {
        ...this.options,
        registry: this.options.auditRegistry || this.options.registry,
        method: 'POST',
        gzip: true,
        body,
      })

      return await res.json()
    } catch (er) {
      log.verbose('audit error', er)
      log.silly('audit error', String(er.body))
      this.error = er
      return null
    } finally {
      timeEnd()
    }
  }
}

// return true if we should audit this one
const shouldAudit = (node, omit, filterSet) =>
  !node.version ? false
  : node.isRoot ? false
  : filterSet && filterSet.size !== 0 && !filterSet.has(node) ? false
  : omit.size === 0 ? true
  : !( // otherwise, just ensure we're not omitting this one
    node.dev && omit.has('dev') ||
    node.optional && omit.has('optional') ||
    node.devOptional && omit.has('dev') && omit.has('optional') ||
    node.peer && omit.has('peer')
  )

const prepareBulkData = (tree, omit, filterSet) => {
  const payload = {}
  for (const name of tree.inventory.query('packageName')) {
    const set = new Set()
    for (const node of tree.inventory.query('packageName', name)) {
      if (!shouldAudit(node, omit, filterSet)) {
        continue
      }

      set.add(node.version)
    }
    if (set.size) {
      payload[name] = [...set]
    }
  }
  return payload
}

module.exports = AuditReport
