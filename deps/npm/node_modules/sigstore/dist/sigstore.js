"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || function (mod) {
    if (mod && mod.__esModule) return mod;
    var result = {};
    if (mod != null) for (var k in mod) if (k !== "default" && Object.prototype.hasOwnProperty.call(mod, k)) __createBinding(result, mod, k);
    __setModuleDefault(result, mod);
    return result;
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.createVerifier = exports.verify = exports.attest = exports.sign = void 0;
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
const bundle_1 = require("@sigstore/bundle");
const tuf = __importStar(require("@sigstore/tuf"));
const verify_1 = require("@sigstore/verify");
const config = __importStar(require("./config"));
async function sign(payload,
/* istanbul ignore next */
options = {}) {
    const bundler = config.createBundleBuilder('messageSignature', options);
    const bundle = await bundler.create({ data: payload });
    return (0, bundle_1.bundleToJSON)(bundle);
}
exports.sign = sign;
async function attest(payload, payloadType,
/* istanbul ignore next */
options = {}) {
    const bundler = config.createBundleBuilder('dsseEnvelope', options);
    const bundle = await bundler.create({ data: payload, type: payloadType });
    return (0, bundle_1.bundleToJSON)(bundle);
}
exports.attest = attest;
async function verify(bundle, dataOrOptions, options) {
    let data;
    if (Buffer.isBuffer(dataOrOptions)) {
        data = dataOrOptions;
    }
    else {
        options = dataOrOptions;
    }
    return createVerifier(options).then((verifier) => verifier.verify(bundle, data));
}
exports.verify = verify;
async function createVerifier(
/* istanbul ignore next */
options = {}) {
    const trustedRoot = await tuf.getTrustedRoot({
        mirrorURL: options.tufMirrorURL,
        rootPath: options.tufRootPath,
        cachePath: options.tufCachePath,
        forceCache: options.tufForceCache,
        retry: options.retry ?? config.DEFAULT_RETRY,
        timeout: options.timeout ?? config.DEFAULT_TIMEOUT,
    });
    const keyFinder = options.keySelector
        ? config.createKeyFinder(options.keySelector)
        : undefined;
    const trustMaterial = (0, verify_1.toTrustMaterial)(trustedRoot, keyFinder);
    const verifierOptions = {
        ctlogThreshold: options.ctLogThreshold,
        tlogThreshold: options.tlogThreshold,
    };
    const verifier = new verify_1.Verifier(trustMaterial, verifierOptions);
    const policy = config.createVerificationPolicy(options);
    return {
        verify: (bundle, payload) => {
            const deserializedBundle = (0, bundle_1.bundleFromJSON)(bundle);
            const signedEntity = (0, verify_1.toSignedEntity)(deserializedBundle, payload);
            verifier.verify(signedEntity, policy);
            return;
        },
    };
}
exports.createVerifier = createVerifier;
