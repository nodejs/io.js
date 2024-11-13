# Issues

* [Asking for general help](#asking-for-general-help)
* [Discussing non-technical topics](#discussing-non-technical-topics)
* [Submitting a bug report](#submitting-a-bug-report)
* [Triaging a bug report](#triaging-a-bug-report)

## Asking for general help

Because the level of activity in the `nodejs/node` repository is so high,
questions or requests for general help using Node.js should be directed at
the [Node.js help repository][].

## Discussing non-technical topics

Discussion of non-technical topics (such as intellectual property and trademark)
should be directed to the [Technical Steering Committee (TSC) repository][].

## Submitting a bug report

When opening a new issue in the `nodejs/node` issue tracker, users will be
presented with a choice of issue templates. If you believe that you have
uncovered a bug in Node.js, please fill out the `Bug Report` template to the
best of your ability. Do not worry if you cannot answer every detail; just fill
in what you can.

The two most important pieces of information we need in order to properly
evaluate the report is a description of the behavior you are seeing and a simple
test case we can use to recreate the problem on our own. If we cannot recreate
the issue, it becomes impossible for us to fix.

In order to rule out the possibility of bugs introduced by userland code, test
cases should be limited, as much as possible, to using _only_ Node.js APIs.
If the bug occurs only when you're using a specific userland module, there is
a very good chance that either (a) the module has a bug or (b) something in
Node.js changed that broke the module.

See [How to create a Minimal, Complete, and Verifiable example](https://stackoverflow.com/help/mcve).

## Triaging a bug report

Once an issue has been opened, it is common for there to be discussion
around it. Some contributors may have differing opinions about the issue,
including whether the behavior being seen is a bug or a feature. This discussion
is part of the process and should be kept focused, helpful, and professional.

The objective of helping with triaging issues (in core and help repos) is to
help reduce the issue backlog and keep the issue tracker healthy, while enabling
newcomers another meaningful way to get engaged and contribute.

Anyone with a reasonable understanding of Node.js programming and the
project's GitHub organization plus a few contributions to the project
(commenting on issues or PRs) can apply for and become a triager. Open a PR
on the README.md of this project with: i) a request to be added as a triager,
ii) the motivation for becoming a triager, and iii) agreement on reading,
understanding, and adhering to the project's [Code Of Conduct](https://github.com/nodejs/admin/blob/HEAD/CODE_OF_CONDUCT.md).

The triage role enables the ability to carry out the most common triage
activities, such as applying labels and closing/reopening/assigning issues.
For more information on the roles and permissions, see ["Permission levels for
repositories owned by an organization"](https://docs.github.com/en/github/setting-up-and-managing-organizations-and-teams/repository-permission-levels-for-an-organization#permission-levels-for-repositories-owned-by-an-organization).

When triagging issues and PRs:

* Show patience and empathy, especially to first-time contributors.
* Show no patience towards spam or troll, close the issue without interacting with it and
  report the user to the moderation repository.
* If you're not able to reproduce an issue, leave a comment asking for more info and
  add the `needs more info` label.
* Ideally issues should be closed only when they have been fixed or answered (and
  merged for pull requests). Closing an issue (or PR) earlier can be seen as
  dismissive from the point of view of the reporter/author.
  Always try to communicate the reason for closing the issue/PR.

[Node.js help repository]: https://github.com/nodejs/help/issues
[Technical Steering Committee (TSC) repository]: https://github.com/nodejs/TSC/issues
