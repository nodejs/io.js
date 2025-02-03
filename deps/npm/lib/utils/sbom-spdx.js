
const crypto = require('node:crypto')
const normalizeData = require('normalize-package-data')
const npa = require('npm-package-arg')
const ssri = require('ssri')

const SPDX_SCHEMA_VERSION = 'SPDX-2.3'
const SPDX_DATA_LICENSE = 'CC0-1.0'
const SPDX_IDENTIFER = 'SPDXRef-DOCUMENT'

const NO_ASSERTION = 'NOASSERTION'

const REL_DESCRIBES = 'DESCRIBES'
const REL_PREREQ = 'PREREQUISITE_FOR'
const REL_OPTIONAL = 'OPTIONAL_DEPENDENCY_OF'
const REL_DEV = 'DEV_DEPENDENCY_OF'
const REL_DEP = 'DEPENDENCY_OF'

const REF_CAT_PACKAGE_MANAGER = 'PACKAGE-MANAGER'
const REF_TYPE_PURL = 'purl'

const spdxOutput = ({ npm, nodes, packageType }) => {
  const rootNode = nodes.find(node => node.isRoot)
  const childNodes = nodes.filter(node => !node.isRoot && !node.isLink)
  const rootID = rootNode.pkgid
  const uuid = crypto.randomUUID()
  const ns = `http://spdx.org/spdxdocs/${npa(rootID).escapedName}-${rootNode.version}-${uuid}`

  // Create list of child nodes w/ unique IDs
  const childNodeMap = new Map()
  for (const item of childNodes) {
    const id = toSpdxID(item)
    if (!childNodeMap.has(id)) {
      childNodeMap.set(id, item)
    }
  }
  const uniqueChildNodes = Array.from(childNodeMap.values())

  const relationships = []
  const seen = new Set()
  for (let node of nodes) {
    if (node.isLink) {
      node = node.target
    }

    if (seen.has(node)) {
      continue
    }
    seen.add(node)

    const rels = [...node.edgesOut.values()]
      // Filter out edges that are linking to nodes not in the list
      .filter(edge => nodes.find(n => n === edge.to))
      .map(edge => toSpdxRelationship(node, edge))
      .filter(rel => rel)

    relationships.push(...rels)
  }

  const extraRelationships = nodes.filter(node => node.extraneous)
    .map(node => toSpdxRelationship(rootNode, { to: node, type: 'optional' }))

  relationships.push(...extraRelationships)

  const bom = {
    spdxVersion: SPDX_SCHEMA_VERSION,
    dataLicense: SPDX_DATA_LICENSE,
    SPDXID: SPDX_IDENTIFER,
    name: rootID,
    documentNamespace: ns,
    creationInfo: {
      created: new Date().toISOString(),
      creators: [
        `Tool: npm/cli-${npm.version}`,
      ],
    },
    documentDescribes: [toSpdxID(rootNode)],
    packages: [toSpdxItem(rootNode, { packageType }), ...uniqueChildNodes.map(toSpdxItem)],
    relationships: [
      {
        spdxElementId: SPDX_IDENTIFER,
        relatedSpdxElement: toSpdxID(rootNode),
        relationshipType: REL_DESCRIBES,
      },
      ...relationships,
    ],
  }

  return bom
}

const toSpdxItem = (node, { packageType }) => {
  normalizeData(node.package)

  // Calculate purl from package spec
  let spec = npa(node.pkgid)
  spec = (spec.type === 'alias') ? spec.subSpec : spec
  const purl = npa.toPurl(spec) + (isGitNode(node) ? `?vcs_url=${node.resolved}` : '')

  /* For workspace nodes, use the location from their linkNode */
  let location = node.location
  if (node.isWorkspace && node.linksIn.size > 0) {
    location = node.linksIn.values().next().value.location
  }

  let license = node.package?.license
  if (license) {
    if (typeof license === 'object') {
      license = license.type
    }
  }

  const pkg = {
    name: node.packageName,
    SPDXID: toSpdxID(node),
    versionInfo: node.version,
    packageFileName: location,
    description: node.package?.description || undefined,
    primaryPackagePurpose: packageType ? packageType.toUpperCase() : undefined,
    downloadLocation: (node.isLink ? undefined : node.resolved) || NO_ASSERTION,
    filesAnalyzed: false,
    homepage: node.package?.homepage || NO_ASSERTION,
    licenseDeclared: license || NO_ASSERTION,
    externalRefs: [
      {
        referenceCategory: REF_CAT_PACKAGE_MANAGER,
        referenceType: REF_TYPE_PURL,
        referenceLocator: purl,
      },
    ],
  }

  if (node.integrity) {
    const integrity = ssri.parse(node.integrity, { single: true })
    pkg.checksums = [{
      algorithm: integrity.algorithm.toUpperCase(),
      checksumValue: integrity.hexDigest(),
    }]
  }
  return pkg
}

const toSpdxRelationship = (node, edge) => {
  let type
  switch (edge.type) {
    case 'peer':
      type = REL_PREREQ
      break
    case 'optional':
      type = REL_OPTIONAL
      break
    case 'dev':
      type = REL_DEV
      break
    default:
      type = REL_DEP
  }

  return {
    spdxElementId: toSpdxID(edge.to),
    relatedSpdxElement: toSpdxID(node),
    relationshipType: type,
  }
}

const toSpdxID = (node) => {
  let name = node.packageName

  // Strip leading @ for scoped packages
  name = name.replace(/^@/, '')

  // Replace slashes with dots
  name = name.replace(/\//g, '.')

  return `SPDXRef-Package-${name}-${node.version}`
}

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

module.exports = { spdxOutput }
