# npm - a JavaScript package manager

### Requirements

You should be running a currently supported version of [Node.js](https://nodejs.org/en/download/) to run **`npm`**.  For a list of which versions of Node.js are currently supported, please see the [Node.js releases](https://nodejs.org/en/about/previous-releases) page.

### Installation

**`npm`** comes bundled with [**`node`**](https://nodejs.org/), & most third-party distributions, by default. Officially supported downloads/distributions can be found at: [nodejs.org/en/download](https://nodejs.org/en/download)

#### Direct Download

You can download & install **`npm`** directly from [**npmjs**.com](https://npmjs.com/) using our custom `install.sh` script:

```bash
curl -qL https://www.npmjs.com/install.sh | sh
```

#### Node Version Managers

If you're looking to manage multiple versions of **`Node.js`** &/or **`npm`**, consider using a [node version manager](https://github.com/search?q=node+version+manager+archived%3Afalse&type=repositories&ref=advsearch)

### Usage

```bash
npm <command>
```

### Links & Resources

* [**Documentation**](https://docs.npmjs.com/) - Official docs & how-tos for all things **npm**
    * Note: you can also search docs locally with `npm help-search <query>`
* [**Bug Tracker**](https://github.com/npm/cli/issues) - Search or submit bugs against the CLI
* [**Roadmap**](https://github.com/orgs/github/projects/4247/views/1?filterQuery=npm) - Track & follow along with our public roadmap
* [**Community Feedback and Discussions**](https://github.com/orgs/community/discussions/categories/npm) - Contribute ideas & discussion around the npm registry, website & CLI
* [**RFCs**](https://github.com/npm/rfcs) - Contribute ideas & specifications for the API/design of the npm CLI
* [**Service Status**](https://status.npmjs.org/) - Monitor the current status & see incident reports for the website & registry
* [**Project Status**](https://npm.github.io/statusboard/) - See the health of all our maintained OSS projects in one view
* [**Events Calendar**](https://calendar.google.com/calendar/u/0/embed?src=npmjs.com_oonluqt8oftrt0vmgrfbg6q6go@group.calendar.google.com) - Keep track of our Open RFC calls, releases, meetups, conferences & more
* [**Support**](https://www.npmjs.com/support) - Experiencing problems with the **npm** [website](https://npmjs.com) or [registry](https://registry.npmjs.org)? File a ticket [here](https://www.npmjs.com/support)

### Acknowledgments

* `npm` is configured to use the **npm Public Registry** at [https://registry.npmjs.org](https://registry.npmjs.org) by default; Usage of this registry is subject to **Terms of Use** available at [https://npmjs.com/policies/terms](https://npmjs.com/policies/terms)
* You can configure `npm` to use any other compatible registry you prefer. You can read more about configuring third-party registries [here](https://docs.npmjs.com/cli/v7/using-npm/registry)

### FAQ on Branding

#### Is it "npm" or "NPM" or "Npm"?

**`npm`** should never be capitalized unless it is being displayed in a location that is customarily all-capitals (ex. titles on `man` pages).

#### Is "npm" an acronym for "Node Package Manager"?

Contrary to popular belief, **`npm`** **is not** in fact an acronym for "Node Package Manager"; It is a recursive bacronymic abbreviation for **"npm is not an acronym"** (if the project was named "ninaa", then it would be an acronym). The precursor to **`npm`** was actually a bash utility named **"pm"**, which was the shortform name of **"pkgmakeinst"** - a bash function that installed various things on various platforms. If **`npm`** were to ever have been considered an acronym, it would be as "node pm" or, potentially "new pm".
