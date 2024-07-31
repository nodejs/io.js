/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/utils/sbom-cyclonedx.js TAP node - with deps > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
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
      "bom-ref": "dep1@0.0.1",
      "type": "library",
      "name": "dep1",
      "version": "0.0.1",
      "scope": "required",
      "purl": "pkg:npm/dep1@0.0.1",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": "node_modules/dep1"
        }
      ],
      "externalReferences": []
    },
    {
      "bom-ref": "dep2@0.0.2",
      "type": "library",
      "name": "dep2",
      "version": "0.0.2",
      "scope": "required",
      "purl": "pkg:npm/dep2@0.0.2",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": "node_modules/dep2"
        }
      ],
      "externalReferences": []
    }
  ],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": [
        "dep1@0.0.1",
        "dep2@0.0.2"
      ]
    },
    {
      "ref": "dep1@0.0.1",
      "dependsOn": []
    },
    {
      "ref": "dep2@0.0.2",
      "dependsOn": [
        "dep1@0.0.1"
      ]
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - application package type > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "application",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - bundled > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        },
        {
          "name": "cdx:npm:package:bundled",
          "value": "true"
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - development > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        },
        {
          "name": "cdx:npm:package:development",
          "value": "true"
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - extraneous > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        },
        {
          "name": "cdx:npm:package:extraneous",
          "value": "true"
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - from git url > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0?vcs_url=https://github.com/foo/bar#1234",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [
        {
          "type": "distribution",
          "url": "https://github.com/foo/bar#1234"
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - no package info > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - optional > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "optional",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - package lock only > must match snapshot 1`] = `
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
        "phase": "pre-build"
      }
    ],
    "tools": [
      {
        "vendor": "npm",
        "name": "cli",
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - private > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        },
        {
          "name": "cdx:npm:package:private",
          "value": "true"
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with author object > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Arthur",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with description > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "description": "Package description",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": []
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with distribution url > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [
        {
          "type": "distribution",
          "url": "https://registry.npmjs.org/root/-/root-1.0.0.tgz"
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with homepage > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [
        {
          "type": "website",
          "url": "https://foo.bar/README.md"
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with integrity > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [],
      "hashes": [
        {
          "alg": "SHA-512",
          "content": "d5191b14650a7b1e25bec07dca121f5a5b493397192947ed07678d6a3683bf7742304a78f62046d0ad78b87f0d9d7f483eec76fa62bb24610e0748e7e3cfc9eb"
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with issue tracker > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [
        {
          "type": "issue-tracker",
          "url": "https://foo.bar/issues"
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with license expression > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [],
      "licenses": [
        {
          "expression": "(MIT OR Apache-2.0)"
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with license object > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [],
      "licenses": [
        {
          "license": {
            "id": "MIT"
          }
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with repository url > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [
        {
          "type": "vcs",
          "url": "https://foo.bar"
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`

exports[`test/lib/utils/sbom-cyclonedx.js TAP single node - with single license > must match snapshot 1`] = `
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
        "version": "10.0.0 "
      }
    ],
    "component": {
      "bom-ref": "root@1.0.0",
      "type": "library",
      "name": "root",
      "version": "1.0.0",
      "scope": "required",
      "author": "Author",
      "purl": "pkg:npm/root@1.0.0",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": ""
        }
      ],
      "externalReferences": [],
      "licenses": [
        {
          "license": {
            "id": "ISC"
          }
        }
      ]
    }
  },
  "components": [],
  "dependencies": [
    {
      "ref": "root@1.0.0",
      "dependsOn": []
    }
  ]
}
`
