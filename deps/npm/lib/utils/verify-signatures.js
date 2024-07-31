const fetch = require('npm-registry-fetch')
const localeCompare = require('@isaacs/string-locale-compare')('en')
const npa = require('npm-package-arg')
const pacote = require('pacote')
const pMap = require('p-map')
const tufClient = require('@sigstore/tuf')
const { log, output } = require('proc-log')

const sortAlphabetically = (a, b) => localeCompare(a.name, b.name)

class VerifySignatures {
  constructor (tree, filterSet, npm, opts) {
    this.tree = tree
    this.filterSet = filterSet
    this.npm = npm
    this.opts = opts
    this.keys = new Map()
    this.invalid = []
    this.missing = []
    this.checkedPackages = new Set()
    this.auditedWithKeysCount = 0
    this.verifiedSignatureCount = 0
    this.verifiedAttestationCount = 0
    this.exitCode = 0
  }

  async run () {
    const start = process.hrtime.bigint()

    // Find all deps in tree
    const { edges, registries } = this.getEdgesOut(this.tree.inventory.values(), this.filterSet)
    if (edges.size === 0) {
      throw new Error('found no installed dependencies to audit')
    }

    const tuf = await tufClient.initTUF({
      cachePath: this.opts.tufCache,
      retry: this.opts.retry,
      timeout: this.opts.timeout,
    })
    await Promise.all([...registries].map(registry => this.setKeys({ registry, tuf })))

    log.verbose('verifying registry signatures')
    await pMap(edges, (e) => this.getVerifiedInfo(e), { concurrency: 20, stopOnError: true })

    // Didn't find any dependencies that could be verified, e.g. only local
    // deps, missing version, not on a registry etc.
    if (!this.auditedWithKeysCount) {
      throw new Error('found no dependencies to audit that were installed from ' +
                      'a supported registry')
    }

    const invalid = this.invalid.sort(sortAlphabetically)
    const missing = this.missing.sort(sortAlphabetically)

    const hasNoInvalidOrMissing = invalid.length === 0 && missing.length === 0

    if (!hasNoInvalidOrMissing) {
      process.exitCode = 1
    }

    if (this.npm.config.get('json')) {
      output.buffer({ invalid, missing })
      return
    }
    const end = process.hrtime.bigint()
    const elapsed = end - start

    const auditedPlural = this.auditedWithKeysCount > 1 ? 's' : ''
    const timing = `audited ${this.auditedWithKeysCount} package${auditedPlural} in ` +
      `${Math.floor(Number(elapsed) / 1e9)}s`
    output.standard(timing)
    output.standard('')

    const verifiedBold = this.npm.chalk.bold('verified')
    if (this.verifiedSignatureCount) {
      if (this.verifiedSignatureCount === 1) {
        /* eslint-disable-next-line max-len */
        output.standard(`${this.verifiedSignatureCount} package has a ${verifiedBold} registry signature`)
      } else {
        /* eslint-disable-next-line max-len */
        output.standard(`${this.verifiedSignatureCount} packages have ${verifiedBold} registry signatures`)
      }
      output.standard('')
    }

    if (this.verifiedAttestationCount) {
      if (this.verifiedAttestationCount === 1) {
        /* eslint-disable-next-line max-len */
        output.standard(`${this.verifiedAttestationCount} package has a ${verifiedBold} attestation`)
      } else {
        /* eslint-disable-next-line max-len */
        output.standard(`${this.verifiedAttestationCount} packages have ${verifiedBold} attestations`)
      }
      output.standard('')
    }

    if (missing.length) {
      const missingClr = this.npm.chalk.redBright('missing')
      if (missing.length === 1) {
        /* eslint-disable-next-line max-len */
        output.standard(`1 package has a ${missingClr} registry signature but the registry is providing signing keys:`)
      } else {
        /* eslint-disable-next-line max-len */
        output.standard(`${missing.length} packages have ${missingClr} registry signatures but the registry is providing signing keys:`)
      }
      output.standard('')
      missing.map(m =>
        output.standard(`${this.npm.chalk.red(`${m.name}@${m.version}`)} (${m.registry})`)
      )
    }

    if (invalid.length) {
      if (missing.length) {
        output.standard('')
      }
      const invalidClr = this.npm.chalk.redBright('invalid')
      // We can have either invalid signatures or invalid provenance
      const invalidSignatures = this.invalid.filter(i => i.code === 'EINTEGRITYSIGNATURE')
      if (invalidSignatures.length) {
        if (invalidSignatures.length === 1) {
          output.standard(`1 package has an ${invalidClr} registry signature:`)
        } else {
          /* eslint-disable-next-line max-len */
          output.standard(`${invalidSignatures.length} packages have ${invalidClr} registry signatures:`)
        }
        output.standard('')
        invalidSignatures.map(i =>
          output.standard(`${this.npm.chalk.red(`${i.name}@${i.version}`)} (${i.registry})`)
        )
        output.standard('')
      }

      const invalidAttestations = this.invalid.filter(i => i.code === 'EATTESTATIONVERIFY')
      if (invalidAttestations.length) {
        if (invalidAttestations.length === 1) {
          output.standard(`1 package has an ${invalidClr} attestation:`)
        } else {
          /* eslint-disable-next-line max-len */
          output.standard(`${invalidAttestations.length} packages have ${invalidClr} attestations:`)
        }
        output.standard('')
        invalidAttestations.map(i =>
          output.standard(`${this.npm.chalk.red(`${i.name}@${i.version}`)} (${i.registry})`)
        )
        output.standard('')
      }

      if (invalid.length === 1) {
        /* eslint-disable-next-line max-len */
        output.standard(`Someone might have tampered with this package since it was published on the registry!`)
      } else {
        /* eslint-disable-next-line max-len */
        output.standard(`Someone might have tampered with these packages since they were published on the registry!`)
      }
      output.standard('')
    }
  }

  getEdgesOut (nodes, filterSet) {
    const edges = new Set()
    const registries = new Set()
    for (const node of nodes) {
      for (const edge of node.edgesOut.values()) {
        const filteredOut =
          edge.from
            && filterSet
            && filterSet.size > 0
            && !filterSet.has(edge.from.target)

        if (!filteredOut) {
          const spec = this.getEdgeSpec(edge)
          if (spec) {
            // Prefetch and cache public keys from used registries
            registries.add(this.getSpecRegistry(spec))
          }
          edges.add(edge)
        }
      }
    }
    return { edges, registries }
  }

  async setKeys ({ registry, tuf }) {
    const { host, pathname } = new URL(registry)
    // Strip any trailing slashes from pathname
    const regKey = `${host}${pathname.replace(/\/$/, '')}/keys.json`
    let keys = await tuf.getTarget(regKey)
      .then((target) => JSON.parse(target))
      .then(({ keys: ks }) => ks.map((key) => ({
        ...key,
        keyid: key.keyId,
        pemkey: `-----BEGIN PUBLIC KEY-----\n${key.publicKey.rawBytes}\n-----END PUBLIC KEY-----`,
        expires: key.publicKey.validFor.end || null,
      }))).catch(err => {
        if (err.code === 'TUF_FIND_TARGET_ERROR') {
          return null
        } else {
          throw err
        }
      })

    // If keys not found in Sigstore TUF repo, fallback to registry keys API
    if (!keys) {
      keys = await fetch.json('/-/npm/v1/keys', {
        ...this.npm.flatOptions,
        registry,
      }).then(({ keys: ks }) => ks.map((key) => ({
        ...key,
        pemkey: `-----BEGIN PUBLIC KEY-----\n${key.key}\n-----END PUBLIC KEY-----`,
      }))).catch(err => {
        if (err.code === 'E404' || err.code === 'E400') {
          return null
        } else {
          throw err
        }
      })
    }

    if (keys) {
      this.keys.set(registry, keys)
    }
  }

  getEdgeType (edge) {
    return edge.optional ? 'optionalDependencies'
      : edge.peer ? 'peerDependencies'
      : edge.dev ? 'devDependencies'
      : 'dependencies'
  }

  getEdgeSpec (edge) {
    let name = edge.name
    try {
      name = npa(edge.spec).subSpec.name
    } catch {
      // leave it as edge.name
    }
    try {
      return npa(`${name}@${edge.spec}`)
    } catch {
      // Skip packages with invalid spec
    }
  }

  buildRegistryConfig (registry) {
    const keys = this.keys.get(registry) || []
    const parsedRegistry = new URL(registry)
    const regKey = `//${parsedRegistry.host}${parsedRegistry.pathname}`
    return {
      [`${regKey}:_keys`]: keys,
    }
  }

  getSpecRegistry (spec) {
    return fetch.pickRegistry(spec, this.npm.flatOptions)
  }

  getValidPackageInfo (edge) {
    const type = this.getEdgeType(edge)
    // Skip potentially optional packages that are not on disk, as these could
    // be omitted during install
    if (edge.error === 'MISSING' && type !== 'dependencies') {
      return
    }

    const spec = this.getEdgeSpec(edge)
    // Skip invalid version requirements
    if (!spec) {
      return
    }
    const node = edge.to || edge
    const { version } = node.package || {}

    if (node.isWorkspace || // Skip local workspaces packages
        !version || // Skip packages that don't have a installed version, e.g. optonal dependencies
        !spec.registry) { // Skip if not from registry, e.g. git package
      return
    }

    for (const omitType of this.npm.config.get('omit')) {
      if (node[omitType]) {
        return
      }
    }

    return {
      name: spec.name,
      version,
      type,
      location: node.location,
      registry: this.getSpecRegistry(spec),
    }
  }

  async verifySignatures (name, version, registry) {
    const {
      _integrity: integrity,
      _signatures,
      _attestations,
      _resolved: resolved,
    } = await pacote.manifest(`${name}@${version}`, {
      verifySignatures: true,
      verifyAttestations: true,
      ...this.buildRegistryConfig(registry),
      ...this.npm.flatOptions,
    })
    const signatures = _signatures || []
    const result = {
      integrity,
      signatures,
      attestations: _attestations,
      resolved,
    }
    return result
  }

  async getVerifiedInfo (edge) {
    const info = this.getValidPackageInfo(edge)
    if (!info) {
      return
    }
    const { name, version, location, registry, type } = info
    if (this.checkedPackages.has(location)) {
      // we already did or are doing this one
      return
    }
    this.checkedPackages.add(location)

    // We only "audit" or verify the signature, or the presence of it, on
    // packages whose registry returns signing keys
    const keys = this.keys.get(registry) || []
    if (keys.length) {
      this.auditedWithKeysCount += 1
    }

    try {
      const { integrity, signatures, attestations, resolved } = await this.verifySignatures(
        name, version, registry
      )

      // Currently we only care about missing signatures on registries that provide a public key
      // We could make this configurable in the future with a strict/paranoid mode
      if (signatures.length) {
        this.verifiedSignatureCount += 1
      } else if (keys.length) {
        this.missing.push({
          integrity,
          location,
          name,
          registry,
          resolved,
          version,
        })
      }

      // Track verified attestations separately to registry signatures, as all
      // packages on registries with signing keys are expected to have registry
      // signatures, but not all packages have provenance and publish attestations.
      if (attestations) {
        this.verifiedAttestationCount += 1
      }
    } catch (e) {
      if (e.code === 'EINTEGRITYSIGNATURE' || e.code === 'EATTESTATIONVERIFY') {
        this.invalid.push({
          code: e.code,
          message: e.message,
          integrity: e.integrity,
          keyid: e.keyid,
          location,
          name,
          registry,
          resolved: e.resolved,
          signature: e.signature,
          predicateType: e.predicateType,
          type,
          version,
        })
      } else {
        throw e
      }
    }
  }
}

module.exports = VerifySignatures
