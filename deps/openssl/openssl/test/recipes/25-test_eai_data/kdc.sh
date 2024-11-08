#! /usr/bin/env bash

# Create a root CA, signing a leaf cert with a KDC principal otherName SAN, and
# also a non-UTF8 smtpUtf8Mailbox SAN followed by an rfc822Name SAN and a DNS
# name SAN.  In the vulnerable EAI code, the KDC principal `otherName` should
# trigger ASAN errors in DNS name checks, while the non-UTF8 `smtpUtf8Mailbox`
# should likewise lead to ASAN issues with email name checks.

rm -f root-key.pem root-cert.pem
openssl req -nodes -new -newkey rsa:2048 -keyout kdc-root-key.pem \
        -x509 -subj /CN=Root -days 36524 -out kdc-root-cert.pem

exts=$(
    printf "%s\n%s\n%s\n%s = " \
        "subjectKeyIdentifier = hash" \
        "authorityKeyIdentifier = keyid" \
        "basicConstraints = CA:false" \
        "subjectAltName"
    printf "%s, " "otherName:1.3.6.1.5.2.2;SEQUENCE:kdc_princ_name"
    printf "%s, " "otherName:1.3.6.1.5.5.7.8.9;IA5:moe@example.com"
    printf "%s, " "email:joe@example.com"
    printf "%s\n" "DNS:mx1.example.com"
    printf "[kdc_princ_name]\n"
    printf "realm = EXP:0, GeneralString:TEST.EXAMPLE\n"
    printf "principal_name = EXP:1, SEQUENCE:kdc_principal_seq\n"
    printf "[kdc_principal_seq]\n"
    printf "name_type = EXP:0, INTEGER:1\n"
    printf "name_string = EXP:1, SEQUENCE:kdc_principal_components\n"
    printf "[kdc_principal_components]\n"
    printf "princ1 = GeneralString:krbtgt\n"
    printf "princ2 = GeneralString:TEST.EXAMPLE\n"
    )

printf "%s\n" "$exts"

openssl req -nodes -new -newkey rsa:2048 -keyout kdc-key.pem \
    -subj "/CN=TEST.EXAMPLE" |
    openssl x509 -req -out kdc-cert.pem \
        -CA "kdc-root-cert.pem" -CAkey "kdc-root-key.pem" \
        -set_serial 2 -days 36524 \
        -extfile <(printf "%s\n" "$exts")
