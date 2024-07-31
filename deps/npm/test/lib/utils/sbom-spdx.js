const t = require('tap')
const Ajv = require('ajv')
const { spdxOutput } = require('../../../lib/utils/sbom-spdx.js')

t.cleanSnapshot = s => {
  let sbom
  try {
    sbom = JSON.parse(s)
  } catch (e) {
    return s
  }

  sbom.documentNamespace = 'docns'

  if (sbom.creationInfo) {
    sbom.creationInfo.created = '2020-01-01T00:00:00.000Z'
  }

  return JSON.stringify(sbom, null, 2)
}

const npm = { version: '10.0.0 ' }

const rootPkg = {
  author: 'Author',
}

const root = {
  packageName: 'root',
  version: '1.0.0',
  pkgid: 'root@1.0.0',
  isRoot: true,
  package: rootPkg,
  location: '',
  edgesOut: [],
}

const dep1 = {
  packageName: 'dep1',
  version: '0.0.1',
  pkgid: 'dep1@0.0.1',
  package: {},
  location: 'node_modules/dep1',
  edgesOut: [],
}

const dep2 = {
  packageName: 'dep2',
  version: '0.0.2',
  pkgid: 'dep2@0.0.2',
  package: {},
  location: 'node_modules/dep2',
  edgesOut: [],
}

const dep3 = {
  packageName: 'dep3',
  version: '0.0.3',
  pkgid: 'dep3@0.0.3',
  package: {},
  location: 'node_modules/dep3',
  edgesOut: [],
}

const dep5 = {
  packageName: 'dep5',
  version: '0.0.5',
  pkgid: 'dep5@0.0.5',
  package: {},
  location: 'node_modules/dep5',
  edgesOut: [],
}

const dep4 = {
  packageName: 'dep4',
  version: '0.0.4',
  pkgid: 'npm@npm:dep4@0.0.4',
  package: {},
  location: 'dep4',
  isWorkspace: true,
  edgesOut: [{ to: dep5 }],
}

const dep4Link = {
  packageName: 'dep4',
  version: '0.0.4',
  pkgid: 'dep4@0.0.4',
  package: {},
  location: 'node_modules/dep4',
  isLink: true,
  target: dep4,
}

dep4.linksIn = new Set([dep4Link])

const dep6 = {
  packageName: 'dep6',
  version: '0.0.6',
  pkgid: 'dep6@0.0.6',
  extraneous: true,
  package: {},
  location: 'node_modules/dep6',
  edgesOut: [],
}

t.test('single node - application package type', t => {
  const res = spdxOutput({ npm, nodes: [root], packageType: 'application' })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with single license', t => {
  const pkg = { ...rootPkg, license: 'ISC' }
  const node = { ...root, package: pkg }
  const res = spdxOutput({ npm, nodes: [node] })
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
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with license expression', t => {
  const pkg = { ...rootPkg, license: '(MIT OR Apache-2.0)' }
  const node = { ...root, package: pkg }
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with description', t => {
  const pkg = { ...rootPkg, description: 'Package description' }
  const node = { ...root, package: pkg }
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with distribution url', t => {
  const node = { ...root, resolved: 'https://registry.npmjs.org/root/-/root-1.0.0.tgz' }
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with homepage', t => {
  const pkg = { ...rootPkg, homepage: 'https://foo.bar/README.md' }
  const node = { ...root, package: pkg }
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - with integrity', t => {
  /* eslint-disable-next-line max-len */
  const node = { ...root, integrity: 'sha512-1RkbFGUKex4lvsB9yhIfWltJM5cZKUftB2eNajaDv3dCMEp49iBG0K14uH8NnX9IPux2+mK7JGEOB0jn48/J6w==' }
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - from git url', t => {
  const node = { ...root, type: 'git', resolved: 'https://github.com/foo/bar#1234' }
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('single node - linked', t => {
  const node = { ...root, isLink: true, target: { edgesOut: [] } }
  const res = spdxOutput({ npm, nodes: [node] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

t.test('node - with deps', t => {
  const node = { ...root,
    edgesOut: [
      { to: dep1, type: 'peer' },
      { to: dep2, type: 'optional' },
      { to: dep3, type: 'dev' },
      { to: dep4 },
      { to: undefined },
      { to: { packageName: 'foo' } },
    ] }
  const res = spdxOutput({ npm, nodes: [node, dep1, dep2, dep3, dep4Link, dep4, dep5, dep6] })
  t.matchSnapshot(JSON.stringify(res))
  t.end()
})

// Check that all of the generated test snapshots validate against the SPDX schema
t.test('schema validation', t => {
  const ajv = new Ajv()

  // Compile schema
  const spdxSchema = require('../../schemas/spdx/spdx-2.3.schema.json')
  const validate = ajv.compile(spdxSchema)

  // Load snapshots for all tests in this file
  const sboms = require('../../../tap-snapshots/test/lib/utils/sbom-spdx.js.test.cjs')

  // Check that all snapshots validate against the SPDX schema
  Object.entries(sboms).forEach(([name, sbom]) => {
    t.ok(validate(JSON.parse(sbom)), { snapshot: name, error: validate.errors?.[0] })
  })
  t.end()
})
