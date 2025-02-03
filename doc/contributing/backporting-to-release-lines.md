# How to backport a pull request to a release line

## Staging branches

Each release line has a staging branch that serves as a workspace for preparing releases.
The branch format is `vN.x-staging`, where `N` is the major release number.

For active staging branches, refer to the [Release Schedule][].

## Identifying changes that require a backport

If a cherry-pick from `main` doesn't apply cleanly on a staging branch, the pull request
will be labeled for the release line (e.g., `backport-requested-vN.x`). This indicates
that manual backporting is required.

## Criteria for backporting

The "Current" release line is more flexible than LTS lines. LTS branches, detailed in the [Release Plan][],
require commits to mature in the Current release for at least two weeks before backporting.

## Labeling backport issues and PRs

Use the following labels, with `N` in `vN.x` denoting the major release number:

| Label                   | Description                                                         |
| ----------------------- | ------------------------------------------------------------------- |
| backport-blocked-vN.x   | PRs for `vN.x-staging` blocked by pending backports from other PRs. |
| backport-open-vN.x      | Indicates an open backport for the PR.                              |
| backport-requested-vN.x | PR awaiting manual backport to `vN.x-staging`.                      |
| backported-to-vN.x      | PR successfully backported to `vN.x-staging`.                       |
| baking-for-lts          | PRs awaiting LTS release after maturation in Current.               |
| lts-watch-vN.x          | PRs possibly included in `vN.x` LTS releases.                       |
| vN.x                    | Issues or PRs impacting `vN.x-staging` (or reproducible on vN.x).   |

## Submitting a backport pull request

For the following steps, let's assume that you need to backport PR `123`
to the vN.x release line. All commands will use the `vN.x-staging` branch
as the target branch. In order to submit a backport pull request to another
branch, simply replace `N` with the version number for the targeted release
line.

### Automated process

1. Ensure [`@node-core/utils`][] is installed.

2. Execute [`git node backport`][] command:

   ```bash
   # Example: Backport PR 123 to vN.x-staging
   git node backport 123 --to=N
   ```

3. Proceed to step 5 in the Manual section below.

### Manual process

1. Checkout the `vN.x-staging` branch.

2. Verify that the local staging branch is up to date with the remote.

3. Create a new branch based on `vN.x-staging`:

   ```bash
   git fetch upstream vN.x-staging:vN.x-staging -f
   git checkout -b backport-123-to-vN.x vN.x-staging
   ```

4. Cherry-pick the desired commit(s):

   ```bash
   git cherry-pick <commit hash>
   ```

5. Resolve conflicts using `git add` and `git cherry-pick --continue`.

6. Leave the commit message as is. If you think it should be modified, comment
   in the pull request. The `Backport-PR-URL` metadata does need to be added to
   the commit, but this will be done later.

7. Verify that `make -j4 test` passes.

8. Push the changes to your fork.

9. Open a pull request:

   * Target `vN.x-staging`.
   * Title format: `[vN.x backport] <commit title>` (e.g., `[v20.x backport] process: improve performance of nextTick`).
   * Reference the original PR in the description.

10. Update the `backport-requested-vN.x` label on the original pull request to `backport-open-vN.x`.

11. If conflicts arise during the review process, the following command be used to rebase:

    ```bash
    git pull --rebase upstream vN.x-staging
    ```

Once merged, update the original PR's label from `backport-open-vN.x` to `backported-to-vN.x`.

[Release Plan]: https://github.com/nodejs/Release#release-plan
[Release Schedule]: https://github.com/nodejs/Release#release-schedule
[`@node-core/utils`]: https://github.com/nodejs/node-core-utils
[`git node backport`]: https://github.com/nodejs/node-core-utils/blob/main/docs/git-node.md#git-node-backport
