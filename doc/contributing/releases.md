# Node.js release process

This document describes the technical aspects of the Node.js release process.
The intended audience is those who have been authorized by the Node.js
Technical Steering Committee (TSC) to create, promote, and sign
official release builds for Node.js, hosted on <https://nodejs.org/>.

## Table of contents

* [Who can make a release?](#who-can-make-a-release)
  * [1. Jenkins release access](#1-jenkins-release-access)
  * [2. \<nodejs.org> access](#2-nodejsorg-access)
  * [3. A publicly listed GPG key](#3-a-publicly-listed-gpg-key)
* [How to create a release](#how-to-create-a-release)
  * [0. Pre-release steps](#0-pre-release-steps)
  * [1. Update the staging branch](#1-update-the-staging-branch)
  * [2. Create a new branch for the release](#2-create-a-new-branch-for-the-release)
  * [3. Update `src/node_version.h`](#3-update-srcnode_versionh)
  * [4. Update the changelog](#4-update-the-changelog)
  * [5. Create release commit](#5-create-release-commit)
  * [6. Propose release on GitHub](#6-propose-release-on-github)
  * [7. Ensure that the release branch is stable](#7-ensure-that-the-release-branch-is-stable)
    * [7.1 Updating the release _(optional)_](#71-updating-the-release-optional)
  * [8. Produce a nightly build _(optional)_](#8-produce-a-nightly-build-optional)
  * [9. Produce release builds](#9-produce-release-builds)
  * [10. Test the build](#10-test-the-build)
  * [11. Tag and sign the release commit](#11-tag-and-sign-the-release-commit)
  * [12. Set up for the next release](#12-set-up-for-the-next-release)
  * [13. Cherry-pick the release commit to `main`](#13-cherry-pick-the-release-commit-to-main)
  * [14. Push the release tag](#14-push-the-release-tag)
  * [15. Promote and sign the release builds](#15-promote-and-sign-the-release-builds)
  * [16. Check the release](#16-check-the-release)
  * [17. Create a blog post](#17-create-a-blog-post)
  * [18. Create the release on GitHub](#18-create-the-release-on-github)
  * [19. Announce](#19-announce)
  * [20. Celebrate](#20-celebrate)
* [LTS releases](#lts-releases)
* [Major releases](#major-releases)

## Who can make a release?

Release authorization is given by the Node.js TSC. Once authorized, an
individual must have the following:

### 1. Jenkins release access

There are three relevant Jenkins jobs that should be used for a release flow:

**a.** **Test runs:**
**[node-test-pull-request](https://ci.nodejs.org/job/node-test-pull-request/)**
is used for a final full-test run to ensure that the current _HEAD_ is stable.

**b.** **Nightly builds:** (optional)
**[iojs+release](https://ci-release.nodejs.org/job/iojs+release/)** can be used
to create a nightly release for the current _HEAD_ if public test releases are
required. Builds triggered with this job are published straight to
<https://nodejs.org/download/nightly/> and are available for public download.

**c.** **Release builds:**
**[iojs+release](https://ci-release.nodejs.org/job/iojs+release/)** does all of
the work to build all required release assets. Promotion of the release files is
a manual step once they are ready (see below).

The [Node.js build team](https://github.com/nodejs/build) is able to provide
this access to individuals authorized by the TSC.

### 2. \<nodejs.org> access

The _dist_ user on nodejs.org controls the assets available in
<https://nodejs.org/download/>. <https://nodejs.org/dist/> is an alias for
<https://nodejs.org/download/release/>.

The Jenkins release build workers upload their artifacts to the web server as
the _staging_ user. The _dist_ user has access to move these assets to public
access while, for security, the _staging_ user does not.

Nightly builds are promoted automatically on the server by a cron task for the
_dist_ user.

Release builds require manual promotion by an individual with SSH access to the
server as the _dist_ user. The
[Node.js build team](https://github.com/nodejs/build) is able to provide this
access to individuals authorized by the TSC.

### 3. A publicly-listed GPG key

A `SHASUMS256.txt` file is produced for every promoted build, nightly, and
releases. Additionally for releases, this file is signed by the individual
responsible for that release. In order to be able to verify downloaded binaries,
the public should be able to check that the `SHASUMS256.txt` file has been
signed by someone who has been authorized to create a release.

The public keys should be fetchable from a known third-party keyserver.
The OpenPGP keyserver at <https://keys.openpgp.org/> is recommended.
Use the [submission](https://keys.openpgp.org/upload) form to submit
a new public key, and make sure to verify the associated email.
You'll need to do an ASCII-armored export of your key first:

```bash
gpg --armor --export email@server.com > ~/nodekey.asc
```

Keys should be fetchable via:

```bash
gpg --keyserver hkps://keys.openpgp.org --recv-keys <FINGERPRINT>
```

The key you use may be a child/subkey of an existing key.

Additionally, full GPG key fingerprints for individuals authorized to release
should be listed in the Node.js GitHub README.md file.

## How to create a release

Notes:

* Dates listed below as _"YYYY-MM-DD"_ should be the date of the release **as
  UTC**. Use `date -u +'%Y-%m-%d'` to find out what this is.
* Version strings are listed below as _"vx.y.z"_ or _"x.y.z"_. Substitute for
  the release version.
* Examples will use the fictional release version `1.2.3`.
* When preparing a security release, follow the security steps in the details
  sections.

### 0. Pre-release steps

Before preparing a Node.js release, the Build Working Group must be notified at
least one business day in advance of the expected release. Coordinating with
Build is essential to make sure that the CI works, release files are published,
and the release blog post is available on the project website.

Build can be contacted best by opening up an issue on the [Build issue
tracker][].

When preparing a security release, contact Build at least two weekdays in
advance of the expected release. To ensure that the security patch(es) can be
properly tested, run a `node-test-pull-request` job against the `main` branch
of the `nodejs-private/node-private` repository a day or so before the
[CI lockdown procedure][] begins. This is to confirm that Jenkins can properly
access the private repository.

### 1. Update the staging branch

Checkout the staging branch locally.

```bash
git remote update
git checkout v1.x-staging
git reset --hard upstream/v1.x-staging
```

If the staging branch is not up to date relative to `main`, bring the
appropriate PRs and commits into it.

Go through PRs with the label `vN.x`. e.g. [PRs with the
`v8.x` label](https://github.com/nodejs/node/pulls?q=is%3Apr+is%3Aopen+sort%3Aupdated-desc+label%3Av8.x)
and `baking-for-lts` label if preparing a release for an LTS line.

For each PR:

* Run or check that there is a passing CI.
* Check approvals (you can approve yourself).
* Check that the commit metadata was not changed from the `main` commit.
* If there are merge conflicts, ask the PR author to rebase.
  Simple conflicts can be resolved when landing.
* If `baking-for-lts` is present, check if the PR is ready to be landed.
  If it is, remove the `baking-for-lts` label.

When landing the PR add the `Backport-PR-URL:` line to each commit. Close the
backport PR with `Landed in ...`. Update the label on the original PR from
`backport-requested-vN.x` to `backported-to-vN.x`.

You can add the `Backport-PR-URL` metadata by using `--backport` with
`git node land`

```bash
git node land --backport $PR-NUMBER
```

To determine the relevant commits, use
[`branch-diff`](https://github.com/nodejs/branch-diff). The tool is available on
npm and should be installed globally or run with `npx`. It depends on our commit
metadata, as well as the GitHub labels such as `semver-minor` and
`semver-major`. One drawback is that when the `PR-URL` metadata is accidentally
omitted from a commit, the commit will show up because it's unsure if it's a
duplicate or not.

For a list of commits that could be landed in a minor release on v1.x:

```bash
N=1 sh -c 'branch-diff v$N.x-staging upstream/main --exclude-label=semver-major,dont-land-on-v$N.x,backport-requested-v$N.x,backport-blocked-v$N.x,backport-open-v$N.x,backported-to-v$N.x --filter-release --format=simple'
```

If the target branch is an LTS line, you should also exclude the `baking-for-lts`:

```bash
N=1 sh -c 'branch-diff v$N.x-staging upstream/main --exclude-label=semver-major,dont-land-on-v$N.x,backport-requested-v$N.x,backport-blocked-v$N.x,backport-open-v$N.x,backported-to-v$N.x,baking-for-lts --filter-release --format=simple'
```

Previously released commits and version bumps do not need to be
cherry-picked.

Carefully review the list of commits:

* Checking for errors (incorrect `PR-URL`)
* Checking semver status - Commits labeled as `semver-minor` or `semver-major`
  should only be cherry-picked when appropriate for the type of release being
  made.
* If you think it's risky and the change should wait for a while, add the
  `baking-for-lts` tag.

When you are ready to cherry-pick commits, you can automate with the following
command.

```bash
N=1 sh -c 'branch-diff v$N.x-staging upstream/main --exclude-label=semver-major,dont-land-on-v$N.x,backport-requested-v$N.x,backport-blocked-v$N.x,backport-open-v$N.x,backported-to-v$N.x --filter-release --format=sha --reverse' | xargs git cherry-pick -S
```

If the target branch is an LTS line, you should also exclude the `baking-for-lts`:

```bash
N=1 sh -c 'branch-diff v$N.x-staging upstream/main --exclude-label=semver-major,dont-land-on-v$N.x,backport-requested-v$N.x,backport-blocked-v$N.x,backport-open-v$N.x,backported-to-v$N.x,baking-for-lts --filter-release --format=sha --reverse' | xargs git cherry-pick -S
```

<sup>For patch releases, make sure to add the `semver-minor` tag
to `exclude-label`<sup>

When cherry-picking commits, if there are simple conflicts you can resolve
them. Otherwise, add the `backport-requested-vN.x` label to the original PR
and post a comment stating that it does not land cleanly and will require a
backport PR. You can refer the owner of the PR to the "[Backporting to Release
Lines](https://github.com/nodejs/node/blob/HEAD/doc/contributing/backporting-to-release-lines.md)" guide.

If commits were cherry-picked in this step, check that the test still pass.

```bash
make test
```

Then, push to the staging branch to keep it up-to-date.

```bash
git push upstream v1.x-staging
```

<details>
<summary>Security release</summary>

Security releases with private patches need to be prepared in the `nodejs-private`
GitHub organization.

Add the `nodejs-private` remote:

```bash
git remote add private git@github.com:nodejs-private/node-private.git
```

For security releases, we generally try to only include the security patches.
As there may already be commits on the `vN.x-staging` branch, it is preferable
to checkout the `vN.x` branch and build up the release directly in a proposal
branch.

```console
$ git checkout vN.x
$ git reset --hard upstream/vN.x
```

The list of patches to include should be listed in the "Next Security Release"
issue in `nodejs-private`. Ask the security release steward if you're unsure.

The `git node land` tool does not work with the `nodejs-private`
organization. To land a PR in Node.js private, use `git cherry-pick` to apply
each commit from the PR. You will also need to manually apply the PR
metadata (`PR-URL`, `Reviewed-by`, etc.) by amending the commit messages. If
known, additionally include `CVE-ID: CVE-XXXX-XXXXX` in the commit metadata.

**Note**: Do not run CI on the PRs in `nodejs-private` until CI is locked down.
You can integrate the PRs into the proposal without running full CI.

</details>

### 2. Create a new branch for the release

> \[!TIP] Once the staging branch is up-to-date you can use the
> [`create-release-proposal`][] action to generate the proposal.

```bash
gh workflow run "Create Release Proposal" -f release-line=N -f release-date=YYYY-MM-DD
```

If you prefer to run it locally you can either run
`git node release --prepare`:

```bash
git node release -S --prepare x.y.z
```

to automate the remaining steps until step 6 or you can perform it manually
following the below steps.

***

Create a new branch named `vx.y.z-proposal`, off the corresponding staging
branch.

```bash
git checkout -b v1.2.3-proposal upstream/v1.x-staging
```

You can also run:

```bash
git node release -S --prepare --security --filterLabel vX.x
```

Example:

```bash
git checkout v20.x
git node release -S --prepare --security --filterLabel v20.x
```

to automate the remaining steps until step 6 or you can perform it manually
following the below steps.

<details>
<summary>Security release</summary>

When performing Security Releases, the `vN.x.x-proposal` branch should be
branched off of `vN.x`.

```console
$ git checkout -b v1.2.3-proposal upstream/v1.x
git cherry-pick  ...  # cherry-pick nodejs-private PR commits directly into the proposal
```

Be sure to label the CVE fixes as `notable-change` in the nodejs-private repository.
This will ensure they are included in the "Notable Changes" section of the CHANGELOG.

</details>

### 3. Update `src/node_version.h`

Set the version for the proposed release using the following macros, which are
already defined in `src/node_version.h`:

```c
#define NODE_MAJOR_VERSION x
#define NODE_MINOR_VERSION y
#define NODE_PATCH_VERSION z
```

Set the `NODE_VERSION_IS_RELEASE` macro value to `1`. This causes the build to
be produced with a version string that does not have a trailing pre-release tag:

```c
#define NODE_VERSION_IS_RELEASE 1
```

### 4. Update the changelog

#### Step 1: Collect the formatted list of changes

Collect a formatted list of commits since the last release. Use
[`changelog-maker`](https://github.com/nodejs/changelog-maker) to do this:

```bash
changelog-maker --group --markdown
```

`changelog-maker` counts commits since the last tag and if the last tag
in the repository was not on the current branch you may have to supply a
`--start-ref` argument:

```bash
changelog-maker --group --markdown --filter-release --start-ref v1.2.2
```

`--filter-release` will remove the release commit from the previous release.

#### Step 2: Update the appropriate doc/changelogs/CHANGELOG\_\*.md file

There is a separate `CHANGELOG_Vx.md` file for each major Node.js release line.
These are located in the `doc/changelogs/` directory. Once the formatted list of
changes is collected, it must be added to the top of the relevant changelog file
in the release branch (e.g. a release for Node.js v4 would be added to the
`/doc/changelogs/CHANGELOG_V4.md`).

**Please do _not_ add the changelog entries to the root `CHANGELOG.md` file.**

The new entry should take the following form:

```markdown
<a id="x.y.x"></a>
## YYYY-MM-DD, Version x.y.z (Release Type), @releaser

### Notable changes

* List notable changes here
* ...

### Commits

* Include the full list of commits since the last release here. Do not include "Working on X.Y.Z+1" commits.
```

The release type should be either Current, LTS, or Maintenance, depending on the
type of release being produced.

By default, the `### Notable changes` section should be populated with the
commits in the release that have either the `notable-change` or `semver-minor`
label. Some `semver-minor` features may be determined by the releaser, or
indicated by another contributor, to not be appropriate to be listed as a
notable. The ultimate decision rests with the releaser.

You can use `branch-diff` to get a list of commits with the `notable-change`
label:

```bash
branch-diff upstream/v1.x v1.2.3-proposal --require-label=notable-change --plaintext
```

Be sure that the `<a>` tag, as well as the two headings, are not indented at
all.

<details>
<summary>Security release</summary>

For security releases, it is necessary to include more detailed information
including which vulnerabilities have been fixed, and any revert flags or
workarounds to revert to the old behavior.

You can use the following template as a guide:

```markdown
<a id="x.y.x"></a>
## YYYY-MM-DD, Version x.y.z (Release Type), @releaser

This is a security release.

### Notable changes

* <CVE Title> (High|Medium|Low)(CVE-XXXX-XXXXX)
* ...

### Commits

* Include the full list of commits since the last release here. Do not include "Working on X.Y.Z+1" commits.
```

Alternatively, refer to one of the [previous security release changelog entries](https://github.com/nodejs/node/blob/main/doc/changelogs/CHANGELOG_V17.md#2022-01-10-version-1731-current-bethgriggs)
to get an idea of the structure and level of detail.

For each fix, use your judgement as to whether a subheading is necessary to
describe the fix in more detail. Much of this information should be able to be
lifted from the "post-release" announcement (should be available in
`nodejs-private`). If the CVE is being fixed in multiple release lines, try to
coordinate the changelog content between the other release so the descriptions
are consistent.

</details>

At the top of the root `CHANGELOG.md` file, there is a table indexing all
releases in each major release line. A link to the new release needs to be added
to it. Follow the existing examples and be sure to add the release to the _top_
of the list. The most recent release for each release line is shown in **bold**
in the index. When updating the index, please make sure to update the display
accordingly by removing the bold styling from the previous release.

Run `make format-md` to ensure the `CHANGELOG_Vx.md` and `CHANGELOG.md` files
are formatted correctly.

#### Step 3: Update any REPLACEME and DEP00XX tags in the docs

If this release includes new APIs then it is necessary to document that they
were first added in this version. The relevant commits should already include
`REPLACEME` tags as per the example in the
[docs README](../../tools/doc/README.md). Check for these tags with

```bash
grep REPLACEME doc/api/*.md
```

and substitute this node version with

```bash
sed -i "s/REPLACEME/$VERSION/g" doc/api/*.md
```

For macOS requires the extension to be specified.

```bash
sed -i "" "s/REPLACEME/$VERSION/g" doc/api/*.md
```

or

```bash
perl -pi -e "s/REPLACEME/$VERSION/g" doc/api/*.md
```

`$VERSION` should be prefixed with a `v`.

If this release includes any new deprecations it is necessary to ensure that
those are assigned a proper static deprecation code. These are listed in the
docs (see `doc/api/deprecations.md`) and in the source as `DEP00XX`. The code
must be assigned a number (e.g. `DEP0012`). This assignment should
occur when the PR is landed, but a check will be made when the release build is
run.

### 5. Create release commit

The `CHANGELOG.md`, `doc/changelogs/CHANGELOG_Vx.md`, `src/node_version.h`, and
`REPLACEME` changes should be the final commit that will be tagged for the
release. When committing these to git, use the following message format:

```text
YYYY-MM-DD, Version x.y.z (Release Type)

Notable changes:

* Copy the notable changes list here, reformatted for plain-text

PR-URL: TBD
```

<details>
<summary>Security release</summary>

For security releases, begin the commit message with the phrase
`This is a security release.` to allow the
[distribution indexer](https://github.com/nodejs/nodejs-dist-indexer) to
identify it as such:

```text
YYYY-MM-DD, Version x.y.z (Release Type)

This is a security release.

Notable changes:

* Copy the notable changes list here, reformatted for plain-text

PR-URL: TBD
```

**Note**: Ensure to push the proposal branch to the nodejs-private repository.
Otherwise, you will leak the commits before the security release.

</details>

### 6. Propose release on GitHub

Push the release branch to `nodejs/node`, not to your own fork. This allows
release branches to more easily be passed between members of the release team if
necessary.

Create a pull request targeting the correct release line. For example, a
`v5.3.0-proposal` PR should target `v5.x`, not `main`. Paste the CHANGELOG
modifications into the body of the PR so that collaborators can see what is
changing. These PRs should be left open for at least 24 hours, and can be
updated as new commits land. If the CHANGELOG pasted into the pull request
is long enough that it slows down the GitHub UI, consider pasting the commits
into `<details>` tags or in follow up comments.

If using the `<details>` tag, use the following format:

```markdown
<details>
<summary>Commits</summary>

* Full list of commits...
</details>
```

If you need any additional information about any of the commits, this PR is a
good place to @-mention the relevant contributors.

After opening the PR, update the release commit to include `PR-URL` metadata and
force-push the proposal.

<details>
<summary>Security release</summary>

If there are private security patches, remember to push and open the proposal
in the `nodejs-private` GitHub repository.

</details>

### 7. Ensure that the release branch is stable

Run a **[`node-test-pull-request`](https://ci.nodejs.org/job/node-test-pull-request/)**
test run to ensure that the build is stable and the HEAD commit is ready for
release.

Also run a **[`node-test-commit-v8-linux`](https://ci.nodejs.org/job/node-test-commit-v8-linux/)**
test run if the release contains changes to `deps/v8`.

Perform some smoke-testing. There is the
**[`citgm-smoker`](https://ci.nodejs.org/job/citgm-smoker/)** CI job for this
purpose. Run it once with the base `vx.x` branch as a reference and with the
proposal branch to check if new regressions could be introduced in the
ecosystem.

Use `ncu-ci` to compare `vx.x` run (10) and proposal branch (11)

```bash
npm i -g @node-core/utils
ncu-ci citgm 10 11
```

<details>
<summary>Security release</summary>

If there are private security patches, do not run any CI jobs on the proposal
until CI has been locked down. The security steward should be coordinating this
with the Build Working Group.

</details>

#### 7.1 Updating the release _(optional)_

Sometimes a release might be deferred to the subsequent day due to several
conditions:

* Unstable CI
* Late CI completion

And when it happens, the CHANGELOG\_Vx and the commit metadata needs to be
updated according to the new target date.

However, if it's just the changelog/commit message that has changed since the
last CI execution, there's no need to rerun CI, V8, or CITGM workflows.
The PR still needs a clean GitHub action run, and the original CI, V8, and
CITGM run should be in a visible comment.

There are some cases when a commit needs to be dropped or adjusted,
consider using the following approach:

1. Update staging

```bash
git checkout v1.x-staging
git rebase -i $HASH_PREVIOUS_BAD_COMMIT
# ... drop or edit the bad commit(s)
git push -f upstream v1.x-staging
```

2. Rebase the proposal against the updated staging branch

```bash
git checkout v1.2.3-proposal
git checkout -b v1.2.3-proposal-tmp
git checkout v1.2.3-proposal

git reset --hard upstream/v1.x-staging
git cherry-pick v1.2.3-proposal-tmp
```

Note the `tmp` branch was created just to save the release commit.

3. Re-run `changelog-maker` and update the CHANGELOG\_Vx to include the new
   Git SHAs. Remember to update the commit message if applicable.

### 8. Produce a nightly build _(optional)_

If there is a reason to produce a test release for the purpose of having others
try out installers or specifics of builds, produce a nightly build using
**[iojs+release](https://ci-release.nodejs.org/job/iojs+release/)** and wait for
it to drop in <https://nodejs.org/download/nightly/>. Follow the directions and
enter a proper length commit SHA, enter a date string, and select "nightly" for
"disttype".

This is particularly recommended if there has been recent work relating to the
macOS or Windows installers as they are not tested in any way by CI.

<details>
<summary>Security release</summary>

Do not produce a release candidate build.

</details>

### 9. Produce release builds

Use **[iojs+release](https://ci-release.nodejs.org/job/iojs+release/)** to
produce release artifacts. Enter the commit that you want to build from and
select "release" for "disttype".

Artifacts from each worker are uploaded to Jenkins and are available if further
testing is required. Use this opportunity particularly to test macOS and Windows
installers if there are any concerns. Click through to the individual workers
for a run to find the artifacts.

All release workers should achieve "SUCCESS" (and be green, not red). A release
with failures should not be promoted as there are likely problems to be
investigated.

You can rebuild the release as many times as you need prior to promoting them if
you encounter problems.

If you have an error on Windows and need to start again, be aware that you'll
get immediate failure unless you wait up to 2 minutes for the linker to stop
from previous jobs. i.e. if a build fails after having started compiling, that
worker will still have a linker process that's running for another couple of
minutes which will prevent Jenkins from clearing the workspace to start a new
one. This isn't a big deal, it's just a hassle because it'll result in another
failed build if you start again!

### 10. Test the build

Jenkins collects the artifacts from the builds, allowing you to download and
install the new build. Make sure that the build appears correct. Check the
version numbers, and perform some basic checks to confirm that all is well with
the build before moving forward. Use the following list as a baseline:

* `process.version` is as expected
* `process.release` is as expected
* `process.versions` is as expected (for example, `openssl` or `llhttp` version
  must be in the expected updated version)
* npm version (check it matches what we expect)
* Run the test suite against the built binaries (optional)
  * Remember to use the proposal branch
  * Run `make build-addons` before running the tests
  * Remove `config.gypi` file

```bash
./tools/test.py --shell ~/Downloads/node-v18.5.0-linux-x64/bin/node
```

### 11. Tag and sign the release commit

Once you have produced builds that you're happy with you can either run
`git node release --promote`

```bash
git node release -S --promote https://github.com/nodejs/node/pull/XXXX
```

to automate the remaining steps until step 16 or you can perform it manually
following the below steps.

***

Create a new tag: By waiting until this stage to create tags, you can discard
a proposed release if something goes wrong or additional commits are required.
Once you have created a tag and pushed it to GitHub, you _**must not**_ delete
and re-tag. If you make a mistake after tagging then you'll have to version-bump
and start again and count that tag/version as lost.

Tag summaries have a predictable format. Look at a recent tag to see:

```bash
git tag -v v6.0.0
```

The message should look something like
`2016-04-26 Node.js v6.0.0 (Current) Release`.

Install `git-secure-tag` npm module:

```bash
npm install -g git-secure-tag
```

> Ensure to disable `--follow-tags` in your git settings using: `git config push.followTags false`

If your private key is protected by a passphrase, you might need to run:

```bash
export GPG_TTY=$(tty)
```

before creating the tag.

To create a tag use the following command:

```bash
git secure-tag <vx.y.z> <commit-sha> -sm "YYYY-MM-DD Node.js vx.y.z (<release-type>) Release"
```

<sup>The commit-sha is the release commit. You can get it easily by running `git rev-parse HEAD`</sup>

`release-type` is either "Current" or "LTS". For LTS releases, you should also
include the release code name.

```text
2019-10-22 Node.js v10.17.0 'Dubnium' (LTS) Release
```

The tag **must** be signed using the GPG key that's listed for you on the
project README.

**Note**: Don't push the tag to remote at this point.

### 12. Set up for the next release

On release proposal branch, edit `src/node_version.h` again and:

* Increment `NODE_PATCH_VERSION` by one
* Change `NODE_VERSION_IS_RELEASE` back to `0`

Commit this change with the following commit message format:

```text
Working on vx.y.z # where 'z' is the incremented patch number

PR-URL: <full URL to your release proposal PR>
```

This sets up the branch so that nightly builds are produced with the next
version number _and_ a pre-release tag.

Merge your release proposal branch into the stable branch that you are releasing
from and rebase the corresponding staging branch on top of that.

```bash
git checkout v1.x
git merge --ff-only v1.2.3-proposal
git push upstream v1.x
git checkout v1.x-staging
git rebase v1.x
git push upstream v1.x-staging
```

<details>
<summary>Security release</summary>

For security releases, you can start merging the release in the `nodejs-private`
GitHub organization in advance by following the same steps:

```bash
git checkout v1.x
git merge --ff-only v1.2.3-proposal
git push private v1.x
git checkout v1.x-staging
git rebase v1.x
git push private v1.x-staging
```

Once all releasers are ready, you can push each of the branches to the public
repository.

</details>

### 13. Cherry-pick the release commit to `main`

```bash
git checkout main
git pull upstream main
git cherry-pick --strategy-option=diff-algorithm=patience v1.x^
```

Git should stop to let you fix conflicts.

Revert all changes that were made to `src/node_version.h`:

```bash
git restore --source=upstream/main src/node_version.h
```

<details>
<summary>Major version release</summary>

On the main branch, instead of reverting changes made to `src/node_version.h`
edit it instead and:

* Increment `NODE_MAJOR_VERSION` by one
* Reset `NODE_PATCH_VERSION` to `0`
* Change `NODE_VERSION_IS_RELEASE` back to `0`

Amend the current commit to apply the changes:

```bash
git commit --amend
```

</details>

Even if there are no conflicts, ensure that you revert all the changes that were
made to `src/node_version.h`. `NODE_VERSION_IS_RELEASE` must be `0`.

<sup>Edit `src/node_version.h`, revert `NODE_VERSION_IS_RELEASE` back to `0`, and `git commit --amend`</sup>

If there are conflicts in `doc` due to updated `REPLACEME`
placeholders (that happens when a change previously landed on another release
branch), keep both version numbers. Convert the YAML field to an array if it is
not already one.

[It's possible that the `cherry-pick` step will end up adding and/or
changing unwanted lines](https://github.com/nodejs/Release/issues/771),
please validate the changes in `doc/` folder files before confirming/continuing
the cherry-pick step.

Then finish cherry-picking and push the commit upstream:

```bash
git add src/node_version.h doc
git diff --staged src doc # read output to validate that changes shows up as expected
git cherry-pick --continue
make lint-md && make lint-cpp
git push upstream main
```

**Do not** cherry-pick the "Working on vx.y.z" commit to `main`.

<details>
<summary>Security release</summary>

For security releases, you will also need to land the fixes on the `main`
branch (if they apply there). Often, you can just cherry-pick the same commit
that landed in the `current` security release which should already have the
metadata.

It is useful to first push the patches to `private/main` to check that the
GitHub actions runs pass, before pushing to `upstream/main`:

```bash
git checkout main
git reset --hard upstream/main
git cherry-pick ... # apply the patches which apply to main
git push private main # push to private main first run GitHub actions
git push upstream main
```

</details>

### 14. Push the release tag

Push the tag to the repository before you promote the builds. If you
haven't pushed your tag first, then build promotion won't work properly.
Push the tag using the following command:

```bash
git push upstream v1.2.3
```

_Note_: Please do not push the tag unless you are ready to complete the
remainder of the release steps.

### 15. Promote and sign the release builds

**The same individual who signed the release tag must be the one
to promote the builds as the `SHASUMS256.txt` file needs to be signed with the
same GPG key!**

Use `tools/release.sh` to promote and sign the build. Before doing this, you'll
need to ensure you've loaded the correct ssh key, or you'll see the following:

If your GPG key is protected by a password, you might need to run:

```console
$ export GPG_TTY=$(tty)
```

```console
# Checking for releases ...
Enter passphrase for key '/Users/<user>/.ssh/id_rsa':
dist@direct.nodejs.org's password:
```

The key can be loaded either with `ssh-add`:

```bash
# Substitute node_id_rsa with whatever you've named the key
ssh-add ~/.ssh/node_id_rsa
```

or at runtime with:

```bash
# Substitute node_id_rsa with whatever you've named the key
./tools/release.sh -i ~/.ssh/node_id_rsa
```

You can also specify a different ssh server address to connect to by defining
a `NODEJS_RELEASE_HOST` environment variable:

```bash
# Substitute proxy.xyz with whatever address you intend to use
NODEJS_RELEASE_HOST=proxy.xyz ./tools/release.sh
```

In case `gpg` is unable to autoselect a key, you can retry using the
`-a` option to enable an interactive interface:

```bash
./tools/release.sh -a
```

> \[!TIP]
> Sometimes, due to machines being overloaded or other external factors,
> the files at <https://nodejs.org/dist/index.json>, <https://nodejs.org/dist/index.tab>
> or `SHASUMS256.txt` may not be generated correctly.
> In this case you can repeat the signing step in order
> to fix it. e.g: `./tools/release.sh -s`.

`tools/release.sh` will perform the following actions when run:

<details>

**a.** Select a GPG key from your private keys. It will use a command similar
to: `gpg --list-secret-keys` to list your keys. If you don't have any keys, it
will bail. If you have only one key, it will use that. If you have more than
one key it will ask you to select one from the list. Be sure to use the same
key that you signed your git tag with.

**b.** Log in to the server via SSH and check for releases that can be promoted,
along with the list of artifacts. It will use the `dist-promotable` command on
the server to find these. You will be asked, for each promotable release,
whether you want to proceed. If there is more than one release to promote (there
shouldn't be), be sure to only promote the release you are responsible for.

**c.** Log in to the server via SSH and run the promote script for the given
release. The command on the server will be similar to: `dist-promote vx.y.z`.
After this step, the release artifacts will be available for download and a
`SHASUMS256.txt` file will be present. The release will still be unsigned,
however.

**d.** Use `scp` to download `SHASUMS256.txt` to a temporary directory on your
computer.

**e.** Sign the `SHASUMS256.txt` file using a command similar to: `gpg --default-key YOURKEY --digest-algo SHA256 --clearsign /path/to/SHASUMS256.txt`.
You will be prompted by GPG for your password. The signed file will be named
SHASUMS256.txt.asc.

**f.** Output an ASCII armored version of your public GPG key using a command
similar to: `gpg --default-key YOURKEY --digest-algo SHA256 --detach-sign /path/to/SHASUMS256.txt`.
You will be prompted by GPG for your password. The signed file will be named
SHASUMS256.txt.sig.

**g.** Upload the `SHASUMS256.txt` files back to the server into the release
directory.

</details>

**It is possible to only sign a release by running `./tools/release.sh -s
vX.Y.Z`.**

### 16. Check the release

Your release should be available at `https://nodejs.org/dist/vx.y.z/` and
<https://nodejs.org/dist/latest/>. Check that the appropriate files are in
place. You may want to check that the binaries are working as appropriate and
have the right internal version strings. Check that the API docs are available
at <https://nodejs.org/api/>. Check that the release catalog files are correct
at <https://nodejs.org/dist/index.tab> and <https://nodejs.org/dist/index.json>.

### 17. Create a blog post

There is an automatic build that is kicked off when you promote new builds, so
within a few minutes nodejs.org will be listing your new version as the latest
release. However, the blog post is not yet fully automatic.

Create a new blog post by running the [nodejs.org release-post.js script][]:

```bash
node ./apps/site/scripts/release-post/index.mjs x.y.z
```

This script will use the promoted builds and changelog to generate the post. Run
`npm run serve` to preview the post locally before pushing to the
[nodejs.org repository][].

* You can add a short blurb just under the main heading if you want to say
  something important, otherwise the text should be publication ready.

* The links to the download files won't be complete unless you waited for the
  ARMv6 builds. Any downloads that are missing will have `*Coming soon*` next to
  them. It's your responsibility to manually update these later when you have
  the outstanding builds.

* The `SHASUMS256.txt.asc` content is at the bottom of the post. When you update
  the list of tarballs you'll need to copy/paste the new contents of this file
  to reflect those changes.

* Always use pull-requests on the [nodejs.org repository][]. Be respectful
  of the website team, but you do not have to wait for PR sign-off. Please
  use the following commit message format:

  ```console
  Blog: vX.Y.Z release post

  Refs: <full URL to your release proposal PR>
  ```

* In order to trigger the CI Checks of the [nodejs.org repository][]; Please
  attach the `github_actions:pull-request` label to the PR.

* Changes to the base branch, `main`, on the [nodejs.org repository][] will
  trigger a new build of nodejs.org, so your changes should appear a few minutes
  after pushing. You can follow the [Deployments](https://github.com/nodejs/nodejs.org/deployments) page
  to see when the build finishes and gets published.

### 18. Create the release on GitHub

* Go to the [New release page](https://github.com/nodejs/node/releases/new).
* Select the tag version you pushed earlier.
* For release title, copy the title from the changelog.
* For the description, copy the rest of the changelog entry.
* If you are not releasing the latest "Current", uncheck
  "Set as the latest release".
* Click on the "Publish release" button.

### 19. Announce

The nodejs.org website will automatically rebuild and include the new version.
To announce the build on social media, please ping the @nodejs-social-team
on offical slack channel.

Node.js is also available on Bluesky and a release announcement can be
reposted using [nodejs/bluesky](https://github.com/nodejs/bluesky) repository.

The post content can be as simple as:

> v5.8.0 of @nodejs is out: <https://nodejs.org/en/blog/release/v5.8.0/>
> …
> something here about notable changes

You can create the PR for the release post on nodejs/bluesky with the following:

```bash
# Create a PR for a post:
gh workflow run create-pr.yml --repo "https://github.com/nodejs/bluesky" \
  -F prTitle='vx.x.x release announcement' \
  -F richText='Node.js vx.x.x is out. Check the blog post at https://nodejs.org/…. TL;DR is

- New feature
- …'

# Create a PR for a retweet:
gh workflow run create-pr.yml --repo "https://github.com/nodejs/bluesky" \
  -F prTitle='Retweet vx.x.x release announcement' -F postURL=…
```

<details>
<summary>Security release</summary>

Let the security release steward know the releases are available.

</details>

### 20. Celebrate

_In whatever form you do this..._

## LTS Releases

### Marking a release line as LTS

The process of marking a release line as LTS has been automated using
[`@node-core/utils`](https://github.com/nodejs/node-core-utils).

Start by checking out the staging branch for the release line that is going to
be marked as LTS, e.g:

```bash
git checkout v1.x-staging
```

Next, make sure you have **`@node-core/utils`** installed:

```bash
npm i -g @node-core/utils
```

Run the prepare LTS release command:

```bash
git node release --prepare --startLTS
```

<details>
<summary>Manual steps for reference.</summary>

To mark a release line as LTS, the following changes must be made to
`src/node_version.h`:

* The `NODE_MINOR_VERSION` macro must be incremented by one
* The `NODE_PATCH_VERSION` macro must be set to `0`
* The `NODE_VERSION_IS_LTS` macro must be set to `1`
* The `NODE_VERSION_LTS_CODENAME` macro must be set to the code name selected
  for the LTS release.

For example:

```diff
-#define NODE_MINOR_VERSION 12
-#define NODE_PATCH_VERSION 1
+#define NODE_MINOR_VERSION 13
+#define NODE_PATCH_VERSION 0

-#define NODE_VERSION_IS_LTS 0
-#define NODE_VERSION_LTS_CODENAME ""
+#define NODE_VERSION_IS_LTS 1
+#define NODE_VERSION_LTS_CODENAME "Erbium"

-#define NODE_VERSION_IS_RELEASE 0
+#define NODE_VERSION_IS_RELEASE 1
```

The changes must be made as part of a new semver-minor release.

Updating changelogs to properly reflect the changes between **Current** and
**Long Term Support** is also necessary, along with adding a reference to the
current LTS codename in its release line changelog file.

The `test/parallel/test-process-release.js` file might also need to be updated.

In case you can not run the automated `@node-core/utils` command and you are
currently running these steps manually it's a good idea to refer to previous
LTS proposal PRs and make sure all required changes are covered.

</details>

### Update release labels

The `lts-watch-vN.x` issue label must be created, with the same color as other
existing labels for that release line, such as `vN.x`.

If the release is transitioning from Active LTS to Maintenance, the
`backport-requested-vN.x` label must be deleted.

### Add new codename to nodejs-latest-linker

In order to make sure a download URL
(e.g: <https://nodejs.org/download/release/latest-codename/>) will be available
for the new LTS release line you need to submit a PR to
<https://github.com/nodejs/nodejs-latest-linker> and add a new entry for the
new LTS codename in its `ltsNames` map located in the `./common.js`
file.

Make sure to reach out to the Build WG in order to validate that the new URL is
available as part of the LTS release promotion.

### Update Release repo info

Add the new LTS codename to the release schedule table located in the
`./README.md` file located at the <https://github.com/nodejs/Release>
repository along with the addition of the new codename to the `./schedule.json`
file in that same repo.

## Major releases

The process for cutting a new Node.js major release has a number of differences
from cutting a minor or patch release.

### Schedule

New Node.js Major releases happen twice per year:

* Even-numbered releases are cut in April.
* Odd-numbered releases are cut in October.

Major releases should be targeted for the third Tuesday of the release month.

A major release must not slip beyond the release month. In other words, major
releases must not slip into May or November.

The @nodejs/releasers make a call for releasers 3 months in advance.
Currently, this call is automated in the `#nodejs-release-private`
Slack channel.

The release date for the next major release should be announced immediately
following the current release (e.g. the release date for 13.0.0 should be
announced immediately following the release of 12.0.0).

### Release branch

Approximately two months before a major release, new `vN.x` and
`vN.x-staging` branches (where `N` indicates the major release) should be
created as forks of the `main` branch. Up until the cut-off date announced by
the releaser, these must be kept in sync with `main`.

The `vN.x` and `vN.x-staging` branches must be kept in sync with one another
up until the date of the release.

If a `SEMVER-MAJOR` pull request lands on the default branch within one month
prior to the major release date, it must not be included on the new major
staging branch, unless there is consensus from the Node.js releasers team to
do so. This measure aims to ensure better stability for the release candidate
(RC) phase, which begins approximately two weeks prior to the official release.
By restricting `SEMVER-MAJOR` commits in this period, we provide more time for
thorough testing and reduce the potential for major breakages, especially in
LTS lines.

### Create release labels

The following issue labels must be created:

* `vN.x`
* `backport-blocked-vN.x`
* `backport-open-vN.x`
* `backport-requested-vN.x`
* `backported-to-vN.x`
* `dont-land-on-vN.x`

The label description can be copied from existing labels of previous releases.
The label color must be the same for all new labels, but different from the
labels of previous releases.

### Release proposal

A draft release proposal should be created 6 weeks before the release. A
separate `vN.x-proposal` branch should be created that tracks the `vN.x`
branch. This branch will contain the draft release commit (with the draft
changelog).

Notify the `@nodejs/npm` team in the release proposal PR to inform them of the
upcoming release. `npm` maintains a list of [supported versions](https://github.com/npm/cli/blob/latest/lib/utils/unsupported.js#L3)
that will need updating to include the new major release.

To keep the branch in sync until the release date, it can be as simple as
doing the following:

> Make sure to check that there are no PRs with the label `dont-land-on-vX.x`.

```bash
git checkout vN.x
git reset --hard upstream/main
git checkout vN.x-staging
git reset --hard upstream/main
git push upstream vN.x
git push upstream vN.x-staging
```

### Update `NODE_MODULE_VERSION`

This macro in `src/node_version.h` is used to signal an ABI version for native
addons. It currently has two common uses in the community:

* Determining what API to work against for compiling native addons, e.g.
  [NAN](https://github.com/nodejs/nan) uses it to form a compatibility-layer for
  much of what it wraps.
* Determining the ABI for downloading pre-built binaries of native addons, e.g.
  [node-pre-gyp](https://github.com/mapbox/node-pre-gyp) uses this value as
  exposed via `process.versions.modules` to help determine the appropriate
  binary to download at install-time.

The general rule is to bump this version when there are _breaking ABI_ changes
and also if there are non-trivial API changes. The rules are not yet strictly
defined, so if in doubt, please confer with someone that will have a more
informed perspective, such as a member of the NAN team.

A registry of currently used `NODE_MODULE_VERSION` values is maintained at
<https://github.com/nodejs/node/blob/HEAD/doc/abi_version_registry.json>.
When bumping `NODE_MODULE_VERSION`, you should choose a new value not listed
in the registry. Also include a change to the registry in your commit to
reflect the newly used value. Ensure that the release commit removes the
`-pre` suffix for the major version being prepared.

It is current TSC policy to bump major version when ABI changes. If you
see a need to bump `NODE_MODULE_VERSION` outside of a major release then
you should consult the TSC. Commits may need to be reverted or a major
version bump may need to happen.

### Test releases and release candidates

Test builds should be generated from the `vN.x-proposal` branch starting at
about 6 weeks before the release.

Release Candidates should be generated from the `vN.x-proposal` branch starting
at about 4 weeks before the release, with a target of one release candidate
per week.

Always run test releases and release candidates through the Canary in the
Goldmine tool for additional testing.

### Changelogs

Generating major release changelogs is a bit more involved than minor and patch
changelogs.

#### Create the changelog file

In the `doc/changelogs` directory, create a new `CHANGELOG_V{N}.md` file where
`{N}` is the major version of the release. Follow the structure of the existing
`CHANGELOG_V*.md` files.

The navigation headers in all of the `CHANGELOG_V*.md` files must be
updated to account for the new `CHANGELOG_V{N}.md` file.

Once the file is created, the root `CHANGELOG.md` file must be updated to
reference the newly-created major release `CHANGELOG_V{N}.md`.

#### Generate the changelog

To generate a proper major release changelog, use the `branch-diff` tool to
compare the `vN.x` branch against the `vN-1.x` branch (e.g. for Node.js 12.0,
we compare the `v12.x` branch against the up to date `v11.x` branch). Make sure
that the local copy of the downlevel branch is up to date.

The commits in the generated changelog must then be organized:

* Remove all release commits from the list
* Remove all reverted commits and their reverts
* Separate all SEMVER-MAJOR, SEMVER-MINOR, and SEMVER-PATCH commits into lists

```console
$ branch-diff upstream/vN-1.x upstream/vN.x --require-label=semver-major --group --filter-release  # get all majors
$ branch-diff upstream/vN-1.x upstream/vN.x --require-label=semver-minor --group --filter-release  # get all minors
$ branch-diff upstream/vN-1.x upstream/vN.x --exclude-label=semver-major,semver-minor --group --filter-release  # get all patches
```

#### Generate the notable changes

For a major release, all SEMVER-MAJOR commits that are not strictly internal,
test, or doc-related are to be listed as notable changes. Some SEMVER-MINOR
commits may be listed as notable changes on a case-by-case basis. Use your
judgment there.

### Update the expected assets

The promotion script does a basic check that the expected files are present.
Open a pull request in the Build repository to add the list of expected files
for the new release line as a new file, `v{N}.x` (where `{N}` is the major
version of the release), in the [expected assets][] folder. The change will
need to be deployed onto the web server by a member of the [build-infra team][]
before the release is promoted.

### Snap

The Node.js [Snap][] package has a "default" for installs where the user hasn't
specified a release line ("track" in Snap terminology). This should be updated
to point to the most recently activated LTS. A member of the Node.js Build
Infrastructure team is able to perform the switch of the default. An issue
should be opened on the [Node.js Snap management repository][] requesting this
take place once a new LTS line has been released.

## FAQ

Due to how `tools/release.sh` work, it isn't uncommon to face some errors
during the promotion process as it depends on network communication and machine
availability. This section aims to guide the releaser through potential
failures.

### Error on dist-indexer while promoting

```bash
node:events:491
      throw er; // Unhandled 'error' event
      ^

Error: read ECONNRESET
    at TLSWrap.onStreamRead (node:internal/stream_base_commons:217:20)
Emitted 'error' event on DestroyableTransform instance at:
    at ClientRequest.<anonymous> (/usr/lib/node_modules/nodejs-dist-indexer/node_modules/hyperquest/index.js:14:19)
    at ClientRequest.emit (node:events:513:28)
    at TLSSocket.socketErrorListener (node:_http_client:494:9)
    at TLSSocket.emit (node:events:513:28)
    at emitErrorNT (node:internal/streams/destroy:157:8)
    at emitErrorCloseNT (node:internal/streams/destroy:122:3)
    at processTicksAndRejections (node:internal/process/task_queues:83:21) {
  errno: -104,
  code: 'ECONNRESET',
  syscall: 'read'
}
```

Typical resolution: sign the release again.

```bash
./tools/release.sh -s vX.Y.Z
```

[Build issue tracker]: https://github.com/nodejs/build/issues/new
[CI lockdown procedure]: https://github.com/nodejs/build/blob/HEAD/doc/jenkins-guide.md#restricting-access-for-security-releases
[Node.js Snap management repository]: https://github.com/nodejs/snap
[Snap]: https://snapcraft.io/node
[`create-release-proposal`]: https://github.com/nodejs/node/actions/workflows/create-release-proposal.yml
[build-infra team]: https://github.com/orgs/nodejs/teams/build-infra
[expected assets]: https://github.com/nodejs/build/tree/HEAD/ansible/www-standalone/tools/promote/expected_assets
[nodejs.org release-post.js script]: https://github.com/nodejs/nodejs.org/blob/HEAD/apps/site/scripts/release-post/index.mjs
[nodejs.org repository]: https://github.com/nodejs/nodejs.org
