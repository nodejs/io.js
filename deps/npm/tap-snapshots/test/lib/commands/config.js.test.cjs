/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/commands/config.js TAP config list --json > output matches snapshot 1`] = `
{
  "cache": "{CACHE}",
  "color": {COLOR},
  "json": true,
  "projectloaded": "yes",
  "userloaded": "yes",
  "globalloaded": "yes",
  "access": null,
  "all": false,
  "allow-same-version": false,
  "also": null,
  "audit": true,
  "audit-level": null,
  "auth-type": "web",
  "before": null,
  "bin-links": true,
  "browser": null,
  "ca": null,
  "cache-max": null,
  "cache-min": 0,
  "cafile": null,
  "call": "",
  "cert": null,
  "cidr": null,
  "commit-hooks": true,
  "cpu": null,
  "depth": null,
  "description": true,
  "dev": false,
  "diff": [],
  "diff-ignore-all-space": false,
  "diff-name-only": false,
  "diff-no-prefix": false,
  "diff-dst-prefix": "b/",
  "diff-src-prefix": "a/",
  "diff-text": false,
  "diff-unified": 3,
  "dry-run": false,
  "editor": "{EDITOR}",
  "engine-strict": false,
  "expect-result-count": null,
  "expect-results": null,
  "fetch-retries": 2,
  "fetch-retry-factor": 10,
  "fetch-retry-maxtimeout": 60000,
  "fetch-retry-mintimeout": 10000,
  "fetch-timeout": 300000,
  "force": false,
  "foreground-scripts": false,
  "format-package-lock": true,
  "fund": true,
  "git": "git",
  "git-tag-version": true,
  "global": false,
  "globalconfig": "{CWD}/global/etc/npmrc",
  "global-style": false,
  "heading": "npm",
  "https-proxy": null,
  "if-present": false,
  "ignore-scripts": false,
  "include": [],
  "include-staged": false,
  "include-workspace-root": false,
  "init-author-email": "",
  "init-author-name": "",
  "init-author-url": "",
  "init-license": "ISC",
  "init-module": "{CWD}/home/.npm-init.js",
  "init-version": "1.0.0",
  "init.author.email": "",
  "init.author.name": "",
  "init.author.url": "",
  "init.license": "ISC",
  "init.module": "{CWD}/home/.npm-init.js",
  "init.version": "1.0.0",
  "install-links": false,
  "install-strategy": "hoisted",
  "key": null,
  "legacy-bundling": false,
  "legacy-peer-deps": false,
  "libc": null,
  "link": false,
  "local-address": null,
  "location": "user",
  "lockfile-version": null,
  "loglevel": "notice",
  "logs-dir": null,
  "logs-max": 10,
  "long": false,
  "maxsockets": 15,
  "message": "%s",
  "node-options": null,
  "noproxy": [
    ""
  ],
  "offline": false,
  "omit": [],
  "omit-lockfile-registry-resolved": false,
  "only": null,
  "optional": null,
  "os": null,
  "otp": null,
  "package": [],
  "package-lock": true,
  "package-lock-only": false,
  "pack-destination": ".",
  "parseable": false,
  "prefer-dedupe": false,
  "prefer-offline": false,
  "prefer-online": false,
  "prefix": "{CWD}/global",
  "preid": "",
  "production": null,
  "progress": {PROGRESS},
  "provenance": false,
  "provenance-file": null,
  "proxy": null,
  "read-only": false,
  "rebuild-bundle": true,
  "registry": "https://registry.npmjs.org/",
  "replace-registry-host": "npmjs",
  "save": true,
  "save-bundle": false,
  "save-dev": false,
  "save-exact": false,
  "save-optional": false,
  "save-peer": false,
  "save-prefix": "^",
  "save-prod": false,
  "sbom-format": null,
  "sbom-type": "library",
  "scope": "",
  "script-shell": null,
  "searchexclude": "",
  "searchlimit": 20,
  "searchopts": "",
  "searchstaleness": 900,
  "shell": "{SHELL}",
  "shrinkwrap": true,
  "sign-git-commit": false,
  "sign-git-tag": false,
  "strict-peer-deps": false,
  "strict-ssl": true,
  "tag": "latest",
  "tag-version-prefix": "v",
  "timing": false,
  "umask": 0,
  "unicode": false,
  "update-notifier": true,
  "usage": false,
  "user-agent": "npm/{npm-version} node/{node-version} {platform} {arch} workspaces/{workspaces} {ci}",
  "userconfig": "{CWD}/home/.npmrc",
  "version": false,
  "versions": false,
  "viewer": "{VIEWER}",
  "which": null,
  "workspace": [],
  "workspaces": null,
  "workspaces-update": true,
  "yes": null,
  "npm-version": "{NPM-VERSION}"
}
`

exports[`test/lib/commands/config.js TAP config list --long > output matches snapshot 1`] = `
; "default" config from default values

_auth = (protected)
access = null
all = false
allow-same-version = false
also = null
audit = true
audit-level = null
auth-type = "web"
before = null
bin-links = true
browser = null
ca = null
; cache = "{CACHE}" ; overridden by cli
cache-max = null
cache-min = 0
cafile = null
call = ""
cert = null
cidr = null
; color = {COLOR}
commit-hooks = true
cpu = null
depth = null
description = true
dev = false
diff = []
diff-dst-prefix = "b/"
diff-ignore-all-space = false
diff-name-only = false
diff-no-prefix = false
diff-src-prefix = "a/"
diff-text = false
diff-unified = 3
dry-run = false
editor = "{EDITOR}"
engine-strict = false
expect-result-count = null
expect-results = null
fetch-retries = 2
fetch-retry-factor = 10
fetch-retry-maxtimeout = 60000
fetch-retry-mintimeout = 10000
fetch-timeout = 300000
force = false
foreground-scripts = false
format-package-lock = true
fund = true
git = "git"
git-tag-version = true
global = false
global-style = false
globalconfig = "{CWD}/global/etc/npmrc"
heading = "npm"
https-proxy = null
if-present = false
ignore-scripts = false
include = []
include-staged = false
include-workspace-root = false
init-author-email = ""
init-author-name = ""
init-author-url = ""
init-license = "ISC"
init-module = "{CWD}/home/.npm-init.js"
init-version = "1.0.0"
init.author.email = ""
init.author.name = ""
init.author.url = ""
init.license = "ISC"
init.module = "{CWD}/home/.npm-init.js"
init.version = "1.0.0"
install-links = false
install-strategy = "hoisted"
json = false
key = null
legacy-bundling = false
legacy-peer-deps = false
libc = null
link = false
local-address = null
location = "user"
lockfile-version = null
loglevel = "notice"
logs-dir = null
logs-max = 10
; long = false ; overridden by cli
maxsockets = 15
message = "%s"
node-options = null
noproxy = [""]
npm-version = "{NPM-VERSION}"
offline = false
omit = []
omit-lockfile-registry-resolved = false
only = null
optional = null
os = null
otp = null
pack-destination = "."
package = []
package-lock = true
package-lock-only = false
parseable = false
prefer-dedupe = false
prefer-offline = false
prefer-online = false
prefix = "{CWD}/global"
preid = ""
production = null
progress = {PROGRESS}
provenance = false
provenance-file = null
proxy = null
read-only = false
rebuild-bundle = true
registry = "https://registry.npmjs.org/"
replace-registry-host = "npmjs"
save = true
save-bundle = false
save-dev = false
save-exact = false
save-optional = false
save-peer = false
save-prefix = "^"
save-prod = false
sbom-format = null
sbom-type = "library"
scope = ""
script-shell = null
searchexclude = ""
searchlimit = 20
searchopts = ""
searchstaleness = 900
shell = "{SHELL}"
shrinkwrap = true
sign-git-commit = false
sign-git-tag = false
strict-peer-deps = false
strict-ssl = true
tag = "latest"
tag-version-prefix = "v"
timing = false
umask = 0
unicode = false
update-notifier = true
usage = false
user-agent = "npm/{npm-version} node/{node-version} {platform} {arch} workspaces/{workspaces} {ci}"
userconfig = "{CWD}/home/.npmrc"
version = false
versions = false
viewer = "{VIEWER}"
which = null
workspace = []
workspaces = null
workspaces-update = true
yes = null

; "global" config from {CWD}/global/etc/npmrc

globalloaded = "yes"

; "user" config from {CWD}/home/.npmrc

userloaded = "yes"

; "project" config from {CWD}/prefix/.npmrc

projectloaded = "yes"

; "cli" config from command line options

cache = "{CACHE}"
color = {COLOR}
long = true
`

exports[`test/lib/commands/config.js TAP config list > output matches snapshot 1`] = `
; "global" config from {CWD}/global/etc/npmrc

globalloaded = "yes"

; "user" config from {CWD}/home/.npmrc

_auth = (protected)
//nerfdart:_auth = (protected)
//nerfdart:auth = (protected)
auth = (protected)
userloaded = "yes"

; "project" config from {CWD}/prefix/.npmrc

projectloaded = "yes"

; "cli" config from command line options

cache = "{CACHE}"
color = {COLOR}

; node bin location = {NODE-BIN-LOCATION}
; node version = {NODE-VERSION}
; npm local prefix = {CWD}/prefix
; npm version = {NPM-VERSION}
; cwd = {CWD}/prefix
; HOME = {CWD}/home
; Run \`npm config ls -l\` to show all defaults.
`

exports[`test/lib/commands/config.js TAP config list with publishConfig global > output matches snapshot 1`] = `
; "cli" config from command line options

cache = "{CACHE}"
color = {COLOR}
global = true

; node bin location = {NODE-BIN-LOCATION}
; node version = {NODE-VERSION}
; npm local prefix = {CWD}/prefix
; npm version = {NPM-VERSION}
; cwd = {CWD}/prefix
; HOME = {CWD}/home
; Run \`npm config ls -l\` to show all defaults.
`

exports[`test/lib/commands/config.js TAP config list with publishConfig local > output matches snapshot 1`] = `
; "cli" config from command line options

cache = "{CACHE}"
color = {COLOR}

; node bin location = {NODE-BIN-LOCATION}
; node version = {NODE-VERSION}
; npm local prefix = {CWD}/prefix
; npm version = {NPM-VERSION}
; cwd = {CWD}/prefix
; HOME = {CWD}/home
; Run \`npm config ls -l\` to show all defaults.

; "publishConfig" from {CWD}/prefix/package.json
; This set of config values will be used at publish-time.

_authToken = (protected)
registry = "https://some.registry"
`
