"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.TUFError = exports.DEFAULT_MIRROR_URL = void 0;
exports.getTrustedRoot = getTrustedRoot;
exports.initTUF = initTUF;
/*
Copyright 2023 The Sigstore Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
const protobuf_specs_1 = require("@sigstore/protobuf-specs");
const appdata_1 = require("./appdata");
const client_1 = require("./client");
exports.DEFAULT_MIRROR_URL = 'https://tuf-repo-cdn.sigstore.dev';
const DEFAULT_CACHE_DIR = 'sigstore-js';
const DEFAULT_RETRY = { retries: 2 };
const DEFAULT_TIMEOUT = 5000;
const TRUSTED_ROOT_TARGET = 'trusted_root.json';
async function getTrustedRoot(
/* istanbul ignore next */
options = {}) {
    const client = createClient(options);
    const trustedRoot = await client.getTarget(TRUSTED_ROOT_TARGET);
    return protobuf_specs_1.TrustedRoot.fromJSON(JSON.parse(trustedRoot));
}
async function initTUF(
/* istanbul ignore next */
options = {}) {
    const client = createClient(options);
    return client.refresh().then(() => client);
}
// Create a TUF client with default options
function createClient(options) {
    /* istanbul ignore next */
    return new client_1.TUFClient({
        cachePath: options.cachePath || (0, appdata_1.appDataPath)(DEFAULT_CACHE_DIR),
        rootPath: options.rootPath,
        mirrorURL: options.mirrorURL || exports.DEFAULT_MIRROR_URL,
        retry: options.retry ?? DEFAULT_RETRY,
        timeout: options.timeout ?? DEFAULT_TIMEOUT,
        forceCache: options.forceCache ?? false,
        forceInit: options.forceInit ?? options.force ?? false,
    });
}
var error_1 = require("./error");
Object.defineProperty(exports, "TUFError", { enumerable: true, get: function () { return error_1.TUFError; } });
