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
exports.ua = exports.oidc = exports.pem = exports.json = exports.encoding = exports.dsse = exports.crypto = void 0;
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
var core_1 = require("@sigstore/core");
Object.defineProperty(exports, "crypto", { enumerable: true, get: function () { return core_1.crypto; } });
Object.defineProperty(exports, "dsse", { enumerable: true, get: function () { return core_1.dsse; } });
Object.defineProperty(exports, "encoding", { enumerable: true, get: function () { return core_1.encoding; } });
Object.defineProperty(exports, "json", { enumerable: true, get: function () { return core_1.json; } });
Object.defineProperty(exports, "pem", { enumerable: true, get: function () { return core_1.pem; } });
exports.oidc = __importStar(require("./oidc"));
exports.ua = __importStar(require("./ua"));
