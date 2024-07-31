const { fixer } = require('normalize-package-data')
const npmFetch = require('npm-registry-fetch')
const npa = require('npm-package-arg')
const { log } = require('proc-log')
const semver = require('semver')
const { URL } = require('node:url')
const ssri = require('ssri')
const ciInfo = require('ci-info')

const { generateProvenance, verifyProvenance } = require('./provenance')

const TLOG_BASE_URL = 'https://search.sigstore.dev/'

const publish = async (manifest, tarballData, opts) => {
  if (manifest.private) {
    throw Object.assign(
      new Error(`This package has been marked as private
Remove the 'private' field from the package.json to publish it.`),
      { code: 'EPRIVATE' }
    )
  }

  // spec is used to pick the appropriate registry/auth combo
  const spec = npa.resolve(manifest.name, manifest.version)
  opts = {
    access: 'public',
    algorithms: ['sha512'],
    defaultTag: 'latest',
    ...opts,
    spec,
  }

  const reg = npmFetch.pickRegistry(spec, opts)
  const pubManifest = patchManifest(manifest, opts)

  // registry-frontdoor cares about the access level,
  // which is only configurable for scoped packages
  if (!spec.scope && opts.access === 'restricted') {
    throw Object.assign(
      new Error("Can't restrict access to unscoped packages."),
      { code: 'EUNSCOPED' }
    )
  }

  const { metadata, transparencyLogUrl } = await buildMetadata(
    reg,
    pubManifest,
    tarballData,
    spec,
    opts
  )

  const res = await npmFetch(spec.escapedName, {
    ...opts,
    method: 'PUT',
    body: metadata,
    ignoreBody: true,
  })
  if (transparencyLogUrl) {
    res.transparencyLogUrl = transparencyLogUrl
  }
  return res
}

const patchManifest = (_manifest, opts) => {
  const { npmVersion } = opts
  // we only update top-level fields, so a shallow clone is fine
  const manifest = { ..._manifest }

  manifest._nodeVersion = process.versions.node
  if (npmVersion) {
    manifest._npmVersion = npmVersion
  }

  fixer.fixNameField(manifest, { strict: true, allowLegacyCase: true })
  const version = semver.clean(manifest.version)
  if (!version) {
    throw Object.assign(
      new Error('invalid semver: ' + manifest.version),
      { code: 'EBADSEMVER' }
    )
  }
  manifest.version = version
  return manifest
}

const buildMetadata = async (registry, manifest, tarballData, spec, opts) => {
  const { access, defaultTag, algorithms, provenance, provenanceFile } = opts
  const root = {
    _id: manifest.name,
    name: manifest.name,
    description: manifest.description,
    'dist-tags': {},
    versions: {},
    access,
  }

  root.versions[manifest.version] = manifest
  const tag = manifest.tag || defaultTag
  root['dist-tags'][tag] = manifest.version

  const tarballName = `${manifest.name}-${manifest.version}.tgz`
  const provenanceBundleName = `${manifest.name}-${manifest.version}.sigstore`
  const tarballURI = `${manifest.name}/-/${tarballName}`
  const integrity = ssri.fromData(tarballData, {
    algorithms: [...new Set(['sha1'].concat(algorithms))],
  })

  manifest._id = `${manifest.name}@${manifest.version}`
  manifest.dist = { ...manifest.dist }
  // Don't bother having sha1 in the actual integrity field
  manifest.dist.integrity = integrity.sha512[0].toString()
  // Legacy shasum support
  manifest.dist.shasum = integrity.sha1[0].hexDigest()

  // NB: the CLI always fetches via HTTPS if the registry is HTTPS,
  // regardless of what's here.  This makes it so that installing
  // from an HTTP-only mirror doesn't cause problems, though.
  manifest.dist.tarball = new URL(tarballURI, registry).href
    .replace(/^https:\/\//, 'http://')

  root._attachments = {}
  root._attachments[tarballName] = {
    content_type: 'application/octet-stream',
    data: tarballData.toString('base64'),
    length: tarballData.length,
  }

  // Handle case where --provenance flag was set to true
  let transparencyLogUrl
  if (provenance === true || provenanceFile) {
    let provenanceBundle
    const subject = {
      name: npa.toPurl(spec),
      digest: { sha512: integrity.sha512[0].hexDigest() },
    }

    if (provenance === true) {
      await ensureProvenanceGeneration(registry, spec, opts)
      provenanceBundle = await generateProvenance([subject], opts)

      /* eslint-disable-next-line max-len */
      log.notice('publish', `Signed provenance statement with source and build information from ${ciInfo.name}`)

      const tlogEntry = provenanceBundle?.verificationMaterial?.tlogEntries[0]
      /* istanbul ignore else */
      if (tlogEntry) {
        transparencyLogUrl = `${TLOG_BASE_URL}?logIndex=${tlogEntry.logIndex}`
        log.notice(
          'publish',
          `Provenance statement published to transparency log: ${transparencyLogUrl}`
        )
      }
    } else {
      provenanceBundle = await verifyProvenance(subject, provenanceFile)
    }

    const serializedBundle = JSON.stringify(provenanceBundle)
    root._attachments[provenanceBundleName] = {
      content_type: provenanceBundle.mediaType,
      data: serializedBundle,
      length: serializedBundle.length,
    }
  }

  return {
    metadata: root,
    transparencyLogUrl,
  }
}

// Check that all the prereqs are met for provenance generation
const ensureProvenanceGeneration = async (registry, spec, opts) => {
  if (ciInfo.GITHUB_ACTIONS) {
    // Ensure that the GHA OIDC token is available
    if (!process.env.ACTIONS_ID_TOKEN_REQUEST_URL) {
      throw Object.assign(
        /* eslint-disable-next-line max-len */
        new Error('Provenance generation in GitHub Actions requires "write" access to the "id-token" permission'),
        { code: 'EUSAGE' }
      )
    }
  } else if (ciInfo.GITLAB) {
    // Ensure that the Sigstore OIDC token is available
    if (!process.env.SIGSTORE_ID_TOKEN) {
      throw Object.assign(
        /* eslint-disable-next-line max-len */
        new Error('Provenance generation in GitLab CI requires "SIGSTORE_ID_TOKEN" with "sigstore" audience to be present in "id_tokens". For more info see:\nhttps://docs.gitlab.com/ee/ci/secrets/id_token_authentication.html'),
        { code: 'EUSAGE' }
      )
    }
  } else {
    throw Object.assign(
      new Error('Automatic provenance generation not supported for provider: ' + ciInfo.name),
      { code: 'EUSAGE' }
    )
  }

  // Some registries (e.g. GH packages) require auth to check visibility,
  // and always return 404 when no auth is supplied. In this case we assume
  // the package is always private and require `--access public` to publish
  // with provenance.
  let visibility = { public: false }
  if (opts.access !== 'public') {
    try {
      const res = await npmFetch
        .json(`${registry}/-/package/${spec.escapedName}/visibility`, opts)
      visibility = res
    } catch (err) {
      if (err.code !== 'E404') {
        throw err
      }
    }
  }

  if (!visibility.public && opts.provenance === true && opts.access !== 'public') {
    throw Object.assign(
      /* eslint-disable-next-line max-len */
      new Error("Can't generate provenance for new or private package, you must set `access` to public."),
      { code: 'EUSAGE' }
    )
  }
}

module.exports = publish
