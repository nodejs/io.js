"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.DEFAULT_TIMEOUT = exports.DEFAULT_RETRY = void 0;
exports.createBundleBuilder = createBundleBuilder;
exports.createKeyFinder = createKeyFinder;
exports.createVerificationPolicy = createVerificationPolicy;
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
const core_1 = require("@sigstore/core");
const sign_1 = require("@sigstore/sign");
const verify_1 = require("@sigstore/verify");
exports.DEFAULT_RETRY = { retries: 2 };
exports.DEFAULT_TIMEOUT = 5000;
function createBundleBuilder(bundleType, options) {
    const bundlerOptions = {
        signer: initSigner(options),
        witnesses: initWitnesses(options),
    };
    switch (bundleType) {
        case 'messageSignature':
            return new sign_1.MessageSignatureBundleBuilder(bundlerOptions);
        case 'dsseEnvelope':
            return new sign_1.DSSEBundleBuilder({
                ...bundlerOptions,
                certificateChain: options.legacyCompatibility,
            });
    }
}
// Translates the public KeySelector type into the KeyFinderFunc type needed by
// the verifier.
function createKeyFinder(keySelector) {
    return (hint) => {
        const key = keySelector(hint);
        if (!key) {
            throw new verify_1.VerificationError({
                code: 'PUBLIC_KEY_ERROR',
                message: `key not found: ${hint}`,
            });
        }
        return {
            publicKey: core_1.crypto.createPublicKey(key),
            validFor: () => true,
        };
    };
}
function createVerificationPolicy(options) {
    const policy = {};
    const san = options.certificateIdentityEmail || options.certificateIdentityURI;
    if (san) {
        policy.subjectAlternativeName = san;
    }
    if (options.certificateIssuer) {
        policy.extensions = { issuer: options.certificateIssuer };
    }
    return policy;
}
// Instantiate the FulcioSigner based on the supplied options.
function initSigner(options) {
    return new sign_1.FulcioSigner({
        fulcioBaseURL: options.fulcioURL,
        identityProvider: options.identityProvider || initIdentityProvider(options),
        retry: options.retry ?? exports.DEFAULT_RETRY,
        timeout: options.timeout ?? exports.DEFAULT_TIMEOUT,
    });
}
// Instantiate an identity provider based on the supplied options. If an
// explicit identity token is provided, use that. Otherwise, use the CI
// context provider.
function initIdentityProvider(options) {
    const token = options.identityToken;
    if (token) {
        /* istanbul ignore next */
        return { getToken: () => Promise.resolve(token) };
    }
    else {
        return new sign_1.CIContextProvider('sigstore');
    }
}
// Instantiate a collection of witnesses based on the supplied options.
function initWitnesses(options) {
    const witnesses = [];
    if (isRekorEnabled(options)) {
        witnesses.push(new sign_1.RekorWitness({
            rekorBaseURL: options.rekorURL,
            entryType: options.legacyCompatibility ? 'intoto' : 'dsse',
            fetchOnConflict: false,
            retry: options.retry ?? exports.DEFAULT_RETRY,
            timeout: options.timeout ?? exports.DEFAULT_TIMEOUT,
        }));
    }
    if (isTSAEnabled(options)) {
        witnesses.push(new sign_1.TSAWitness({
            tsaBaseURL: options.tsaServerURL,
            retry: options.retry ?? exports.DEFAULT_RETRY,
            timeout: options.timeout ?? exports.DEFAULT_TIMEOUT,
        }));
    }
    return witnesses;
}
// Type assertion to ensure that Rekor is enabled
function isRekorEnabled(options) {
    return options.tlogUpload !== false;
}
// Type assertion to ensure that TSA is enabled
function isTSAEnabled(options) {
    return options.tsaServerURL !== undefined;
}
