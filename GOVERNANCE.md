# Node.js Project Governance

<!-- TOC -->

* [Triagers](#triagers)
* [Collaborators](#collaborators)
  * [Collaborator activities](#collaborator-activities)
* [Technical steering committee](#technical-steering-committee)
  * [TSC meetings](#tsc-meetings)
* [Collaborator nominations](#collaborator-nominations)
  * [Who can nominate Collaborators?](#who-can-nominate-collaborators)
  * [Ideal Nominees](#ideal-nominees)
  * [Nominating a new Collaborator](#nominating-a-new-collaborator)
  * [Onboarding](#onboarding)
* [Consensus seeking process](#consensus-seeking-process)

<!-- /TOC -->

## Triagers

Triagers assess newly-opened issues in the [nodejs/node][] and [nodejs/help][]
repositories. The GitHub team for Node.js triagers is @nodejs/issue-triage.
Triagers are given the "Triage" GitHub role and have:

* Ability to label issues and pull requests
* Ability to comment, close, and reopen issues and pull requests

See:

* [List of triagers](./README.md#triagers)
* [A guide for triagers](./doc/contributing/issues.md#triaging-a-bug-report)

## Collaborators

Node.js core collaborators maintain the [nodejs/node][] GitHub repository.
The GitHub team for Node.js core collaborators is @nodejs/collaborators.
Collaborators have:

* Commit access to the [nodejs/node][] repository
* Access to the Node.js continuous integration (CI) jobs

Both collaborators and non-collaborators may propose changes to the Node.js
source code. The mechanism to propose such a change is a GitHub pull request.
Collaborators review and merge (_land_) pull requests.

Two collaborators must approve a pull request before the pull request can land.
(One collaborator approval is enough if the pull request has been open for more
than 7 days.) Approving a pull request indicates that the collaborator accepts
responsibility for the change. Approval must be from collaborators who are not
authors of the change.

If a collaborator opposes a proposed change, then the change cannot land. The
exception is if the TSC votes to approve the change despite the opposition.
Usually, involving the TSC is unnecessary. Often, discussions or further changes
result in collaborators removing their opposition.

See:

* [List of collaborators](./README.md#current-project-team-members)
* [A guide for collaborators](./doc/contributing/collaborator-guide.md)

### Collaborator activities

* Helping users and novice contributors
* Contributing code and documentation changes that improve the project
* Reviewing and commenting on issues and pull requests
* Participation in working groups
* Merging pull requests

The TSC can remove inactive collaborators or provide them with _emeritus_
status. Emeriti may request that the TSC restore them to active status.

A collaborator is automatically made emeritus (and removed from active
collaborator status) if it has been more than 12 months since the collaborator
has authored or approved a commit that has landed.

## Technical Steering Committee

A subset of the collaborators forms the Technical Steering Committee (TSC).
The TSC has final authority over this project, including:

* Technical direction
* Project governance and process (including this policy)
* Contribution policy
* GitHub repository hosting
* Conduct guidelines
* Maintaining the list of collaborators

The current list of TSC members is in
[the project README](./README.md#current-project-team-members).

The [TSC Charter][] governs the operations of the TSC. All changes to the
Charter need approval by the OpenJS Foundation Cross-Project Council (CPC).

### TSC meetings

The TSC meets in a video conference call. Each year, the TSC elects a chair to
run the meetings. The TSC streams its meetings for public viewing on YouTube.

The TSC agenda includes issues that are at an impasse. The intention of the
agenda is not to review or approve all patches. Collaborators review and approve
patches on GitHub.

Any community member can create a GitHub issue asking that the TSC review
something. If consensus-seeking fails for an issue, a collaborator may apply the
`tsc-agenda` label. That will add it to the TSC meeting agenda.

Before each TSC meeting, the meeting chair will share the agenda with members of
the TSC. TSC members can also add items to the agenda at the beginning of each
meeting. The meeting chair and the TSC cannot veto or remove items.

The TSC may invite people to take part in a non-voting capacity.

During the meeting, the TSC chair ensures that someone takes minutes. After the
meeting, the TSC chair ensures that someone opens a pull request with the
minutes.

The TSC seeks to resolve as many issues as possible outside meetings using
[the TSC issue tracker](https://github.com/nodejs/TSC/issues). The process in
the issue tracker is:

* A TSC member opens an issue explaining the proposal/issue and @-mentions
  @nodejs/tsc.
* The proposal passes if, after 72 hours, there are two or more TSC voting
  member approvals and no TSC voting member opposition.
* If there is an extended impasse, a TSC member may make a motion for a vote.

## Collaborator nominations

### Who can nominate Collaborators?

Existing Collaborators can nominate someone to become a Collaborator.

### Ideal Nominees

Nominees should have significant and valuable contributions across the Node.js
organization.

Contributions can be:

* Opening pull requests.
* Comments and reviews.
* Opening new issues.
* Participation in other projects, teams, and working groups of the Node.js
  organization.

### Nominating a new Collaborator

To nominate a new Collaborator, open an issue in the [nodejs/node][] repository.
Provide a summary of the nominee's contributions. For example:

* Commits in the [nodejs/node][] repository.
  * Use the link `https://github.com/nodejs/node/commits?author=GITHUB_ID`
* Pull requests and issues opened in the [nodejs/node][] repository.
  * Use the link `https://github.com/nodejs/node/issues?q=author:GITHUB_ID`
* Comments on pull requests and issues in the [nodejs/node][] repository
  * Use the link `https://github.com/nodejs/node/issues?q=commenter:GITHUB_ID`
* Reviews on pull requests in the [nodejs/node][] repository
  * Use the link `https://github.com/nodejs/node/pulls?q=reviewed-by:GITHUB_ID`
* Help provided to end-users and novice contributors
* Pull requests and issues opened throughout the Node.js organization
  * Use the link  `https://github.com/search?q=author:GITHUB_ID+org:nodejs`
* Comments on pull requests and issues throughout the Node.js organization
  * Use the link `https://github.com/search?q=commenter:GITHUB_ID+org:nodejs`
* Participation in other projects, teams, and working groups of the Node.js
  organization
* Other participation in the wider Node.js community

Mention @nodejs/collaborators in the issue to notify other collaborators about
the nomination.

The nomination passes if no collaborators oppose it after one week. In the case
of an objection, the TSC is responsible for working with the individuals
involved and finding a resolution.

There are steps a nominator can take in advance to make a nomination as
frictionless as possible. To request feedback from other collaborators in
private, use the [collaborators discussion page][]
(which only collaborators may view). A nominator may also work with the
nominee to improve their contribution profile.

Collaborators might overlook someone with valuable contributions. In that case,
the contributor may open an issue or contact a collaborator to request a
nomination.

### Onboarding

After the nomination passes, a TSC member onboards the new collaborator. See
[the onboarding guide](./onboarding.md) for details of the onboarding
process.

## Consensus seeking process

The TSC follows a [Consensus Seeking][] decision-making model per the
[TSC Charter][].

[Consensus Seeking]: https://en.wikipedia.org/wiki/Consensus-seeking_decision-making
[TSC Charter]: https://github.com/nodejs/TSC/blob/HEAD/TSC-Charter.md
[collaborators discussion page]: https://github.com/nodejs/collaborators/discussions/categories/collaborator-nominations
[nodejs/help]: https://github.com/nodejs/help
[nodejs/node]: https://github.com/nodejs/node
