# Feature request management

Feature requests are a valuable source of input to the project.
They help our maintainers understand what additions will be of
value to users of the Node.js runtime.

At the same time, the project is volunteer run and does not
have the ability to direct resources toward specific work. The
features which are implemented are those for which volunteers
are individually motivated to work on. The best way to ensure
a feature gets implemented is to create a PR to add it.
The project strives to support people who do that.

An open feature request does not provide any indication that work
on that feature will take place and after some period of time
may be detrimental as it may result in an expectation that will
never be fulfilled.

This process tries to balance retaining the valuable input
we get through feature requests and the overhead of
maintaining open feature requests that will never get
implemented.

## Creating feature requests

A feature request can be created by adding the `feature request`
label to an issue. This may be done automatically when the issue
is opened or at a later point (often when investigation of a bug
report results in it being considered a feature request as opposed
to a bug).

The current list of feature requests can be found through the
[is:issue is:open label:"feature request"](https://github.com/nodejs/node/issues?q=is%3Aissue+is%3Aopen+label%3A%22feature+request%22)
query.

## Triage of feature requests

There is no set process for triaging/handling feature requests.
Individual collaborators review issues marked as `feature request`
along with other issues and may or may not decide to
work on an implementation or advocate on their behalf.

If a collaborator believes a feature request must be implemented
they can add the `never-stale` label to the issue and it will
be excluded from the automated feature request handling
as outlined below.

## Expressing support for a feature request

If you come across a feature request and want to add your
support for that feature please express your support
with the thumbs up emoji as a reaction. At some point in the
future we may use this as additional input in the automated
handling of feature requests.

## Automated feature request handling

Our experience is that most feature requests that are
likely to be addressed, will be addressed within the first
6 months after they are submitted.

Once there has been no activity on a feature request for
5 months, the following comment will be added
to the issue:

```markdown
There has been no activity on this feature request for
5 months and it is unlikely to be implemented.
It will be closed 6 months after the last non-automated comment.

For more information on how the project manages
feature requests, please consult the
[feature request management document](https://github.com/nodejs/node/blob/HEAD/doc/contributing/feature-request-management.md).
```

If there is no additional activity/discussion on the
feature request in the next month, the following
comment is added to the issue and the issue will be
closed:

```markdown
There has been no activity on this feature request
and it is being closed. If you feel closing this issue is not the
right thing to do, please leave a comment.

For more information on how the project manages
feature requests, please consult the
[feature request management document](https://github.com/nodejs/node/blob/HEAD/doc/contributing/feature-request-management.md).
```
