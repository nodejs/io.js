const crypto = require('node:crypto')
const normalizeData = require('normalize-package-data')
const parseLicense = require('spdx-expression-parse')
const npa = require('npm-package-arg')
const ssri = require('ssri')

const CYCLONEDX_SCHEMA = 'http://cyclonedx.org/schema/bom-1.5.schema.json'
const CYCLONEDX_FORMAT = 'CycloneDX'
const CYCLONEDX_SCHEMA_VERSION = '1.5'

const PROP_PATH = 'cdx:npm:package:path'
const PROP_BUNDLED = 'cdx:npm:package:bundled'
const PROP_DEVELOPMENT = 'cdx:npm:package:development'
const PROP_EXTRANEOUS = 'cdx:npm:package:extraneous'
const PROP_PRIVATE = 'cdx:npm:package:private'

const REF_VCS = 'vcs'
const REF_WEBSITE = 'website'
const REF_ISSUE_TRACKER = 'issue-tracker'
const REF_DISTRIBUTION = 'distribution'

const ALGO_MAP = {
  sha1: 'SHA-1',
  sha256: 'SHA-256',
  sha384: 'SHA-384',
  sha512: 'SHA-512',
}

const cyclonedxOutput = ({ npm, nodes, packageType, packageLockOnly }) => {
  const rootNode = nodes.find(node => node.isRoot)
  const childNodes = nodes.filter(node => !node.isRoot && !node.isLink)
  const uuid = crypto.randomUUID()

  const deps = []
  const seen = new Set()
  for (let node of nodes) {
    if (node.isLink) {
      node = node.target
    }

    if (seen.has(node)) {
      continue
    }
    seen.add(node)
    deps.push(toCyclonedxDependency(node, nodes))
  }

  const bom = {
    $schema: CYCLONEDX_SCHEMA,
    bomFormat: CYCLONEDX_FORMAT,
    specVersion: CYCLONEDX_SCHEMA_VERSION,
    serialNumber: `urn:uuid:${uuid}`,
    version: 1,
    metadata: {
      timestamp: new Date().toISOString(),
      lifecycles: [
        { phase: packageLockOnly ? 'pre-build' : 'build' },
      ],
      tools: [
        {
          vendor: 'npm',
          name: 'cli',
          version: npm.version,
        },
      ],
      component: toCyclonedxItem(rootNode, { packageType }),
    },
    components: childNodes.map(toCyclonedxItem),
    dependencies: deps,
  }

  return bom
}

const toCyclonedxItem = (node, { packageType }) => {
  packageType = packageType || 'library'

  // Calculate purl from package spec
  let spec = npa(node.pkgid)
  spec = (spec.type === 'alias') ? spec.subSpec : spec
  const purl = npa.toPurl(spec) + (isGitNode(node) ? `?vcs_url=${node.resolved}` : '')

  if (node.package) {
    normalizeData(node.package)
  }

  let parsedLicense
  try {
    let license = node.package?.license
    if (license) {
      if (typeof license === 'object') {
        license = license.type
      }
    }

    parsedLicense = parseLicense(license)
  } catch {
    parsedLicense = null
  }

  const component = {
    'bom-ref': toCyclonedxID(node),
    type: packageType,
    name: node.name,
    version: node.version,
    scope: (node.optional || node.devOptional) ? 'optional' : 'required',
    author: (typeof node.package?.author === 'object')
      ? node.package.author.name
      : (node.package?.author || undefined),
    description: node.package?.description || undefined,
    purl: purl,
    properties: [{
      name: PROP_PATH,
      value: node.location,
    }],
    externalReferences: [],
  }

  if (node.integrity) {
    const integrity = ssri.parse(node.integrity, { single: true })
    component.hashes = [{
      alg: ALGO_MAP[integrity.algorithm] || /* istanbul ignore next */ 'SHA-512',
      content: integrity.hexDigest(),
    }]
  }

  if (node.dev === true) {
    component.properties.push(prop(PROP_DEVELOPMENT))
  }

  if (node.package?.private === true) {
    component.properties.push(prop(PROP_PRIVATE))
  }

  if (node.extraneous === true) {
    component.properties.push(prop(PROP_EXTRANEOUS))
  }

  if (node.inBundle === true) {
    component.properties.push(prop(PROP_BUNDLED))
  }

  if (!node.isLink && node.resolved) {
    component.externalReferences.push(extRef(REF_DISTRIBUTION, node.resolved))
  }

  if (node.package?.repository?.url) {
    component.externalReferences.push(extRef(REF_VCS, node.package.repository.url))
  }

  if (node.package?.homepage) {
    component.externalReferences.push(extRef(REF_WEBSITE, node.package.homepage))
  }

  if (node.package?.bugs?.url) {
    component.externalReferences.push(extRef(REF_ISSUE_TRACKER, node.package.bugs.url))
  }

  // If license is a single SPDX license, use the license field
  if (parsedLicense?.license) {
    component.licenses = [{ license: { id: parsedLicense.license } }]
    // If license is a conjunction, use the expression field
  } else if (parsedLicense?.conjunction) {
    component.licenses = [{ expression: node.package.license }]
  }

  return component
}

const toCyclonedxDependency = (node, nodes) => {
  return {
    ref: toCyclonedxID(node),
    dependsOn: [...node.edgesOut.values()]
      // Filter out edges that are linking to nodes not in the list
      .filter(edge => nodes.find(n => n === edge.to))
      .map(edge => toCyclonedxID(edge.to))
      .filter(id => id),
  }
}

const toCyclonedxID = (node) => `${node.packageName}@${node.version}`

const prop = (name) => ({ name, value: 'true' })

const extRef = (type, url) => ({ type, url })

const isGitNode = (node) => {
  if (!node.resolved) {
    return
  }

  try {
    const { type } = npa(node.resolved)
    return type === 'git' || type === 'hosted'
  } catch {
    /* istanbul ignore next */
    return false
  }
}

module.exports = { cyclonedxOutput }
