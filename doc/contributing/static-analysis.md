# Static Analysis

The project uses Coverity to scan Node.js source code and to report potential
issues in the C/C++ code base.

Those who have been added to the [Node.js coverity project][] can receive emails
when there are new issues reported as well as view all current issues
through <https://scan9.scan.coverity.com/reports.htm>.

Any collaborator can ask to be added to the Node.js coverity project
by opening an issue in the [build][] repository titled
`Please add me to coverity`. A member of the build WG with admin access will
verify that the requester is an existing collaborator as listed in the
[collaborators section][] on the nodejs/node project repo. Once validated the
requester will be added to the coverity project.

[Node.js coverity project]: https://scan.coverity.com/projects/node-js
[build]: https://github.com/nodejs/build
[collaborators section]: https://github.com/nodejs/node#collaborators
