"use strict";
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
Object.defineProperty(exports, "__esModule", { value: true });
exports.X509SCTExtension = exports.X509Certificate = exports.EXTENSION_OID_SCT = void 0;
var cert_1 = require("./cert");
Object.defineProperty(exports, "EXTENSION_OID_SCT", { enumerable: true, get: function () { return cert_1.EXTENSION_OID_SCT; } });
Object.defineProperty(exports, "X509Certificate", { enumerable: true, get: function () { return cert_1.X509Certificate; } });
var ext_1 = require("./ext");
Object.defineProperty(exports, "X509SCTExtension", { enumerable: true, get: function () { return ext_1.X509SCTExtension; } });
