"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.X509SCTExtension = exports.X509SubjectKeyIDExtension = exports.X509AuthorityKeyIDExtension = exports.X509SubjectAlternativeNameExtension = exports.X509KeyUsageExtension = exports.X509BasicConstraintsExtension = exports.X509Extension = void 0;
const stream_1 = require("../stream");
const sct_1 = require("./sct");
// https://www.rfc-editor.org/rfc/rfc5280#section-4.1
class X509Extension {
    constructor(asn1) {
        this.root = asn1;
    }
    get oid() {
        return this.root.subs[0].toOID();
    }
    get critical() {
        // The critical field is optional and will be the second element of the
        // extension sequence if present. Default to false if not present.
        return this.root.subs.length === 3 ? this.root.subs[1].toBoolean() : false;
    }
    get value() {
        return this.extnValueObj.value;
    }
    get valueObj() {
        return this.extnValueObj;
    }
    get extnValueObj() {
        // The extnValue field will be the last element of the extension sequence
        return this.root.subs[this.root.subs.length - 1];
    }
}
exports.X509Extension = X509Extension;
// https://www.rfc-editor.org/rfc/rfc5280#section-4.2.1.9
class X509BasicConstraintsExtension extends X509Extension {
    get isCA() {
        return this.sequence.subs[0]?.toBoolean() ?? false;
    }
    get pathLenConstraint() {
        return this.sequence.subs.length > 1
            ? this.sequence.subs[1].toInteger()
            : undefined;
    }
    // The extnValue field contains a single sequence wrapping the isCA and
    // pathLenConstraint.
    get sequence() {
        return this.extnValueObj.subs[0];
    }
}
exports.X509BasicConstraintsExtension = X509BasicConstraintsExtension;
// https://www.rfc-editor.org/rfc/rfc5280#section-4.2.1.3
class X509KeyUsageExtension extends X509Extension {
    get digitalSignature() {
        return this.bitString[0] === 1;
    }
    get keyCertSign() {
        return this.bitString[5] === 1;
    }
    get crlSign() {
        return this.bitString[6] === 1;
    }
    // The extnValue field contains a single bit string which is a bit mask
    // indicating which key usages are enabled.
    get bitString() {
        return this.extnValueObj.subs[0].toBitString();
    }
}
exports.X509KeyUsageExtension = X509KeyUsageExtension;
// https://www.rfc-editor.org/rfc/rfc5280#section-4.2.1.6
class X509SubjectAlternativeNameExtension extends X509Extension {
    get rfc822Name() {
        return this.findGeneralName(0x01)?.value.toString('ascii');
    }
    get uri() {
        return this.findGeneralName(0x06)?.value.toString('ascii');
    }
    // Retrieve the value of an otherName with the given OID.
    otherName(oid) {
        const otherName = this.findGeneralName(0x00);
        if (otherName === undefined) {
            return undefined;
        }
        // The otherName is a sequence containing an OID and a value.
        // Need to check that the OID matches the one we're looking for.
        const otherNameOID = otherName.subs[0].toOID();
        if (otherNameOID !== oid) {
            return undefined;
        }
        // The otherNameValue is a sequence containing the actual value.
        const otherNameValue = otherName.subs[1];
        return otherNameValue.subs[0].value.toString('ascii');
    }
    findGeneralName(tag) {
        return this.generalNames.find((gn) => gn.tag.isContextSpecific(tag));
    }
    // The extnValue field contains a sequence of GeneralNames.
    get generalNames() {
        return this.extnValueObj.subs[0].subs;
    }
}
exports.X509SubjectAlternativeNameExtension = X509SubjectAlternativeNameExtension;
// https://www.rfc-editor.org/rfc/rfc5280#section-4.2.1.1
class X509AuthorityKeyIDExtension extends X509Extension {
    get keyIdentifier() {
        return this.findSequenceMember(0x00)?.value;
    }
    findSequenceMember(tag) {
        return this.sequence.subs.find((el) => el.tag.isContextSpecific(tag));
    }
    // The extnValue field contains a single sequence wrapping the keyIdentifier
    get sequence() {
        return this.extnValueObj.subs[0];
    }
}
exports.X509AuthorityKeyIDExtension = X509AuthorityKeyIDExtension;
// https://www.rfc-editor.org/rfc/rfc5280#section-4.2.1.2
class X509SubjectKeyIDExtension extends X509Extension {
    get keyIdentifier() {
        return this.extnValueObj.subs[0].value;
    }
}
exports.X509SubjectKeyIDExtension = X509SubjectKeyIDExtension;
// https://www.rfc-editor.org/rfc/rfc6962#section-3.3
class X509SCTExtension extends X509Extension {
    constructor(asn1) {
        super(asn1);
    }
    get signedCertificateTimestamps() {
        const buf = this.extnValueObj.subs[0].value;
        const stream = new stream_1.ByteStream(buf);
        // The overall list length is encoded in the first two bytes -- note this
        // is the length of the list in bytes, NOT the number of SCTs in the list
        const end = stream.getUint16() + 2;
        const sctList = [];
        while (stream.position < end) {
            // Read the length of the next SCT
            const sctLength = stream.getUint16();
            // Slice out the bytes for the next SCT and parse it
            const sct = stream.getBlock(sctLength);
            sctList.push(sct_1.SignedCertificateTimestamp.parse(sct));
        }
        if (stream.position !== end) {
            throw new Error('SCT list length does not match actual length');
        }
        return sctList;
    }
}
exports.X509SCTExtension = X509SCTExtension;
