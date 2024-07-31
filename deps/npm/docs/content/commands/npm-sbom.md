---
title: npm-sbom
section: 1
description: Generate a Software Bill of Materials (SBOM)
---

### Synopsis

```bash
npm sbom
```

### Description

The `npm sbom` command generates a Software Bill of Materials (SBOM) listing the
dependencies for the current project. SBOMs can be generated in either
[SPDX](https://spdx.dev/) or [CycloneDX](https://cyclonedx.org/) format.

### Example CycloneDX SBOM

```json
{
  "$schema": "http://cyclonedx.org/schema/bom-1.5.schema.json",
  "bomFormat": "CycloneDX",
  "specVersion": "1.5",
  "serialNumber": "urn:uuid:09f55116-97e1-49cf-b3b8-44d0207e7730",
  "version": 1,
  "metadata": {
    "timestamp": "2023-09-01T00:00:00.001Z",
    "lifecycles": [
      {
        "phase": "build"
      }
    ],
    "tools": [
      {
        "vendor": "npm",
        "name": "cli",
        "version": "10.1.0"
      }
    ],
    "component": {
      "bom-ref": "simple@1.0.0",
      "type": "library",
      "name": "simple",
      "version": "1.0.0",
      "scope": "required",
      "author": "John Doe",
      "description": "simple react app",
      "purl": "pkg:npm/simple@1.0.0",
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
  "components": [
    {
      "bom-ref": "lodash@4.17.21",
      "type": "library",
      "name": "lodash",
      "version": "4.17.21",
      "scope": "required",
      "author": "John-David Dalton",
      "description": "Lodash modular utilities.",
      "purl": "pkg:npm/lodash@4.17.21",
      "properties": [
        {
          "name": "cdx:npm:package:path",
          "value": "node_modules/lodash"
        }
      ],
      "externalReferences": [
        {
          "type": "distribution",
          "url": "https://registry.npmjs.org/lodash/-/lodash-4.17.21.tgz"
        },
        {
          "type": "vcs",
          "url": "git+https://github.com/lodash/lodash.git"
        },
        {
          "type": "website",
          "url": "https://lodash.com/"
        },
        {
          "type": "issue-tracker",
          "url": "https://github.com/lodash/lodash/issues"
        }
      ],
      "hashes": [
        {
          "alg": "SHA-512",
          "content": "bf690311ee7b95e713ba568322e3533f2dd1cb880b189e99d4edef13592b81764daec43e2c54c61d5c558dc5cfb35ecb85b65519e74026ff17675b6f8f916f4a"
        }
      ],
      "licenses": [
        {
          "license": {
            "id": "MIT"
          }
        }
      ]
    }
  ],
  "dependencies": [
    {
      "ref": "simple@1.0.0",
      "dependsOn": [
        "lodash@4.17.21"
      ]
    },
    {
      "ref": "lodash@4.17.21",
      "dependsOn": []
    }
  ]
}
```

### Example SPDX SBOM

```json
{
  "spdxVersion": "SPDX-2.3",
  "dataLicense": "CC0-1.0",
  "SPDXID": "SPDXRef-DOCUMENT",
  "name": "simple@1.0.0",
  "documentNamespace": "http://spdx.org/spdxdocs/simple-1.0.0-bf81090e-8bbc-459d-bec9-abeb794e096a",
  "creationInfo": {
    "created": "2023-09-01T00:00:00.001Z",
    "creators": [
      "Tool: npm/cli-10.1.0"
    ]
  },
  "documentDescribes": [
    "SPDXRef-Package-simple-1.0.0"
  ],
  "packages": [
    {
      "name": "simple",
      "SPDXID": "SPDXRef-Package-simple-1.0.0",
      "versionInfo": "1.0.0",
      "packageFileName": "",
      "description": "simple react app",
      "primaryPackagePurpose": "LIBRARY",
      "downloadLocation": "NOASSERTION",
      "filesAnalyzed": false,
      "homepage": "NOASSERTION",
      "licenseDeclared": "MIT",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/simple@1.0.0"
        }
      ]
    },
    {
      "name": "lodash",
      "SPDXID": "SPDXRef-Package-lodash-4.17.21",
      "versionInfo": "4.17.21",
      "packageFileName": "node_modules/lodash",
      "description": "Lodash modular utilities.",
      "downloadLocation": "https://registry.npmjs.org/lodash/-/lodash-4.17.21.tgz",
      "filesAnalyzed": false,
      "homepage": "https://lodash.com/",
      "licenseDeclared": "MIT",
      "externalRefs": [
        {
          "referenceCategory": "PACKAGE-MANAGER",
          "referenceType": "purl",
          "referenceLocator": "pkg:npm/lodash@4.17.21"
        }
      ],
      "checksums": [
        {
          "algorithm": "SHA512",
          "checksumValue": "bf690311ee7b95e713ba568322e3533f2dd1cb880b189e99d4edef13592b81764daec43e2c54c61d5c558dc5cfb35ecb85b65519e74026ff17675b6f8f916f4a"
        }
      ]
    }
  ],
  "relationships": [
    {
      "spdxElementId": "SPDXRef-DOCUMENT",
      "relatedSpdxElement": "SPDXRef-Package-simple-1.0.0",
      "relationshipType": "DESCRIBES"
    },
    {
      "spdxElementId": "SPDXRef-Package-simple-1.0.0",
      "relatedSpdxElement": "SPDXRef-Package-lodash-4.17.21",
      "relationshipType": "DEPENDS_ON"
    }
  ]
}
```

### Package lock only mode

If package-lock-only is enabled, only the information in the package
lock (or shrinkwrap) is loaded.  This means that information from the
package.json files of your dependencies will not be included in the
result set (e.g. description, homepage, engines).

### Configuration

#### `omit`

* Default: 'dev' if the `NODE_ENV` environment variable is set to
  'production', otherwise empty.
* Type: "dev", "optional", or "peer" (can be set multiple times)

Dependency types to omit from the installation tree on disk.

Note that these dependencies _are_ still resolved and added to the
`package-lock.json` or `npm-shrinkwrap.json` file. They are just not
physically installed on disk.

If a package type appears in both the `--include` and `--omit` lists, then
it will be included.

If the resulting omit list includes `'dev'`, then the `NODE_ENV` environment
variable will be set to `'production'` for all lifecycle scripts.



#### `package-lock-only`

* Default: false
* Type: Boolean

If set to true, the current operation will only use the `package-lock.json`,
ignoring `node_modules`.

For `update` this means only the `package-lock.json` will be updated,
instead of checking `node_modules` and downloading dependencies.

For `list` this means the output will be based on the tree described by the
`package-lock.json`, rather than the contents of `node_modules`.



#### `sbom-format`

* Default: null
* Type: "cyclonedx" or "spdx"

SBOM format to use when generating SBOMs.



#### `sbom-type`

* Default: "library"
* Type: "library", "application", or "framework"

The type of package described by the generated SBOM. For SPDX, this is the
value for the `primaryPackagePurpose` field. For CycloneDX, this is the
value for the `type` field.



#### `workspace`

* Default:
* Type: String (can be set multiple times)

Enable running a command in the context of the configured workspaces of the
current project while filtering by running only the workspaces defined by
this configuration option.

Valid values for the `workspace` config are either:

* Workspace names
* Path to a workspace directory
* Path to a parent workspace directory (will result in selecting all
  workspaces within that folder)

When set for the `npm init` command, this may be set to the folder of a
workspace which does not yet exist, to create the folder and set it up as a
brand new workspace within the project.

This value is not exported to the environment for child processes.

#### `workspaces`

* Default: null
* Type: null or Boolean

Set to true to run the command in the context of **all** configured
workspaces.

Explicitly setting this to false will cause commands like `install` to
ignore workspaces altogether. When not set explicitly:

- Commands that operate on the `node_modules` tree (install, update, etc.)
will link workspaces into the `node_modules` folder. - Commands that do
other things (test, exec, publish, etc.) will operate on the root project,
_unless_ one or more workspaces are specified in the `workspace` config.

This value is not exported to the environment for child processes.
## See Also

* [package spec](/using-npm/package-spec)
* [dependency selectors](/using-npm/dependency-selectors)
* [package.json](/configuring-npm/package-json)
* [workspaces](/using-npm/workspaces)
