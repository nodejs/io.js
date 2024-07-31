// An object representing a vulnerability either as the result of an
// advisory or due to the package in question depending exclusively on
// vulnerable versions of a dep.
//
// - name: package name
// - range: Set of vulnerable versions
// - nodes: Set of nodes affected
// - effects: Set of vulns triggered by this one
// - advisories: Set of advisories (including metavulns) causing this vuln.
//   All of the entries in via are vulnerability objects returned by
//   @npmcli/metavuln-calculator
// - via: dependency vulns which cause this one

const { satisfies, simplifyRange } = require('semver')
const semverOpt = { loose: true, includePrerelease: true }

const localeCompare = require('@isaacs/string-locale-compare')('en')
const npa = require('npm-package-arg')

const severities = new Map([
  ['info', 0], [0, 'info'],
  ['low', 1], [1, 'low'],
  ['moderate', 2], [2, 'moderate'],
  ['high', 3], [3, 'high'],
  ['critical', 4], [4, 'critical'],
  [null, -1], [-1, null],
])

class Vuln {
  #range = null
  #simpleRange = null
  // assume a fix is available unless it hits a top node
  // that locks it in place, setting this false or {isSemVerMajor, version}.
  #fixAvailable = true

  constructor ({ name, advisory }) {
    this.name = name
    this.via = new Set()
    this.advisories = new Set()
    this.severity = null
    this.effects = new Set()
    this.topNodes = new Set()
    this.nodes = new Set()
    this.addAdvisory(advisory)
    this.packument = advisory.packument
    this.versions = advisory.versions
  }

  get fixAvailable () {
    return this.#fixAvailable
  }

  set fixAvailable (f) {
    this.#fixAvailable = f
    // if there's a fix available for this at the top level, it means that
    // it will also fix the vulns that led to it being there.  to get there,
    // we set the vias to the most "strict" of fix availables.
    // - false: no fix is available
    // - {name, version, isSemVerMajor} fix requires -f, is semver major
    // - {name, version} fix requires -f, not semver major
    // - true: fix does not require -f
    // TODO: duped entries may require different fixes but the current
    // structure does not support this, so the case were a top level fix
    // corrects a duped entry may mean you have to run fix more than once
    for (const v of this.via) {
      // don't blow up on loops
      if (v.fixAvailable === f) {
        continue
      }

      if (f === false) {
        v.fixAvailable = f
      } else if (v.fixAvailable === true) {
        v.fixAvailable = f
      } else if (typeof f === 'object' && (
        typeof v.fixAvailable !== 'object' || !v.fixAvailable.isSemVerMajor)) {
        v.fixAvailable = f
      }
    }
  }

  get isDirect () {
    for (const node of this.nodes.values()) {
      for (const edge of node.edgesIn) {
        if (edge.from.isProjectRoot || edge.from.isWorkspace) {
          return true
        }
      }
    }
    return false
  }

  testSpec (spec) {
    const specObj = npa(spec)
    if (!specObj.registry) {
      return true
    }

    if (specObj.subSpec) {
      spec = specObj.subSpec.rawSpec
    }

    for (const v of this.versions) {
      if (satisfies(v, spec) && !satisfies(v, this.range, semverOpt)) {
        return false
      }
    }
    return true
  }

  toJSON () {
    return {
      name: this.name,
      severity: this.severity,
      isDirect: this.isDirect,
      // just loop over the advisories, since via is only Vuln objects,
      // and calculated advisories have all the info we need
      via: [...this.advisories].map(v => v.type === 'metavuln' ? v.dependency : {
        ...v,
        versions: undefined,
        vulnerableVersions: undefined,
        id: undefined,
      }).sort((a, b) =>
        localeCompare(String(a.source || a), String(b.source || b))),
      effects: [...this.effects].map(v => v.name).sort(localeCompare),
      range: this.simpleRange,
      nodes: [...this.nodes].map(n => n.location).sort(localeCompare),
      fixAvailable: this.#fixAvailable,
    }
  }

  addVia (v) {
    this.via.add(v)
    v.effects.add(this)
    // call the setter since we might add vias _after_ setting fixAvailable
    this.fixAvailable = this.fixAvailable
  }

  deleteVia (v) {
    this.via.delete(v)
    v.effects.delete(this)
  }

  deleteAdvisory (advisory) {
    this.advisories.delete(advisory)
    // make sure we have the max severity of all the vulns causing this one
    this.severity = null
    this.#range = null
    this.#simpleRange = null
    // refresh severity
    for (const advisory of this.advisories) {
      this.addAdvisory(advisory)
    }

    // remove any effects that are no longer relevant
    const vias = new Set([...this.advisories].map(a => a.dependency))
    for (const via of this.via) {
      if (!vias.has(via.name)) {
        this.deleteVia(via)
      }
    }
  }

  addAdvisory (advisory) {
    this.advisories.add(advisory)
    const sev = severities.get(advisory.severity)
    this.#range = null
    this.#simpleRange = null
    if (sev > severities.get(this.severity)) {
      this.severity = advisory.severity
    }
  }

  get range () {
    if (!this.#range) {
      this.#range = [...this.advisories].map(v => v.range).join(' || ')
    }
    return this.#range
  }

  get simpleRange () {
    if (this.#simpleRange && this.#simpleRange === this.#range) {
      return this.#simpleRange
    }

    const versions = [...this.advisories][0].versions
    const range = this.range
    this.#simpleRange = simplifyRange(versions, range, semverOpt)
    this.#range = this.#simpleRange
    return this.#simpleRange
  }

  isVulnerable (node) {
    if (this.nodes.has(node)) {
      return true
    }

    const { version } = node.package
    if (!version) {
      return false
    }

    for (const v of this.advisories) {
      if (v.testVersion(version)) {
        this.nodes.add(node)
        return true
      }
    }

    return false
  }
}

module.exports = Vuln
