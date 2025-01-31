# Security

## Reporting a bug in Node.js

Report security bugs in Node.js via [HackerOne](https://hackerone.com/nodejs).

Normally, your report will be acknowledged within 5 days, and you'll receive
a more detailed response to your report within 10 days indicating the
next steps in handling your submission. These timelines may extend when
our triage volunteers are away on holiday, particularly at the end of the
year.

After the initial reply to your report, the security team will endeavor to keep
you informed of the progress being made towards a fix and full announcement,
and may ask for additional information or guidance surrounding the reported
issue.

### Node.js bug bounty program

The Node.js project engages in an official bug bounty program for security
researchers and responsible public disclosures.  The program is managed through
the HackerOne platform. See <https://hackerone.com/nodejs> for further details.

## Reporting a bug in a third-party module

Security bugs in third-party modules should be reported to their respective
maintainers.

## Disclosure policy

Here is the security disclosure policy for Node.js

* The security report is received and is assigned a primary handler. This
  person will coordinate the fix and release process. The problem is validated
  against all supported Node.js versions. Once confirmed, a list of all affected
  versions is determined. Code is audited to find any potential similar
  problems. Fixes are prepared for all supported releases.
  These fixes are not committed to the public repository but rather held locally
  pending the announcement.

* A suggested embargo date for this vulnerability is chosen and a CVE (Common
  Vulnerabilities and Exposures (CVE®)) is requested for the vulnerability.

* On the embargo date, a copy of the announcement is sent to the Node.js
  security mailing list. The changes are pushed to the public repository and new
  builds are deployed to nodejs.org. Within 6 hours of the mailing list being
  notified, a copy of the advisory will be published on the Node.js blog.

* Typically, the embargo date will be set 72 hours from the time the CVE is
  issued. However, this may vary depending on the severity of the bug or
  difficulty in applying a fix.

* This process can take some time, especially when we need to coordinate with
  maintainers of other projects. We will try to handle the bug as quickly as
  possible; however, we must follow the release process above to ensure that we
  handle disclosure consistently.

## The Node.js threat model

In the Node.js threat model, there are trusted elements such as the
underlying operating system. Vulnerabilities that require the compromise
of these trusted elements are outside the scope of the Node.js threat
model.

For a vulnerability to be eligible for a bug bounty, it must be a
vulnerability in the context of the Node.js threat model. In other
words, it cannot assume that a trusted element (such as the operating
system) has been compromised.

Being able to cause the following through control of the elements that Node.js
does not trust is considered a vulnerability:

* Disclosure or loss of integrity or confidentiality of data protected through
  the correct use of Node.js APIs.
* The unavailability of the runtime, including the unbounded degradation of its
  performance.

If Node.js loads configuration files or runs code by default (without a
specific request from the user), and this is not documented, it is considered a
vulnerability.
Vulnerabilities related to this case may be fixed by a documentation update.

**Node.js does NOT trust**:

* Data received from the remote end of inbound network connections
  that are accepted through the use of Node.js APIs and
  which is transformed/validated by Node.js before being passed
  to the application. This includes:
  * HTTP APIs (all flavors) server APIs.
* The data received from the remote end of outbound network connections
  that are created through the use of Node.js APIs and
  which is transformed/validated by Node.js before being passed
  to the application EXCEPT with respect to payload length. Node.js trusts
  that applications make connections/requests which will avoid payload
  sizes that will result in a Denial of Service.
  * HTTP APIs (all flavors) client APIs.
  * DNS APIs.
* Consumers of data protected through the use of Node.js APIs (for example,
  people who have access to data encrypted through the Node.js crypto APIs).
* The file content or other I/O that is opened for reading or writing by the
  use of Node.js APIs (ex: stdin, stdout, stderr).

In other words, if the data passing through Node.js to/from the application
can trigger actions other than those documented for the APIs, there is likely
a security vulnerability. Examples of unwanted actions are polluting globals,
causing an unrecoverable crash, or any other unexpected side effects that can
lead to a loss of confidentiality, integrity, or availability.

**Node.js trusts everything else**. Examples include:

* The developers and infrastructure that runs it.
* The operating system that Node.js is running under and its configuration,
  along with anything under control of the operating system.
* The code it is asked to run, including JavaScript, WASM and native code, even
  if said code is dynamically loaded, e.g., all dependencies installed from the
  npm registry.
  The code run inherits all the privileges of the execution user.
* Inputs provided to it by the code it is asked to run, as it is the
  responsibility of the application to perform the required input validations,
  e.g. the input to `JSON.parse()`.
* Any connection used for inspector (debugger protocol) regardless of being
  opened by command line options or Node.js APIs, and regardless of the remote
  end being on the local machine or remote.
* The file system when requiring a module.
  See <https://nodejs.org/api/modules.html#all-together>.
* The `node:wasi` module does not currently provide the comprehensive file
  system security properties provided by some WASI runtimes.

Any unexpected behavior from the data manipulation from Node.js Internal
functions may be considered a vulnerability if they are exploitable via
untrusted resources.

In addition to addressing vulnerabilities based on the above, the project works
to avoid APIs and internal implementations that make it "easy" for application
code to use the APIs incorrectly in a way that results in vulnerabilities within
the application code itself. While we don’t consider those vulnerabilities in
Node.js itself and will not necessarily issue a CVE, we do want them to be
reported privately to Node.js first.
We often choose to work to improve our APIs based on those reports and issue
fixes either in regular or security releases depending on how much of a risk to
the community they pose.

### Examples of vulnerabilities

#### Improper Certificate Validation (CWE-295)

* Node.js provides APIs to validate handling of Subject Alternative Names (SANs)
  in certificates used to connect to a TLS/SSL endpoint. If certificates can be
  crafted which result in incorrect validation by the Node.js APIs that is
  considered a vulnerability.

#### Inconsistent Interpretation of HTTP Requests (CWE-444)

* Node.js provides APIs to accept http connections. Those APIs parse the
  headers received for a connection and pass them on to the application.
  Bugs in parsing those headers which can result in request smuggling are
  considered vulnerabilities.

#### Missing Cryptographic Step (CWE-325)

* Node.js provides APIs to encrypt data. Bugs that would allow an attacker
  to get the original data without requiring the decryption key are
  considered vulnerabilities.

#### External Control of System or Configuration Setting (CWE-15)

* If Node.js automatically loads a configuration file which is not documented
  and modification of that configuration can affect the confidentiality of
  data protected using the Node.js APIs this is considered a vulnerability.

### Examples of non-vulnerabilities

#### Malicious Third-Party Modules (CWE-1357)

* Code is trusted by Node.js. Therefore any scenario that requires a malicious
  third-party module cannot result in a vulnerability in Node.js.

#### Prototype Pollution Attacks (CWE-1321)

* Node.js trusts the inputs provided to it by application code.
  It is up to the application to sanitize appropriately. Therefore any scenario
  that requires control over user input is not considered a vulnerability.

#### Uncontrolled Search Path Element (CWE-427)

* Node.js trusts the file system in the environment accessible to it.
  Therefore, it is not a vulnerability if it accesses/loads files from any path
  that is accessible to it.

#### External Control of System or Configuration Setting (CWE-15)

* If Node.js automatically loads a configuration file which is documented
  no scenario that requires modification of that configuration file is
  considered a vulnerability.

#### Uncontrolled Resource Consumption (CWE-400) on outbound connections

* If Node.js is asked to connect to a remote site and return an
  artifact, it is not considered a vulnerability if the size of
  that artifact is large enough to impact performance or
  cause the runtime to run out of resources.

#### Vulnerabilities affecting software downloaded by Corepack

* Corepack defaults to downloading the latest version of the software requested
  by the user, or a specific version requested by the user. For this reason,
  Node.js releases won't be affected by such vulnerabilities. Users are
  responsible for keeping the software they use through Corepack up-to-date.

## Assessing experimental features reports

Experimental features are eligible to reports as any other stable feature of
Node.js. They will also be susceptible to receiving the same severity score
as any other stable feature.

## Receiving security updates

Security notifications will be distributed via the following methods.

* <https://groups.google.com/group/nodejs-sec>
* <https://nodejs.org/en/blog/vulnerability>

## Comments on this policy

If you have suggestions on how this process could be improved, please visit
the [nodejs/security-wg](https://github.com/nodejs/security-wg)
repository.
