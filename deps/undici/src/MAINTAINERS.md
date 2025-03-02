# Maintainers

This document details any and all processes relevant to project maintainers. Maintainers should feel empowered to contribute back to this document with any process changes they feel improve the overall experience for themselves and other maintainers.

## Labels

Maintainers are encouraged to use the extensive and detailed list of labels for easier repo management.

* Generally, all issues should be labelled. The most general labels are `bug`, `enhancement`, and `Status: help-wanted`.
* Issues specific to a certain aspect of the project should be labeled using one of the specificity labels listed below. For example, a bug in the `Client` class should have the `Client` and `bug` label assigned.
  * Specificity labels:
    * `Agent`
    * `Client`
    * `Docs`
    * `Performance`
    * `Pool`
    * `Tests`
    * `Types`
* Any `question` or `usage help` issues should be converted into Q&A Discussions
* `Status:` labels should be added to all open issues indicating their relative development status.
  * Status labels:
    * `Status: blocked`
    * `Status: help-wanted`
    * `Status: in-progress`
    * `Status: wontfix`
* Issues and/or pull requests with an agreed upon semver status can be assigned the appropriate `semver-` label.
  * Semver labels:
    * `semver-major`
    * `semver-minor`
    * `semver-patch`
* Issues with a low-barrier of entry should be assigned the `good first issue` label.
* Do not use the `invalid` label, instead use `bug` or `Status: wontfix`.
* Duplicate issues should initially be assigned the `duplicate` label.


## Making a Release

1. Go to github actions, then select ["Create Release PR"](https://github.com/nodejs/undici/actions/workflows/release-create-pr.yml).
2. Run the workflow, selecting `main` and indicating if you want a specific version number or a patch/minor/major release
3. Wait for the PR to be created. Approve the PR ([this](https://github.com/nodejs/undici/pull/4021) is a an example).
4. Land the PR, wait for the CI to pass.
5. Got to the ["Release"](https://github.com/nodejs/undici/actions/workflows/release.yml) workflow, you should see a job waiting.
6. If you are one of the [releases](https://github.com/nodejs/undici?tab=readme-ov-file#releasers), then click "review deployments", then select "release" and click "approve and deploy". If you are not a releaser, contact one.
