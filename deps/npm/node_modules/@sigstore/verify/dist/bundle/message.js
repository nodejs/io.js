"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.MessageSignatureContent = void 0;
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
class MessageSignatureContent {
    constructor(messageSignature, artifact) {
        this.signature = messageSignature.signature;
        this.messageDigest = messageSignature.messageDigest.digest;
        this.artifact = artifact;
    }
    compareSignature(signature) {
        return core_1.crypto.bufferEqual(signature, this.signature);
    }
    compareDigest(digest) {
        return core_1.crypto.bufferEqual(digest, this.messageDigest);
    }
    verifySignature(key) {
        return core_1.crypto.verify(this.artifact, key, this.signature);
    }
}
exports.MessageSignatureContent = MessageSignatureContent;
