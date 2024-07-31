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
exports.X509Certificate = exports.EXTENSION_OID_SCT = void 0;
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
const asn1_1 = require("../asn1");
const crypto = __importStar(require("../crypto"));
const oid_1 = require("../oid");
const pem = __importStar(require("../pem"));
const ext_1 = require("./ext");
const EXTENSION_OID_SUBJECT_KEY_ID = '2.5.29.14';
const EXTENSION_OID_KEY_USAGE = '2.5.29.15';
const EXTENSION_OID_SUBJECT_ALT_NAME = '2.5.29.17';
const EXTENSION_OID_BASIC_CONSTRAINTS = '2.5.29.19';
const EXTENSION_OID_AUTHORITY_KEY_ID = '2.5.29.35';
exports.EXTENSION_OID_SCT = '1.3.6.1.4.1.11129.2.4.2';
class X509Certificate {
    constructor(asn1) {
        this.root = asn1;
    }
    static parse(cert) {
        const der = typeof cert === 'string' ? pem.toDER(cert) : cert;
        const asn1 = asn1_1.ASN1Obj.parseBuffer(der);
        return new X509Certificate(asn1);
    }
    get tbsCertificate() {
        return this.tbsCertificateObj;
    }
    get version() {
        // version number is the first element of the version context specific tag
        const ver = this.versionObj.subs[0].toInteger();
        return `v${(ver + BigInt(1)).toString()}`;
    }
    get serialNumber() {
        return this.serialNumberObj.value;
    }
    get notBefore() {
        // notBefore is the first element of the validity sequence
        return this.validityObj.subs[0].toDate();
    }
    get notAfter() {
        // notAfter is the second element of the validity sequence
        return this.validityObj.subs[1].toDate();
    }
    get issuer() {
        return this.issuerObj.value;
    }
    get subject() {
        return this.subjectObj.value;
    }
    get publicKey() {
        return this.subjectPublicKeyInfoObj.toDER();
    }
    get signatureAlgorithm() {
        const oid = this.signatureAlgorithmObj.subs[0].toOID();
        return oid_1.ECDSA_SIGNATURE_ALGOS[oid];
    }
    get signatureValue() {
        // Signature value is a bit string, so we need to skip the first byte
        return this.signatureValueObj.value.subarray(1);
    }
    get subjectAltName() {
        const ext = this.extSubjectAltName;
        return ext?.uri || ext?.rfc822Name;
    }
    get extensions() {
        // The extension list is the first (and only) element of the extensions
        // context specific tag
        const extSeq = this.extensionsObj?.subs[0];
        return extSeq?.subs || /* istanbul ignore next */ [];
    }
    get extKeyUsage() {
        const ext = this.findExtension(EXTENSION_OID_KEY_USAGE);
        return ext ? new ext_1.X509KeyUsageExtension(ext) : undefined;
    }
    get extBasicConstraints() {
        const ext = this.findExtension(EXTENSION_OID_BASIC_CONSTRAINTS);
        return ext ? new ext_1.X509BasicConstraintsExtension(ext) : undefined;
    }
    get extSubjectAltName() {
        const ext = this.findExtension(EXTENSION_OID_SUBJECT_ALT_NAME);
        return ext ? new ext_1.X509SubjectAlternativeNameExtension(ext) : undefined;
    }
    get extAuthorityKeyID() {
        const ext = this.findExtension(EXTENSION_OID_AUTHORITY_KEY_ID);
        return ext ? new ext_1.X509AuthorityKeyIDExtension(ext) : undefined;
    }
    get extSubjectKeyID() {
        const ext = this.findExtension(EXTENSION_OID_SUBJECT_KEY_ID);
        return ext
            ? new ext_1.X509SubjectKeyIDExtension(ext)
            : /* istanbul ignore next */ undefined;
    }
    get extSCT() {
        const ext = this.findExtension(exports.EXTENSION_OID_SCT);
        return ext ? new ext_1.X509SCTExtension(ext) : undefined;
    }
    get isCA() {
        const ca = this.extBasicConstraints?.isCA || false;
        // If the KeyUsage extension is present, keyCertSign must be set
        if (this.extKeyUsage) {
            ca && this.extKeyUsage.keyCertSign;
        }
        return ca;
    }
    extension(oid) {
        const ext = this.findExtension(oid);
        return ext ? new ext_1.X509Extension(ext) : undefined;
    }
    verify(issuerCertificate) {
        // Use the issuer's public key if provided, otherwise use the subject's
        const publicKey = issuerCertificate?.publicKey || this.publicKey;
        const key = crypto.createPublicKey(publicKey);
        return crypto.verify(this.tbsCertificate.toDER(), key, this.signatureValue, this.signatureAlgorithm);
    }
    validForDate(date) {
        return this.notBefore <= date && date <= this.notAfter;
    }
    equals(other) {
        return this.root.toDER().equals(other.root.toDER());
    }
    // Creates a copy of the certificate with a new buffer
    clone() {
        const der = this.root.toDER();
        const clone = Buffer.alloc(der.length);
        der.copy(clone);
        return X509Certificate.parse(clone);
    }
    findExtension(oid) {
        // Find the extension with the given OID. The OID will always be the first
        // element of the extension sequence
        return this.extensions.find((ext) => ext.subs[0].toOID() === oid);
    }
    /////////////////////////////////////////////////////////////////////////////
    // The following properties use the documented x509 structure to locate the
    // desired ASN.1 object
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.1.1
    get tbsCertificateObj() {
        // tbsCertificate is the first element of the certificate sequence
        return this.root.subs[0];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.1.2
    get signatureAlgorithmObj() {
        // signatureAlgorithm is the second element of the certificate sequence
        return this.root.subs[1];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.1.3
    get signatureValueObj() {
        // signatureValue is the third element of the certificate sequence
        return this.root.subs[2];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.1
    get versionObj() {
        // version is the first element of the tbsCertificate sequence
        return this.tbsCertificateObj.subs[0];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.2
    get serialNumberObj() {
        // serialNumber is the second element of the tbsCertificate sequence
        return this.tbsCertificateObj.subs[1];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.4
    get issuerObj() {
        // issuer is the fourth element of the tbsCertificate sequence
        return this.tbsCertificateObj.subs[3];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.5
    get validityObj() {
        // version is the fifth element of the tbsCertificate sequence
        return this.tbsCertificateObj.subs[4];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.6
    get subjectObj() {
        // subject is the sixth element of the tbsCertificate sequence
        return this.tbsCertificateObj.subs[5];
    }
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.7
    get subjectPublicKeyInfoObj() {
        // subjectPublicKeyInfo is the seventh element of the tbsCertificate sequence
        return this.tbsCertificateObj.subs[6];
    }
    // Extensions can't be located by index because their position varies. Instead,
    // we need to find the extensions context specific tag
    // https://www.rfc-editor.org/rfc/rfc5280#section-4.1.2.9
    get extensionsObj() {
        return this.tbsCertificateObj.subs.find((sub) => sub.tag.isContextSpecific(0x03));
    }
}
exports.X509Certificate = X509Certificate;
