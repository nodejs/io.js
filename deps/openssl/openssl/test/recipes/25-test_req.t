#! /usr/bin/env perl
# Copyright 2015-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


use strict;
use warnings;

use OpenSSL::Test::Utils;
use OpenSSL::Test qw/:DEFAULT srctop_file/;

setup("test_req");

plan tests => 49;

require_ok(srctop_file('test', 'recipes', 'tconversion.pl'));

my @certs = qw(test certs);

# What type of key to generate?
my @req_new;
if (disabled("rsa")) {
    @req_new = ("-newkey", "dsa:".srctop_file("apps", "dsa512.pem"));
} else {
    @req_new = ("-new");
    note("There should be a 2 sequences of .'s and some +'s.");
    note("There should not be more that at most 80 per line");
}

# Prevent MSys2 filename munging for arguments that look like file paths but
# aren't
$ENV{MSYS2_ARG_CONV_EXCL} = "/CN=";

# Check for duplicate -addext parameters, and one "working" case.
my @addext_args = ( "openssl", "req", "-new", "-out", "testreq.pem",
                    "-key",  srctop_file("test", "certs", "ee-key.pem"),
    "-config", srctop_file("test", "test.cnf"), @req_new );
my $val = "subjectAltName=DNS:example.com";
my $val1 = "subjectAltName=otherName:1.2.3.4;UTF8:test,email:info\@example.com";
my $val2 = " " . $val;
my $val3 = $val;
$val3 =~ s/=/    =/;
ok( run(app([@addext_args, "-addext", $val])));
ok( run(app([@addext_args, "-addext", $val1])));
$val1 =~ s/UTF8/XXXX/; # execute the error handling in do_othername
ok(!run(app([@addext_args, "-addext", $val1])));
ok(!run(app([@addext_args, "-addext", $val, "-addext", $val])));
ok(!run(app([@addext_args, "-addext", $val, "-addext", $val2])));
ok(!run(app([@addext_args, "-addext", $val, "-addext", $val3])));
ok(!run(app([@addext_args, "-addext", $val2, "-addext", $val3])));
ok(run(app([@addext_args, "-addext", "SXNetID=1:one, 2:two, 3:three"])));

# If a CSR is provided with neither of -key or -CA/-CAkey, this should fail.
ok(!run(app(["openssl", "req", "-x509",
                "-in", srctop_file(@certs, "x509-check.csr"),
                "-out", "testreq.pem"])));

subtest "generating alt certificate requests with RSA" => sub {
    plan tests => 3;

    SKIP: {
        skip "RSA is not supported by this OpenSSL build", 2
            if disabled("rsa");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-section", "altreq",
                    "-new", "-out", "testreq-rsa.pem", "-utf8",
                    "-key", srctop_file("test", "testrsa.pem")])),
           "Generating request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-rsa.pem", "-noout"])),
           "Verifying signature on request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-section", "altreq",
                    "-verify", "-in", "testreq-rsa.pem", "-noout"])),
           "Verifying signature on request");
    }
};


subtest "generating certificate requests with RSA" => sub {
    plan tests => 8;

    SKIP: {
        skip "RSA is not supported by this OpenSSL build", 2
            if disabled("rsa");

        ok(!run(app(["openssl", "req",
                     "-config", srctop_file("test", "test.cnf"),
                     "-new", "-out", "testreq-rsa.pem", "-utf8",
                     "-key", srctop_file("test", "testrsa.pem"),
                     "-keyform", "DER"])),
           "Checking that mismatching keyform fails");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-rsa.pem", "-utf8",
                    "-key", srctop_file("test", "testrsa.pem"),
                    "-keyform", "PEM"])),
           "Generating request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-rsa.pem", "-noout"])),
           "Verifying signature on request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-modulus", "-in", "testreq-rsa.pem", "-noout"])),
           "Printing a modulus of the request key");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq_withattrs_pem.pem", "-utf8",
                    "-key", srctop_file("test", "testrsa_withattrs.pem")])),
           "Generating request from a key with extra attributes - PEM");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq_withattrs_pem.pem", "-noout"])),
           "Verifying signature on request from a key with extra attributes - PEM");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq_withattrs_der.pem", "-utf8",
                    "-key", srctop_file("test", "testrsa_withattrs.der"),
                    "-keyform", "DER"])),
           "Generating request from a key with extra attributes - PEM");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq_withattrs_der.pem", "-noout"])),
           "Verifying signature on request from a key with extra attributes - PEM");
    }
};

subtest "generating certificate requests with RSA-PSS" => sub {
    plan tests => 12;

    SKIP: {
        skip "RSA is not supported by this OpenSSL build", 2
            if disabled("rsa");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-rsapss.pem", "-utf8",
                    "-key", srctop_file("test", "testrsapss.pem")])),
           "Generating request");
        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-rsapss.pem", "-noout"])),
           "Verifying signature on request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-rsapss2.pem", "-utf8",
                    "-sigopt", "rsa_padding_mode:pss",
                    "-sigopt", "rsa_pss_saltlen:-1",
                    "-key", srctop_file("test", "testrsapss.pem")])),
           "Generating request");
        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-rsapss2.pem", "-noout"])),
           "Verifying signature on request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-rsapssmand.pem", "-utf8",
                    "-sigopt", "rsa_padding_mode:pss",
                    "-key", srctop_file("test", "testrsapssmandatory.pem")])),
           "Generating request");
        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-rsapssmand.pem", "-noout"])),
           "Verifying signature on request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-rsapssmand2.pem", "-utf8",
                    "-sigopt", "rsa_pss_saltlen:100",
                    "-key", srctop_file("test", "testrsapssmandatory.pem")])),
           "Generating request");
        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-rsapssmand2.pem", "-noout"])),
           "Verifying signature on request");

        ok(!run(app(["openssl", "req",
                     "-config", srctop_file("test", "test.cnf"),
                     "-new", "-out", "testreq-rsapss3.pem", "-utf8",
                     "-sigopt", "rsa_padding_mode:pkcs1",
                     "-key", srctop_file("test", "testrsapss.pem")])),
           "Generating request with expected failure");

        ok(!run(app(["openssl", "req",
                     "-config", srctop_file("test", "test.cnf"),
                     "-new", "-out", "testreq-rsapss3.pem", "-utf8",
                     "-sigopt", "rsa_pss_saltlen:-4",
                     "-key", srctop_file("test", "testrsapss.pem")])),
           "Generating request with expected failure");

        ok(!run(app(["openssl", "req",
                     "-config", srctop_file("test", "test.cnf"),
                     "-new", "-out", "testreq-rsapssmand3.pem", "-utf8",
                     "-sigopt", "rsa_pss_saltlen:10",
                     "-key", srctop_file("test", "testrsapssmandatory.pem")])),
           "Generating request with expected failure");

        ok(!run(app(["openssl", "req",
                     "-config", srctop_file("test", "test.cnf"),
                     "-new", "-out", "testreq-rsapssmand3.pem", "-utf8",
                     "-sha256",
                     "-key", srctop_file("test", "testrsapssmandatory.pem")])),
           "Generating request with expected failure");
    }
};

subtest "generating certificate requests with DSA" => sub {
    plan tests => 2;

    SKIP: {
        skip "DSA is not supported by this OpenSSL build", 2
            if disabled("dsa");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-dsa.pem", "-utf8",
                    "-key", srctop_file("test", "testdsa.pem")])),
           "Generating request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-dsa.pem", "-noout"])),
           "Verifying signature on request");
    }
};

subtest "generating certificate requests with ECDSA" => sub {
    plan tests => 2;

    SKIP: {
        skip "ECDSA is not supported by this OpenSSL build", 2
            if disabled("ec");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-ec.pem", "-utf8",
                    "-key", srctop_file("test", "testec-p256.pem")])),
           "Generating request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-ec.pem", "-noout"])),
           "Verifying signature on request");
    }
};

subtest "generating certificate requests with Ed25519" => sub {
    plan tests => 2;

    SKIP: {
        skip "Ed25519 is not supported by this OpenSSL build", 2
            if disabled("ec");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-ed25519.pem", "-utf8",
                    "-key", srctop_file("test", "tested25519.pem")])),
           "Generating request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-ed25519.pem", "-noout"])),
           "Verifying signature on request");
    }
};

subtest "generating certificate requests with Ed448" => sub {
    plan tests => 2;

    SKIP: {
        skip "Ed448 is not supported by this OpenSSL build", 2
            if disabled("ec");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-out", "testreq-ed448.pem", "-utf8",
                    "-key", srctop_file("test", "tested448.pem")])),
           "Generating request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-ed448.pem", "-noout"])),
           "Verifying signature on request");
    }
};

subtest "generating certificate requests" => sub {
    plan tests => 2;

    ok(run(app(["openssl", "req", "-config", srctop_file("test", "test.cnf"),
                "-key", srctop_file("test", "certs", "ee-key.pem"),
                @req_new, "-out", "testreq.pem"])),
       "Generating request");

    ok(run(app(["openssl", "req", "-config", srctop_file("test", "test.cnf"),
                "-verify", "-in", "testreq.pem", "-noout"])),
       "Verifying signature on request");
};

subtest "generating SM2 certificate requests" => sub {
    plan tests => 4;

    SKIP: {
        skip "SM2 is not supported by this OpenSSL build", 4
        if disabled("sm2");
        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-key", srctop_file(@certs, "sm2.key"),
                    "-sigopt", "distid:1234567812345678",
                    "-out", "testreq-sm2.pem", "-sm3"])),
           "Generating SM2 certificate request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-sm2.pem", "-noout",
                    "-vfyopt", "distid:1234567812345678", "-sm3"])),
           "Verifying signature on SM2 certificate request");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-new", "-key", srctop_file(@certs, "sm2.key"),
                    "-sigopt", "hexdistid:DEADBEEF",
                    "-out", "testreq-sm2.pem", "-sm3"])),
           "Generating SM2 certificate request with hex id");

        ok(run(app(["openssl", "req",
                    "-config", srctop_file("test", "test.cnf"),
                    "-verify", "-in", "testreq-sm2.pem", "-noout",
                    "-vfyopt", "hexdistid:DEADBEEF", "-sm3"])),
           "Verifying signature on SM2 certificate request");
    }
};

my @openssl_args = ("req", "-config", srctop_file("apps", "openssl.cnf"));

run_conversion('req conversions',
               "testreq.pem");
run_conversion('req conversions -- testreq2',
               srctop_file("test", "testreq2.pem"));

sub run_conversion {
    my $title = shift;
    my $reqfile = shift;

    subtest $title => sub {
        run(app(["openssl", @openssl_args,
                 "-in", $reqfile, "-inform", "p",
                 "-noout", "-text"],
                stderr => "req-check.err", stdout => undef));
        open DATA, "req-check.err";
        SKIP: {
            plan skip_all => "skipping req conversion test for $reqfile"
                if grep /Unknown Public Key/, map { s/\R//; } <DATA>;

            tconversion( -type => 'req', -in => $reqfile,
                         -args => [ @openssl_args ] );
        }
        close DATA;
        unlink "req-check.err";

        done_testing();
    };
}

# Test both generation and verification of certs w.r.t. RFC 5280 requirements

my $ca_cert; # will be set below
sub generate_cert {
    my $cert = shift @_;
    my $ss = $cert =~ m/self-signed/;
    my $is_ca = $cert =~ m/CA/;
    my $cn = $is_ca ? "CA" : "EE";
    my $ca_key = srctop_file(@certs, "ca-key.pem");
    my $key = $is_ca ? $ca_key : srctop_file(@certs, "ee-key.pem");
    my @cmd = ("openssl", "req", "-config", "", "-x509",
               "-subj", "/CN=$cn", @_, "-out", $cert);
    push(@cmd, ("-key", $key)) if $ss;
    push(@cmd, ("-CA", $ca_cert, "-CAkey", $ca_key)) unless $ss;
    ok(run(app([@cmd])), "generate $cert");
}
sub has_SKID {
    my $cert = shift @_;
    my $expect = shift @_;
    cert_contains($cert, "Subject Key Identifier", $expect);
}
sub has_AKID {
    my $cert = shift @_;
    my $expect = shift @_;
    cert_contains($cert, "Authority Key Identifier", $expect);
}
sub has_keyUsage {
    my $cert = shift @_;
    my $expect = shift @_;
    cert_contains($cert, "Key Usage", $expect);
}
sub strict_verify {
    my $cert = shift @_;
    my $expect = shift @_;
    my $trusted = shift @_;
    $trusted = $cert unless $trusted;
    ok(run(app(["openssl", "verify", "-x509_strict", "-trusted", $trusted,
                "-partial_chain", $cert])) == $expect,
       "strict verify allow $cert");
}

my @v3_ca = ("-addext", "basicConstraints = critical,CA:true",
             "-addext", "keyUsage = keyCertSign");
my $SKID_AKID = "subjectKeyIdentifier,authorityKeyIdentifier";
my $cert = "self-signed_v1_CA_no_KIDs.pem";
generate_cert($cert);
cert_ext_has_n_different_lines($cert, 0, $SKID_AKID); # no SKID and no AKID
#TODO strict_verify($cert, 1); # self-signed v1 root cert should be accepted as CA

$ca_cert = "self-signed_v3_CA_default_SKID.pem";
generate_cert($ca_cert, @v3_ca);
has_SKID($ca_cert, 1);
has_AKID($ca_cert, 0);
strict_verify($ca_cert, 1);

$cert = "self-signed_v3_CA_no_SKID.pem";
generate_cert($cert, @v3_ca, "-addext", "subjectKeyIdentifier = none");
cert_ext_has_n_different_lines($cert, 0, $SKID_AKID); # no SKID and no AKID
#TODO strict_verify($cert, 0);

$cert = "self-signed_v3_CA_both_KIDs.pem";
generate_cert($cert, @v3_ca, "-addext", "subjectKeyIdentifier = hash",
            "-addext", "authorityKeyIdentifier = keyid:always");
cert_ext_has_n_different_lines($cert, 3, $SKID_AKID); # SKID == AKID
strict_verify($cert, 1);

$cert = "self-signed_v3_EE_wrong_keyUsage.pem";
generate_cert($cert, "-addext", "keyUsage = keyCertSign");
#TODO strict_verify($cert, 1); # should be accepted because RFC 5280 does not apply

$cert = "v3_EE_default_KIDs.pem";
generate_cert($cert, "-addext", "keyUsage = dataEncipherment",
              "-key", srctop_file(@certs, "ee-key.pem"));
cert_ext_has_n_different_lines($cert, 4, $SKID_AKID); # SKID != AKID
strict_verify($cert, 1, $ca_cert);

$cert = "v3_EE_no_AKID.pem";
generate_cert($cert, "-addext", "authorityKeyIdentifier = none",
              "-key", srctop_file(@certs, "ee-key.pem"));
has_SKID($cert, 1);
has_AKID($cert, 0);
strict_verify($cert, 0, $ca_cert);

$cert = "self-issued_v3_EE_default_KIDs.pem";
generate_cert($cert, "-addext", "keyUsage = dataEncipherment",
    "-in", srctop_file(@certs, "x509-check.csr"));
cert_ext_has_n_different_lines($cert, 4, $SKID_AKID); # SKID != AKID
strict_verify($cert, 1);

my $cert = "self-signed_CA_no_keyUsage.pem";
generate_cert($cert, "-in", srctop_file(@certs, "ext-check.csr"));
has_keyUsage($cert, 0);
my $cert = "self-signed_CA_with_keyUsages.pem";
generate_cert($cert, "-in", srctop_file(@certs, "ext-check.csr"),
    "-copy_extensions", "copy");
has_keyUsage($cert, 1);

# Generate cert using req with '-modulus'
ok(run(app(["openssl", "req", "-x509", "-new", "-days", "365",
            "-key", srctop_file("test", "testrsa.pem"),
            "-config", srctop_file('test', 'test.cnf'),
            "-out", "testreq-cert.pem",
            "-modulus"])), "cert req creation - with -modulus");

# Verify cert
ok(run(app(["openssl", "x509", "-in", "testreq-cert.pem",
            "-noout", "-text"])), "cert verification");
