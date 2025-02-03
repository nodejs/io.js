/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/utils/sbom-spdx.js TAP node - with deps > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    },
    {
      "name": "dep1",
      "SPDXID": "SPDXRef-Package-dep1-0.0.1",
      "versionInfo": "0.0.1",
      "packageFileName": "node_modules/dep1",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep1@0.0.1"
        }
      ]
    },
    {
      "name": "dep2",
      "SPDXID": "SPDXRef-Package-dep2-0.0.2",
      "versionInfo": "0.0.2",
      "packageFileName": "node_modules/dep2",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep2@0.0.2"
        }
      ]
    },
    {
      "name": "dep3",
      "SPDXID": "SPDXRef-Package-dep3-0.0.3",
      "versionInfo": "0.0.3",
      "packageFileName": "node_modules/dep3",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep3@0.0.3"
        }
      ]
    },
    {
      "name": "dep4",
      "SPDXID": "SPDXRef-Package-dep4-0.0.4",
      "versionInfo": "0.0.4",
      "packageFileName": "node_modules/dep4",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep4@0.0.4"
        }
      ]
    },
    {
      "name": "dep5",
      "SPDXID": "SPDXRef-Package-dep5-0.0.5",
      "versionInfo": "0.0.5",
      "packageFileName": "node_modules/dep5",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep5@0.0.5"
        }
      ]
    },
    {
      "name": "dep6",
      "SPDXID": "SPDXRef-Package-dep6-0.0.6",
      "versionInfo": "0.0.6",
      "packageFileName": "node_modules/dep6",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep6@0.0.6"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep1-0.0.1",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "PREREQUISITE_FOR"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep2-0.0.2",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "OPTIONAL_DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep3-0.0.3",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DEV_DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep4-0.0.4",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep5-0.0.5",
      "relatedSpdxElement": "SPDXRef-Package-dep4-0.0.4",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep6-0.0.6",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "OPTIONAL_DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP node - with duplicate deps > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    },
    {
      "name": "dep1",
      "SPDXID": "SPDXRef-Package-dep1-0.0.1",
      "versionInfo": "0.0.1",
      "packageFileName": "node_modules/dep1",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep1@0.0.1"
        }
      ]
    },
    {
      "name": "dep2",
      "SPDXID": "SPDXRef-Package-dep2-0.0.2",
      "versionInfo": "0.0.2",
      "packageFileName": "node_modules/dep2",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/dep2@0.0.2"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep1-0.0.1",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    },
    {
      "spdxElementId": "SPDXRef-Package-dep2-0.0.2",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DEPENDENCY_OF"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - application package type > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "primaryPackagePurpose": "APPLICATION",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - from git url > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "https://github.com/foo/bar#1234",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0?vcs_url=https://github.com/foo/bar#1234"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - linked > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - with description > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "description": "Package description",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - with distribution url > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "https://registry.npmjs.org/root/-/root-1.0.0.tgz",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - with homepage > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "https://foo.bar/README.md",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - with integrity > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "NOASSERTION",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ],
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "d5191b14650a7b1e25bec07dca121f5a5b493397192947ed07678d6a3683bf7742304a78f62046d0ad78b87f0d9d7f483eec76fa62bb24610e0748e7e3cfc9eb"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - with license expression > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "(MIT OR Apache-2.0)",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - with license object > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "MIT",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`

exports[`test/lib/utils/sbom-spdx.js TAP single node - with single license > must match snapshot 1`] = `
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "root@1.0.0",
  "documentNamespace": "docns",
  "creationInfo": {
    "created": "2020-01-01T00:00:00.000Z",
    "creators": [
      "Tool: npm/cli-10.0.0 "
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-root-1.0.0"
  ],
  "packages": [
    {
      "name": "root",
      "SPDXID": "SPDXRef-Package-root-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "ISC",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/root@1.0.0"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-root-1.0.0",
      "relationshipType": "DESCRIBES"
    }
  ]
}
`
