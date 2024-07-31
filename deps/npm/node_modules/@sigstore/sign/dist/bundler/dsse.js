"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.DSSEBundleBuilder = void 0;
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
const util_1 = require("../util");
const base_1 = require("./base");
const bundle_1 = require("./bundle");
// BundleBuilder implementation for DSSE wrapped attestations
class DSSEBundleBuilder extends base_1.BaseBundleBuilder {
    constructor(options) {
        super(options);
        this.singleCertificate = options.singleCertificate ?? false;
    }
    // DSSE requires the artifact to be pre-encoded with the payload type
    // before the signature is generated.
    async prepare(artifact) {
        const a = artifactDefaults(artifact);
        return util_1.dsse.preAuthEncoding(a.type, a.data);
    }
    // Packages the artifact and signature into a DSSE bundle
    async package(artifact, signature) {
        return (0, bundle_1.toDSSEBundle)(artifactDefaults(artifact), signature, this.singleCertificate);
    }
}
exports.DSSEBundleBuilder = DSSEBundleBuilder;
// Defaults the artifact type to an empty string if not provided
function artifactDefaults(artifact) {
    return {
        ...artifact,
        type: artifact.type ?? '',
    };
}
