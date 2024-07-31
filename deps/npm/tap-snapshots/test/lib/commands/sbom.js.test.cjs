/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/sbom.js TAP sbom --omit dev > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "test-npm-sbom@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-test-npm-sbom-1.0.0"
  ],
  "packages": [
    {
      "name": "test-npm-sbom",
      "SPDXID": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/test-npm-sbom@1.0.0"
        }
      ]
    },
    {
      "name": "foo",
      "SPDXID": "SPDXRef-Package-foo-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/foo@1.0.0"
        }
      ]
    },
    {
      "name": "dog",
      "SPDXID": "SPDXRef-Package-dog-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo/node_modules/dog",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dog@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-foo-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dog-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-foo-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom --omit optional > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "test-npm-sbom@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-test-npm-sbom-1.0.0"
  ],
  "packages": [
    {
      "name": "test-npm-sbom",
      "SPDXID": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/test-npm-sbom@1.0.0"
        }
      ]
    },
    {
      "name": "chai",
      "SPDXID": "SPDXRef-Package-chai-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/chai",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/chai@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-chai-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom --omit peer > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "test-npm-sbom@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-test-npm-sbom-1.0.0"
  ],
  "packages": [
    {
      "name": "test-npm-sbom",
      "SPDXID": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/test-npm-sbom@1.0.0"
        }
      ]
    },
    {
      "name": "chai",
      "SPDXID": "SPDXRef-Package-chai-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/chai",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/chai@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-chai-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom basic sbom - cyclonedx > must match snapshot 1`] = `
{
  "$schema": "http://cyclonedx.org/schema/bom-1.5.schema.json",
  "bomFormat": "CycloneDX",
  "specVersion": "1.5",
  "serialNumber": "urn:uuid:00000000-0000-0000-0000-000000000000",
  "version": 1,
  "metadata": {
    "timestamp": "2020-01-01T00:00:00.000Z",
    "lifecycles": [
      {
        "phase": "build"
      }
    ],
    "tools": [
      {
        "vendor": "npm",
        "name": "cli",
        "version": "10.0.0"
      }
    ],
    "component": {
      "bom-ref": "test-npm-sbom@1.0.0",
      "type": "application",
      "name": "prefix",
      "version": "1.0.0",
      "scope": "required",
      "purl": "pkg:npm/test-npm-sbom@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": []
    }
  },
  "components": [
    {
      "bom-ref": "chai@1.0.0",
      "type": "library",
      "name": "chai",
      "version": "1.0.0",
      "scope": "required",
      "purl": "pkg:npm/chai@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": "node_modules/chai"
        }
      ],
      "externalReferences": []
    },
    {
      "bom-ref": "foo@1.0.0",
      "type": "library",
      "name": "foo",
      "version": "1.0.0",
      "scope": "required",
      "purl": "pkg:npm/foo@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": "node_modules/foo"
        }
      ],
      "externalReferences": []
    },
    {
      "bom-ref": "dog@1.0.0",
      "type": "library",
      "name": "dog",
      "version": "1.0.0",
      "scope": "required",
      "purl": "pkg:npm/dog@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": "node_modules/foo/node_modules/dog"
        }
      ],
      "externalReferences": []
    }
  ],
  "dependencies": [
    {
      "ref": "test-npm-sbom@1.0.0",
      "dependsOn": [
        "foo@1.0.0",
        "chai@1.0.0"
      ]
    },
    {
      "ref": "chai@1.0.0",
      "dependsOn": []
    },
    {
      "ref": "foo@1.0.0",
      "dependsOn": [
        "dog@1.0.0"
      ]
    },
    {
      "ref": "dog@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom basic sbom - spdx > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "test-npm-sbom@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-test-npm-sbom-1.0.0"
  ],
  "packages": [
    {
      "name": "test-npm-sbom",
      "SPDXID": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/test-npm-sbom@1.0.0"
        }
      ]
    },
    {
      "name": "chai",
      "SPDXID": "SPDXRef-Package-chai-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/chai",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/chai@1.0.0"
        }
      ]
    },
    {
      "name": "foo",
      "SPDXID": "SPDXRef-Package-foo-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/foo@1.0.0"
        }
      ]
    },
    {
      "name": "dog",
      "SPDXID": "SPDXRef-Package-dog-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo/node_modules/dog",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dog@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-foo-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-chai-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-sbom-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dog-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-foo-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom extraneous dep > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "test-npm-ls@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-test-npm-ls-1.0.0"
  ],
  "packages": [
    {
      "name": "test-npm-ls",
      "SPDXID": "SPDXRef-Package-test-npm-ls-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/test-npm-ls@1.0.0"
        }
      ]
    },
    {
      "name": "chai",
      "SPDXID": "SPDXRef-Package-chai-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/chai",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/chai@1.0.0"
        }
      ]
    },
    {
      "name": "foo",
      "SPDXID": "SPDXRef-Package-foo-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/foo@1.0.0"
        }
      ]
    },
    {
      "name": "dog",
      "SPDXID": "SPDXRef-Package-dog-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo/node_modules/dog",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dog@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-foo-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dog-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-foo-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-chai-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "OPTIONAL_DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom loading a tree containing workspaces should filter worksapces with --workspace > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "workspaces-tree@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-workspaces-tree-1.0.0"
  ],
  "packages": [
    {
      "name": "workspaces-tree",
      "SPDXID": "SPDXRef-Package-workspaces-tree-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/workspaces-tree@1.0.0"
        }
      ]
    },
    {
      "name": "a",
      "SPDXID": "SPDXRef-Package-a-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/a",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/a@1.0.0"
        }
      ]
    },
    {
      "name": "d",
      "SPDXID": "SPDXRef-Package-d-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/d",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/d@1.0.0"
        }
      ]
    },
    {
      "name": "bar",
      "SPDXID": "SPDXRef-Package-bar-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/bar",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/bar@1.0.0"
        }
      ]
    },
    {
      "name": "baz",
      "SPDXID": "SPDXRef-Package-baz-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/baz",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/baz@1.0.0"
        }
      ]
    },
    {
      "name": "c",
      "SPDXID": "SPDXRef-Package-c-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/c",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/c@1.0.0"
        }
      ]
    },
    {
      "name": "foo",
      "SPDXID": "SPDXRef-Package-foo-1.1.1",
      "versionInfo": "1.1.1",
      "packageFileName": "node_modules/foo",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/foo@1.1.1"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-a-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-d-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-c-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-a-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-d-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-a-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-baz-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-a-1.0.0",
      "relationshipType": "DEV_DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-foo-1.1.1",
      "relatedSpdxElement": "SPDXRef-Package-d-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-bar-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-foo-1.1.1",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom loading a tree containing workspaces should filter workspaces with multiple --workspace flags > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "workspaces-tree@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-workspaces-tree-1.0.0"
  ],
  "packages": [
    {
      "name": "workspaces-tree",
      "SPDXID": "SPDXRef-Package-workspaces-tree-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/workspaces-tree@1.0.0"
        }
      ]
    },
    {
      "name": "e",
      "SPDXID": "SPDXRef-Package-e-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/e",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/e@1.0.0"
        }
      ]
    },
    {
      "name": "f",
      "SPDXID": "SPDXRef-Package-f-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/f",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/f@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-e-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-f-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom loading a tree containing workspaces should list workspaces properly with default configs > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "workspaces-tree@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-workspaces-tree-1.0.0"
  ],
  "packages": [
    {
      "name": "workspaces-tree",
      "SPDXID": "SPDXRef-Package-workspaces-tree-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/workspaces-tree@1.0.0"
        }
      ]
    },
    {
      "name": "a",
      "SPDXID": "SPDXRef-Package-a-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/a",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/a@1.0.0"
        }
      ]
    },
    {
      "name": "b",
      "SPDXID": "SPDXRef-Package-b-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/b",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/b@1.0.0"
        }
      ]
    },
    {
      "name": "d",
      "SPDXID": "SPDXRef-Package-d-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/d",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/d@1.0.0"
        }
      ]
    },
    {
      "name": "e",
      "SPDXID": "SPDXRef-Package-e-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/e",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/e@1.0.0"
        }
      ]
    },
    {
      "name": "f",
      "SPDXID": "SPDXRef-Package-f-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/f",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/f@1.0.0"
        }
      ]
    },
    {
      "name": "bar",
      "SPDXID": "SPDXRef-Package-bar-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/bar",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/bar@1.0.0"
        }
      ]
    },
    {
      "name": "baz",
      "SPDXID": "SPDXRef-Package-baz-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/baz",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/baz@1.0.0"
        }
      ]
    },
    {
      "name": "c",
      "SPDXID": "SPDXRef-Package-c-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/c",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/c@1.0.0"
        }
      ]
    },
    {
      "name": "foo",
      "SPDXID": "SPDXRef-Package-foo-1.1.1",
      "versionInfo": "1.1.1",
      "packageFileName": "node_modules/foo",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/foo@1.1.1"
        }
      ]
    },
    {
      "name": "pacote",
      "SPDXID": "SPDXRef-Package-pacote-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/pacote",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/pacote@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-a-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-b-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-d-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-e-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-f-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-pacote-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-c-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-a-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-d-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-a-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-baz-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-a-1.0.0",
      "relationshipType": "DEV_DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-foo-1.1.1",
      "relatedSpdxElement": "SPDXRef-Package-d-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-bar-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-foo-1.1.1",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom loading a tree containing workspaces should not list workspaces with --no-workspaces > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "workspaces-tree@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-workspaces-tree-1.0.0"
  ],
  "packages": [
    {
      "name": "workspaces-tree",
      "SPDXID": "SPDXRef-Package-workspaces-tree-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/workspaces-tree@1.0.0"
        }
      ]
    },
    {
      "name": "pacote",
      "SPDXID": "SPDXRef-Package-pacote-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/pacote",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/pacote@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-pacote-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-workspaces-tree-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom lock file only - missing lock file > must match snapshot 1`] = `

`

exports[`test/lib/commands/sbom.js TAP sbom lock file only > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "test-npm-ls@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-test-npm-ls-1.0.0"
  ],
  "packages": [
    {
      "name": "test-npm-ls",
      "SPDXID": "SPDXRef-Package-test-npm-ls-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/test-npm-ls@1.0.0"
        }
      ]
    },
    {
      "name": "chai",
      "SPDXID": "SPDXRef-Package-chai-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/chai",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/chai@1.0.0"
        }
      ]
    },
    {
      "name": "dog",
      "SPDXID": "SPDXRef-Package-dog-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/dog",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dog@1.0.0"
        }
      ]
    },
    {
      "name": "foo",
      "SPDXID": "SPDXRef-Package-foo-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/foo@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-foo-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-chai-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dog-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-foo-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom missing (optional) dep > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "test-npm-ls@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/test-npm-sbom-1.0.0-00000000-0000-0000-0000-000000000000",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-test-npm-ls-1.0.0"
  ],
  "packages": [
    {
      "name": "test-npm-ls",
      "SPDXID": "SPDXRef-Package-test-npm-ls-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/test-npm-ls@1.0.0"
        }
      ]
    },
    {
      "name": "chai",
      "SPDXID": "SPDXRef-Package-chai-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/chai",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/chai@1.0.0"
        }
      ]
    },
    {
      "name": "foo",
      "SPDXID": "SPDXRef-Package-foo-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/foo@1.0.0"
        }
      ]
    },
    {
      "name": "dog",
      "SPDXID": "SPDXRef-Package-dog-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "node_modules/foo/node_modules/dog",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dog@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-foo-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-chai-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-test-npm-ls-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dog-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-foo-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/commands/sbom.js TAP sbom missing format > must match snapshot 1`] = `

`
