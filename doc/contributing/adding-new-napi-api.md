# Contributing a new API to Node-API

Node-API is the ABI-stable API for native addons. We encourage contributions to enhance the API,
while also ensuring compatibility and adherence to guidelines. When adding a new API to Node-API,
please follow these principles and guidelines:

## Core principles

1. **Adherence to Node-API standards**
   * **Must** be a C API.
   * **Must** not throw exceptions.
   * **Must** return `napi_status`.
   * **Should** consume `napi_env`.
   * **Must** operate on primitive data types, pointers to primitive data types, or opaque handles.
   * **Must** be a necessary API, not a convenience API (which belongs in node-addon-api).
   * **Must** not break ABI compatibility with other Node.js versions.

2. **Maintaining VM agnosticism**
   * New APIs **should** be compatible with various JavaScript VMs.

## Documentation and testing

1. **Documentation**
   * PRs introducing new APIs **must** include corresponding documentation updates.
   * Experimental APIs **must** be clearly documented as experimental and require an explicit compile-time flag
     to opt-in (`#define`).

2. **Testing**
   * PRs **must** include at least one test case demonstrating API usage.
   * **Should** include test cases for various interesting uses of the API.
   * **Should** provide a sample demonstrating realistic usage, similar to a real-world addon.

## Process and approval

1. **Team discussion**
   * New APIs **should** be discussed in a Node-API team meeting.

2. **Review and approval**
   * A new API addition **must** be signed off by at least two Node-API team members.
   * **Should** be implemented in at least one other VM implementation of Node.js.

3. **Experimental phase**
   * New APIs **must** be marked as experimental for at least one minor Node.js release before promotion.
   * **Must** have a feature flag (`NODE_API_EXPERIMENTAL_HAS_<FEATURE>`) for distinguishing experimental
     feature existence.
   * **Must** be considered for backporting.
   * Exit criteria from experimental status include:
     * Opening a PR in `nodejs/node` to remove experimental status, tagged as **node-api** and **semver-minor**.
     * Approval by the Node-API team.
     * Availability of a down-level implementation if backporting is needed.
     * Usage by a published real-world module.
     * Implementation in an alternative VM.
