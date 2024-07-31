# Security Model Strategy

A clear security model, with features like permissions and policy enforcement,
is a
[top technical priority](https://github.com/nodejs/node/blob/HEAD/doc/contributing/technical-priorities.md#permissionspoliciessecurity-model)
of Node.js.

## High-level approach

* Document the security model
* Document threat models and current state of the art
* Support experimentation on features like permissions and policies
* Add a security component in Node.js certification covering
  the Node.js security model

### Document the security model

The current security model for Node.js is not yet well documented.
At a high level it is:

* Node.js does not provide a sandbox, both the JavaScript and
  native code which is run is trusted to not be malicious.
* The project works to help code running on top of Node.js to avoid
  making mistakes, but not doing so is not considered a
  vulnerability in Node.js. Just because you can build something
  vulnerable with the APIs does not mean there is a vulnerability
  in Node.js itself.

The project has a goal to better document the security model
and this section will be expanded when that happens.

Once the security model is documented the project will work
to add a security component in Node.js certification covering
the Node.js security model.

### Document threat models and current state of the art

Node.js is used in several different use cases and the
threats may be different in each use case. The project
should document the threat models and use that to
help define the security model in the context of each
of these use cases.

This section will be expanded as the use case/threat
models are defined. The initial list includes:

* Server
* Desktop application
* Cli
* Single executable application
* CI/CD pipeline components

### Support experimentation on features like permissions

The project is not currently planning to provide supported
sandbox functionality, but wants to support experimentation on
related features like permission enforcement.

Features in this category should:

* be opt-in, and additional overhead when not enabled must be low
* limit change in core to just what is needed to enable experimentation
