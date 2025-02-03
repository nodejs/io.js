/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/shrinkwrap.js TAP with hidden lockfile ancient > must match snapshot 1`] = `
{
  "localPrefix": {
    "node_modules": {
      ".package-lock.json": {
        "lockfileVersion": 1
      }
    }
  },
  "config": {},
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 1,
    "requires": true
  },
  "logs": [
    "created a lockfile as npm-shrinkwrap.json"
  ],
  "warn": []
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with hidden lockfile ancient upgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "node_modules": {
      ".package-lock.json": {
        "lockfileVersion": 1
      }
    }
  },
  "config": {
    "lockfile-version": 3
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {}
  },
  "logs": [
    "created a lockfile as npm-shrinkwrap.json with version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v1 -> v3"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with hidden lockfile existing > must match snapshot 1`] = `
{
  "localPrefix": {
    "node_modules": {
      ".package-lock.json": {
        "lockfileVersion": 2
      }
    }
  },
  "config": {},
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 2,
    "requires": true,
    "packages": {}
  },
  "logs": [
    "created a lockfile as npm-shrinkwrap.json"
  ],
  "warn": []
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with hidden lockfile existing downgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "node_modules": {
      ".package-lock.json": {
        "lockfileVersion": 2
      }
    }
  },
  "config": {
    "lockfile-version": 1
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 1,
    "requires": true
  },
  "logs": [
    "created a lockfile as npm-shrinkwrap.json with version 1"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v2 -> v1"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with hidden lockfile existing upgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "node_modules": {
      ".package-lock.json": {
        "lockfileVersion": 2
      }
    }
  },
  "config": {
    "lockfile-version": 3
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {}
  },
  "logs": [
    "created a lockfile as npm-shrinkwrap.json with version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v2 -> v3"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with nothing ancient > must match snapshot 1`] = `
{
  "localPrefix": {},
  "config": {},
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {}
  },
  "logs": [
    "created a lockfile as npm-shrinkwrap.json with version 3"
  ],
  "warn": []
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with nothing ancient upgrade > must match snapshot 1`] = `
{
  "localPrefix": {},
  "config": {
    "lockfile-version": 3
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {}
  },
  "logs": [
    "created a lockfile as npm-shrinkwrap.json with version 3"
  ],
  "warn": []
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with npm-shrinkwrap.json ancient > must match snapshot 1`] = `
{
  "localPrefix": {
    "npm-shrinkwrap.json": {
      "lockfileVersion": 1
    }
  },
  "config": {},
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "npm-shrinkwrap.json updated to version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v1 -> v3"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with npm-shrinkwrap.json ancient upgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "npm-shrinkwrap.json": {
      "lockfileVersion": 1
    }
  },
  "config": {
    "lockfile-version": 3
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "npm-shrinkwrap.json updated to version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v1 -> v3"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with npm-shrinkwrap.json existing > must match snapshot 1`] = `
{
  "localPrefix": {
    "npm-shrinkwrap.json": {
      "lockfileVersion": 2
    }
  },
  "config": {},
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 2,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "npm-shrinkwrap.json up to date"
  ],
  "warn": []
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with npm-shrinkwrap.json existing downgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "npm-shrinkwrap.json": {
      "lockfileVersion": 2
    }
  },
  "config": {
    "lockfile-version": 1
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 1,
    "requires": true
  },
  "logs": [
    "npm-shrinkwrap.json updated to version 1"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v2 -> v1"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with npm-shrinkwrap.json existing upgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "npm-shrinkwrap.json": {
      "lockfileVersion": 2
    }
  },
  "config": {
    "lockfile-version": 3
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "npm-shrinkwrap.json updated to version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v2 -> v3"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with package-lock.json ancient > must match snapshot 1`] = `
{
  "localPrefix": {
    "package-lock.json": {
      "lockfileVersion": 1
    }
  },
  "config": {},
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "package-lock.json has been renamed to npm-shrinkwrap.json and updated to version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v1 -> v3"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with package-lock.json ancient upgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "package-lock.json": {
      "lockfileVersion": 1
    }
  },
  "config": {
    "lockfile-version": 3
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "package-lock.json has been renamed to npm-shrinkwrap.json and updated to version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v1 -> v3"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with package-lock.json existing > must match snapshot 1`] = `
{
  "localPrefix": {
    "package-lock.json": {
      "lockfileVersion": 2
    }
  },
  "config": {},
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 2,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "package-lock.json has been renamed to npm-shrinkwrap.json"
  ],
  "warn": []
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with package-lock.json existing downgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "package-lock.json": {
      "lockfileVersion": 2
    }
  },
  "config": {
    "lockfile-version": 1
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 1,
    "requires": true
  },
  "logs": [
    "package-lock.json has been renamed to npm-shrinkwrap.json and updated to version 1"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v2 -> v1"
  ]
}
`

exports[`test/lib/commands/shrinkwrap.js TAP with package-lock.json existing upgrade > must match snapshot 1`] = `
{
  "localPrefix": {
    "package-lock.json": {
      "lockfileVersion": 2
    }
  },
  "config": {
    "lockfile-version": 3
  },
  "shrinkwrap": {
    "name": "prefix",
    "lockfileVersion": 3,
    "requires": true,
    "packages": {
      "": {
        "name": "prefix"
      }
    }
  },
  "logs": [
    "package-lock.json has been renamed to npm-shrinkwrap.json and updated to version 3"
  ],
  "warn": [
    "shrinkwrap Converting lock file (npm-shrinkwrap.json) from v2 -> v3"
  ]
}
`
