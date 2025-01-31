"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.DSSESignatureContent = void 0;
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
class DSSESignatureContent {
    constructor(env) {
        this.env = env;
    }
    compareDigest(digest) {
        return core_1.crypto.bufferEqual(digest, core_1.crypto.digest('sha256', this.env.payload));
    }
    compareSignature(signature) {
        return core_1.crypto.bufferEqual(signature, this.signature);
    }
    verifySignature(key) {
        return core_1.crypto.verify(this.preAuthEncoding, key, this.signature);
    }
    get signature() {
        return this.env.signatures.length > 0
            ? this.env.signatures[0].sig
            : Buffer.from('');
    }
    // DSSE Pre-Authentication Encoding
    get preAuthEncoding() {
        return core_1.dsse.preAuthEncoding(this.env.payloadType, this.env.payload);
    }
}
exports.DSSESignatureContent = DSSESignatureContent;
