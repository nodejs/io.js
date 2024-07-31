"use strict";
/* istanbul ignore file */
Object.defineProperty(exports, "__esModule", { value: true });
exports.TSAWitness = exports.RekorWitness = exports.DEFAULT_REKOR_URL = void 0;
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
var tlog_1 = require("./tlog");
Object.defineProperty(exports, "DEFAULT_REKOR_URL", { enumerable: true, get: function () { return tlog_1.DEFAULT_REKOR_URL; } });
Object.defineProperty(exports, "RekorWitness", { enumerable: true, get: function () { return tlog_1.RekorWitness; } });
var tsa_1 = require("./tsa");
Object.defineProperty(exports, "TSAWitness", { enumerable: true, get: function () { return tsa_1.TSAWitness; } });
