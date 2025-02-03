const t = require('tap')
const Ajv = require('ajv')
const applyFormats = require('ajv-formats')
const applyDraftFormats = require('ajv-formats-draft2019')
const { cyclonedxOutput } = require('../../../lib/utils/sbom-cyclonedx.js')

const FAKE_UUID = 'urn:uuid:00000000-0000-0000-0000-000000000000'

t.cleanSnapshot = s => {
  let sbom
  try {
    sbom = JSON.parse(s)
  } catch (e) {
    return s
  }

  sbom.serialNumber = FAKE_UUID
  if (sbom.metadata) {
    sbom.metadata.timestamp = '2020-01-01T00:00:00.000Z'
  }

  return JSON.stringify(sbom, null, 2)
}

const npm = { version: '10.0.0 ' }

const rootPkg = {
  author: 'Author',
}

const root = {
  name: 'root',
  packageName: 'root',
  version: '1.0.0',
  pkgid: 'root@1.0.0',
  isRoot: true,
  package: rootPkg,
  location: '',
  edgesOut: [],
}

const dep1 = {
  name: 'dep1',
  packageName: 'dep1',
  version: '0.0.1',
  pkgid: 'dep1@0.0.1',
  package: {},
  location: 'node_modules/dep1',
  edgesOut: [],
}

const dep2 = {
  name: 'dep2',
  packageName: 'dep2',
  version: '0.0.2',
  pkgid: 'npm@npm:dep2@0.0.2',
  package: {},
  location: 'node_modules/dep2',
  edgesOut: [{ to: dep1 }],
}

const dep2Link = {
  name: 'dep2',
  packageName: 'dep2',
  version: '0.0.2',
  pkgid: 'dep2@0.0.2',
  package: {},
  location: 'node_modules/dep2',
  edgesOut: [],
  isLink: true,
  target: dep2,
}

t.test('single node - application package type', t => {
  const res = cyclonedxOutput({ npm, nodes: [root], packageType: 'application' })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - package lock only', t => {
  const res = cyclonedxOutput({ npm, nodes: [root], packageLockOnly: true })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - optional ', t => {
  const node = { ...root, optional: true }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with description', t => {
  const pkg = { ...rootPkg, description: 'Package description' }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with author object', t => {
  const pkg = { ...rootPkg, author: { name: 'Arthur' } }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with integrity', t => {
  const node = { ...root, integrity: 'sha512-1RkbFGUKex4lvsB9yhIfWltJM5cZKUftB2eNajaDv3dCMEp49iBG0K14uH8NnX9IPux2+mK7JGEOB0jn48/J6w==' }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - development', t => {
  const node = { ...root, dev: true }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - extraneous', t => {
  const node = { ...root, extraneous: true }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - bundled', t => {
  const node = { ...root, inBundle: true }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - private', t => {
  const pkg = { ...rootPkg, private: true }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with repository url', t => {
  const pkg = { ...rootPkg, repository: { url: 'https://foo.bar' } }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with homepage', t => {
  const pkg = { ...rootPkg, homepage: 'https://foo.bar/README.md' }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with issue tracker', t => {
  const pkg = { ...rootPkg, bugs: { url: 'https://foo.bar/issues' } }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with distribution url', t => {
  const node = { ...root, resolved: 'https://registry.npmjs.org/root/-/root-1.0.0.tgz' }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with single license', t => {
  const pkg = { ...rootPkg, license: 'ISC' }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with license expression', t => {
  const pkg = { ...rootPkg, license: '(MIT OR Apache-2.0)' }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with license object', t => {
  const pkg = {
    ...rootPkg,
    license: {
      type: 'MIT',
      url: 'http://github.com/kriskowal/q/raw/master/LICENSE',
    },
  }
  const node = { ...root, package: pkg }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - from git url', t => {
  const node = { ...root, type: 'git', resolved: 'https://github.com/foo/bar#1234' }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - no package info', t => {
  const node = { ...root, package: undefined }
  const res = cyclonedxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('node - with deps', t => {
  const node = {
    ...root,
    edgesOut: [
      { to: dep1 },
      { to: dep2 },
      { to: undefined },
      { to: { pkgid: 'foo' } },
    ],
  }
  const res = cyclonedxOutput({ npm, nodes: [node, dep1, dep2, dep2Link] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('node - with duplicate deps', t => {
  const node = {
    ...root,
    edgesOut: [
      { to: dep1 },
    ],
  }
  const res = cyclonedxOutput({ npm, nodes: [node, dep1, dep1] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

// Check that all of the generated test snapshots validate against the CycloneDX schema
t.test('schema validation', t => {
  // Load schemas
  const cdxSchema = require('../../schemas/cyclonedx/bom-1.5.schema.json')
  const spdxLicenseSchema = require('../../schemas/cyclonedx/spdx.schema.json')
  const jsfSchema = require('../../schemas/cyclonedx/jsf-0.82.schema.json')

  const ajv = new Ajv({
    strict: false,
    schemas: [spdxLicenseSchema, jsfSchema, cdxSchema],
  })
  applyFormats(ajv)
  applyDraftFormats(ajv)

  // Retrieve compiled schema
  const validate = ajv.getSchema('http://cyclonedx.org/schema/bom-1.5.schema.json')

  // Load snapshots for all tests in this file
  const sboms = require('../../../tap-snapshots/test/lib/utils/sbom-cyclonedx.js.test.cjs')

  // Check that all snapshots validate against the CycloneDX schema
  Object.entries(sboms).forEach(([name, sbom]) => {
    t.ok(validate(JSON.parse(sbom)), { snapshot: name, error: validate.errors?.[0] })
  })
  t.end()
})
