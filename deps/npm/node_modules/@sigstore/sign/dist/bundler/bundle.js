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
exports.toMessageSignatureBundle = toMessageSignatureBundle;
exports.toDSSEBundle = toDSSEBundle;
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
const sigstore = __importStar(require("@sigstore/bundle"));
const util_1 = require("../util");
// Helper functions for assembling the parts of a Sigstore bundle
// Message signature bundle - $case: 'messageSignature'
function toMessageSignatureBundle(artifact, signature) {
    const digest = util_1.crypto.digest('sha256', artifact.data);
    return sigstore.toMessageSignatureBundle({
        digest,
        signature: signature.signature,
        certificate: signature.key.$case === 'x509Certificate'
            ? util_1.pem.toDER(signature.key.certificate)
            : undefined,
        keyHint: signature.key.$case === 'publicKey' ? signature.key.hint : undefined,
        certificateChain: true,
    });
}
// DSSE envelope bundle - $case: 'dsseEnvelope'
function toDSSEBundle(artifact, signature, certificateChain) {
    return sigstore.toDSSEBundle({
        artifact: artifact.data,
        artifactType: artifact.type,
        signature: signature.signature,
        certificate: signature.key.$case === 'x509Certificate'
            ? util_1.pem.toDER(signature.key.certificate)
            : undefined,
        keyHint: signature.key.$case === 'publicKey' ? signature.key.hint : undefined,
        certificateChain,
    });
}
