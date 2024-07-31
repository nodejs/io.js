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
exports.TSTInfo = void 0;
const crypto = __importStar(require("../crypto"));
const oid_1 = require("../oid");
const error_1 = require("./error");
class TSTInfo {
    constructor(asn1) {
        this.root = asn1;
    }
    get version() {
        return this.root.subs[0].toInteger();
    }
    get genTime() {
        return this.root.subs[4].toDate();
    }
    get messageImprintHashAlgorithm() {
        const oid = this.messageImprintObj.subs[0].subs[0].toOID();
        return oid_1.SHA2_HASH_ALGOS[oid];
    }
    get messageImprintHashedMessage() {
        return this.messageImprintObj.subs[1].value;
    }
    get raw() {
        return this.root.toDER();
    }
    verify(data) {
        const digest = crypto.digest(this.messageImprintHashAlgorithm, data);
        if (!crypto.bufferEqual(digest, this.messageImprintHashedMessage)) {
            throw new error_1.RFC3161TimestampVerificationError('message imprint does not match artifact');
        }
    }
    // https://www.rfc-editor.org/rfc/rfc3161#section-2.4.2
    get messageImprintObj() {
        return this.root.subs[2];
    }
}
exports.TSTInfo = TSTInfo;
