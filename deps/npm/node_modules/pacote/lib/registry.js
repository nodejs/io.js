const crypto = require('node:crypto')
const PackageJson = require('@npmcli/package-json')
const pickManifest = require('npm-pick-manifest')
const ssri = require('ssri')
const npa = require('npm-package-arg')
const sigstore = require('sigstore')
const fetch = require('npm-registry-fetch')
const Fetcher = require('./fetcher.js')
const RemoteFetcher = require('./remote.js')
const pacoteVersion = require('../package.json').version
const removeTrailingSlashes = require('./util/trailing-slashes.js')
const _ = require('./util/protected.js')

// Corgis are cute. 🐕🐶
const corgiDoc = 'application/vnd.npm.install-v1+json; q=1.0, application/json; q=0.8, */*'
const fullDoc = 'application/json'

// Some really old packages have no time field in their packument so we need a
// cutoff date.
const MISSING_TIME_CUTOFF = '2015-01-01T00:00:00.000Z'

class RegistryFetcher extends Fetcher {
  #cacheKey
  constructor (spec, opts) {
    super(spec, opts)

    // you usually don't want to fetch the same packument multiple times in
    // the span of a given script or command, no matter how many pacote calls
    // are made, so this lets us avoid doing that.  It's only relevant for
    // registry fetchers, because other types simulate their packument from
    // the manifest, which they memoize on this.package, so it's very cheap
    // already.
    this.packumentCache = this.opts.packumentCache || null

    this.registry = fetch.pickRegistry(spec, opts)
    this.packumentUrl = `${removeTrailingSlashes(this.registry)}/${this.spec.escapedName}`
    this.#cacheKey = `${this.fullMetadata ? 'full' : 'corgi'}:${this.packumentUrl}`

    const parsed = new URL(this.registry)
    const regKey = `//${parsed.host}${parsed.pathname}`
    // unlike the nerf-darted auth keys, this one does *not* allow a mismatch
    // of trailing slashes.  It must match exactly.
    if (this.opts[`${regKey}:_keys`]) {
      this.registryKeys = this.opts[`${regKey}:_keys`]
    }

    // XXX pacote <=9 has some logic to ignore opts.resolved if
    // the resolved URL doesn't go to the same registry.
    // Consider reproducing that here, to throw away this.resolved
    // in that case.
  }

  async resolve () {
    // fetching the manifest sets resolved and (if present) integrity
    await this.manifest()
    if (!this.resolved) {
      throw Object.assign(
        new Error('Invalid package manifest: no `dist.tarball` field'),
        { package: this.spec.toString() }
      )
    }
    return this.resolved
  }

  #headers () {
    return {
      // npm will override UA, but ensure that we always send *something*
      'user-agent': this.opts.userAgent ||
        `pacote/${pacoteVersion} node/${process.version}`,
      ...(this.opts.headers || {}),
      'pacote-version': pacoteVersion,
      'pacote-req-type': 'packument',
      'pacote-pkg-id': `registry:${this.spec.name}`,
      accept: this.fullMetadata ? fullDoc : corgiDoc,
    }
  }

  async packument () {
    // note this might be either an in-flight promise for a request,
    // or the actual packument, but we never want to make more than
    // one request at a time for the same thing regardless.
    if (this.packumentCache?.has(this.#cacheKey)) {
      return this.packumentCache.get(this.#cacheKey)
    }

    // npm-registry-fetch the packument
    // set the appropriate header for corgis if fullMetadata isn't set
    // return the res.json() promise
    try {
      const res = await fetch(this.packumentUrl, {
        ...this.opts,
        headers: this.#headers(),
        spec: this.spec,

        // never check integrity for packuments themselves
        integrity: null,
      })
      const packument = await res.json()
      const contentLength = res.headers.get('content-length')
      if (contentLength) {
        packument._contentLength = Number(contentLength)
      }
      this.packumentCache?.set(this.#cacheKey, packument)
      return packument
    } catch (err) {
      this.packumentCache?.delete(this.#cacheKey)
      if (err.code !== 'E404' || this.fullMetadata) {
        throw err
      }
      // possible that corgis are not supported by this registry
      this.fullMetadata = true
      return this.packument()
    }
  }

  async manifest () {
    if (this.package) {
      return this.package
    }

    // When verifying signatures, we need to fetch the full/uncompressed
    // packument to get publish time as this is not included in the
    // corgi/compressed packument.
    if (this.opts.verifySignatures) {
      this.fullMetadata = true
    }

    const packument = await this.packument()
    const steps = PackageJson.normalizeSteps.filter(s => s !== '_attributes')
    const mani = await new PackageJson().fromContent(pickManifest(packument, this.spec.fetchSpec, {
      ...this.opts,
      defaultTag: this.defaultTag,
      before: this.before,
    })).normalize({ steps }).then(p => p.content)

    /* XXX add ETARGET and E403 revalidation of cached packuments here */

    // add _time from packument if fetched with fullMetadata
    const time = packument.time?.[mani.version]
    if (time) {
      mani._time = time
    }

    // add _resolved and _integrity from dist object
    const { dist } = mani
    if (dist) {
      this.resolved = mani._resolved = dist.tarball
      mani._from = this.from
      const distIntegrity = dist.integrity ? ssri.parse(dist.integrity)
        : dist.shasum ? ssri.fromHex(dist.shasum, 'sha1', { ...this.opts })
        : null
      if (distIntegrity) {
        if (this.integrity && !this.integrity.match(distIntegrity)) {
          // only bork if they have algos in common.
          // otherwise we end up breaking if we have saved a sha512
          // previously for the tarball, but the manifest only
          // provides a sha1, which is possible for older publishes.
          // Otherwise, this is almost certainly a case of holding it
          // wrong, and will result in weird or insecure behavior
          // later on when building package tree.
          for (const algo of Object.keys(this.integrity)) {
            if (distIntegrity[algo]) {
              throw Object.assign(new Error(
                `Integrity checksum failed when using ${algo}: ` +
                `wanted ${this.integrity} but got ${distIntegrity}.`
              ), { code: 'EINTEGRITY' })
            }
          }
        }
        // made it this far, the integrity is worthwhile.  accept it.
        // the setter here will take care of merging it into what we already
        // had.
        this.integrity = distIntegrity
      }
    }
    if (this.integrity) {
      mani._integrity = String(this.integrity)
      if (dist.signatures) {
        if (this.opts.verifySignatures) {
          // validate and throw on error, then set _signatures
          const message = `${mani._id}:${mani._integrity}`
          for (const signature of dist.signatures) {
            const publicKey = this.registryKeys &&
              this.registryKeys.filter(key => (key.keyid === signature.keyid))[0]
            if (!publicKey) {
              throw Object.assign(new Error(
                  `${mani._id} has a registry signature with keyid: ${signature.keyid} ` +
                  'but no corresponding public key can be found'
              ), { code: 'EMISSINGSIGNATUREKEY' })
            }

            const publishedTime = Date.parse(mani._time || MISSING_TIME_CUTOFF)
            const validPublicKey = !publicKey.expires ||
              publishedTime < Date.parse(publicKey.expires)
            if (!validPublicKey) {
              throw Object.assign(new Error(
                  `${mani._id} has a registry signature with keyid: ${signature.keyid} ` +
                  `but the corresponding public key has expired ${publicKey.expires}`
              ), { code: 'EEXPIREDSIGNATUREKEY' })
            }
            const verifier = crypto.createVerify('SHA256')
            verifier.write(message)
            verifier.end()
            const valid = verifier.verify(
              publicKey.pemkey,
              signature.sig,
              'base64'
            )
            if (!valid) {
              throw Object.assign(new Error(
                  `${mani._id} has an invalid registry signature with ` +
                  `keyid: ${publicKey.keyid} and signature: ${signature.sig}`
              ), {
                code: 'EINTEGRITYSIGNATURE',
                keyid: publicKey.keyid,
                signature: signature.sig,
                resolved: mani._resolved,
                integrity: mani._integrity,
              })
            }
          }
          mani._signatures = dist.signatures
        } else {
          mani._signatures = dist.signatures
        }
      }

      if (dist.attestations) {
        if (this.opts.verifyAttestations) {
          // Always fetch attestations from the current registry host
          const attestationsPath = new URL(dist.attestations.url).pathname
          const attestationsUrl = removeTrailingSlashes(this.registry) + attestationsPath
          const res = await fetch(attestationsUrl, {
            ...this.opts,
            // disable integrity check for attestations json payload, we check the
            // integrity in the verification steps below
            integrity: null,
          })
          const { attestations } = await res.json()
          const bundles = attestations.map(({ predicateType, bundle }) => {
            const statement = JSON.parse(
              Buffer.from(bundle.dsseEnvelope.payload, 'base64').toString('utf8')
            )
            const keyid = bundle.dsseEnvelope.signatures[0].keyid
            const signature = bundle.dsseEnvelope.signatures[0].sig

            return {
              predicateType,
              bundle,
              statement,
              keyid,
              signature,
            }
          })

          const attestationKeyIds = bundles.map((b) => b.keyid).filter((k) => !!k)
          const attestationRegistryKeys = (this.registryKeys || [])
            .filter(key => attestationKeyIds.includes(key.keyid))
          if (!attestationRegistryKeys.length) {
            throw Object.assign(new Error(
              `${mani._id} has attestations but no corresponding public key(s) can be found`
            ), { code: 'EMISSINGSIGNATUREKEY' })
          }

          for (const { predicateType, bundle, keyid, signature, statement } of bundles) {
            const publicKey = attestationRegistryKeys.find(key => key.keyid === keyid)
            // Publish attestations have a keyid set and a valid public key must be found
            if (keyid) {
              if (!publicKey) {
                throw Object.assign(new Error(
                  `${mani._id} has attestations with keyid: ${keyid} ` +
                  'but no corresponding public key can be found'
                ), { code: 'EMISSINGSIGNATUREKEY' })
              }

              const integratedTime = new Date(
                Number(
                  bundle.verificationMaterial.tlogEntries[0].integratedTime
                ) * 1000
              )
              const validPublicKey = !publicKey.expires ||
                (integratedTime < Date.parse(publicKey.expires))
              if (!validPublicKey) {
                throw Object.assign(new Error(
                  `${mani._id} has attestations with keyid: ${keyid} ` +
                  `but the corresponding public key has expired ${publicKey.expires}`
                ), { code: 'EEXPIREDSIGNATUREKEY' })
              }
            }

            const subject = {
              name: statement.subject[0].name,
              sha512: statement.subject[0].digest.sha512,
            }

            // Only type 'version' can be turned into a PURL
            const purl = this.spec.type === 'version' ? npa.toPurl(this.spec) : this.spec
            // Verify the statement subject matches the package, version
            if (subject.name !== purl) {
              throw Object.assign(new Error(
                `${mani._id} package name and version (PURL): ${purl} ` +
                `doesn't match what was signed: ${subject.name}`
              ), { code: 'EATTESTATIONSUBJECT' })
            }

            // Verify the statement subject matches the tarball integrity
            const integrityHexDigest = ssri.parse(this.integrity).hexDigest()
            if (subject.sha512 !== integrityHexDigest) {
              throw Object.assign(new Error(
                `${mani._id} package integrity (hex digest): ` +
                `${integrityHexDigest} ` +
                `doesn't match what was signed: ${subject.sha512}`
              ), { code: 'EATTESTATIONSUBJECT' })
            }

            try {
              // Provenance attestations are signed with a signing certificate
              // (including the key) so we don't need to return a public key.
              //
              // Publish attestations are signed with a keyid so we need to
              // specify a public key from the keys endpoint: `registry-host.tld/-/npm/v1/keys`
              const options = {
                tufCachePath: this.tufCache,
                tufForceCache: true,
                keySelector: publicKey ? () => publicKey.pemkey : undefined,
              }
              await sigstore.verify(bundle, options)
            } catch (e) {
              throw Object.assign(new Error(
                `${mani._id} failed to verify attestation: ${e.message}`
              ), {
                code: 'EATTESTATIONVERIFY',
                predicateType,
                keyid,
                signature,
                resolved: mani._resolved,
                integrity: mani._integrity,
              })
            }
          }
          mani._attestations = dist.attestations
        } else {
          mani._attestations = dist.attestations
        }
      }
    }

    this.package = mani
    return this.package
  }

  [_.tarballFromResolved] () {
    // we use a RemoteFetcher to get the actual tarball stream
    return new RemoteFetcher(this.resolved, {
      ...this.opts,
      resolved: this.resolved,
      pkgid: `registry:${this.spec.name}@${this.resolved}`,
    })[_.tarballFromResolved]()
  }

  get types () {
    return [
      'tag',
      'version',
      'range',
    ]
  }
}
module.exports = RegistryFetcher
