#! /usr/bin/env perl
# Copyright 2015-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


use strict;
use warnings;

use File::Spec;
use OpenSSL::Test::Utils;
use OpenSSL::Test qw/:DEFAULT srctop_file/;

setup("test_x509");

plan tests => 29;

# Prevent MSys2 filename munging for arguments that look like file paths but
# aren't
$ENV{MSYS2_ARG_CONV_EXCL} = "/CN=";

require_ok(srctop_file("test", "recipes", "tconversion.pl"));

my @certs = qw(test certs);
my $pem = srctop_file(@certs, "cyrillic.pem");
my $out_msb = "out-cyrillic.msb";
my $out_utf8 = "out-cyrillic.utf8";
my $der = "cyrillic.der";
my $der2 = "cyrillic.der";
my $msb = srctop_file(@certs, "cyrillic.msb");
my $utf = srctop_file(@certs, "cyrillic.utf8");

ok(run(app(["openssl", "x509", "-text", "-in", $pem, "-out", $out_msb,
            "-nameopt", "esc_msb"])));
is(cmp_text($out_msb, $msb),
   0, 'Comparing esc_msb output with cyrillic.msb');
ok(run(app(["openssl", "x509", "-text", "-in", $pem, "-out", $out_utf8,
            "-nameopt", "utf8"])));
is(cmp_text($out_utf8, $utf),
   0, 'Comparing utf8 output with cyrillic.utf8');

SKIP: {
    skip "DES disabled", 1 if disabled("des");
    skip "Platform doesn't support command line UTF-8", 1 if $^O =~ /^(VMS|msys)$/;

    my $p12 = srctop_file("test", "shibboleth.pfx");
    my $p12pass = "σύνθημα γνώρισμα";
    my $out_pem = "out.pem";
    ok(run(app(["openssl", "x509", "-text", "-in", $p12, "-out", $out_pem,
                "-passin", "pass:$p12pass"])));
    # not unlinking $out_pem
}

ok(!run(app(["openssl", "x509", "-in", $pem, "-inform", "DER",
             "-out", $der, "-outform", "DER"])),
   "Checking failure of mismatching -inform DER");
ok(run(app(["openssl", "x509", "-in", $pem, "-inform", "PEM",
            "-out", $der, "-outform", "DER"])),
   "Conversion to DER");
ok(!run(app(["openssl", "x509", "-in", $der, "-inform", "PEM",
             "-out", $der2, "-outform", "DER"])),
   "Checking failure of mismatching -inform PEM");

# producing and checking self-issued (but not self-signed) cert
my $subj = "/CN=CA"; # using same DN as in issuer of ee-cert.pem
my $extfile = srctop_file("test", "v3_ca_exts.cnf");
my $pkey = srctop_file(@certs, "ca-key.pem"); # issuer private key
my $pubkey = "ca-pubkey.pem"; # the corresponding issuer public key
# use any (different) key for signing our self-issued cert:
my $signkey = srctop_file(@certs, "serverkey.pem");
my $selfout = "self-issued.out";
my $testcert = srctop_file(@certs, "ee-cert.pem");
ok(run(app(["openssl", "pkey", "-in", $pkey, "-pubout", "-out", $pubkey]))
&& run(app(["openssl", "x509", "-new", "-force_pubkey", $pubkey,
            "-subj", $subj, "-extfile", $extfile,
            "-signkey", $signkey, "-out", $selfout]))
&& run(app(["openssl", "verify", "-no_check_time",
            "-trusted", $selfout, "-partial_chain", $testcert])));
# not unlinking $pubkey
# not unlinking $selfout

subtest 'x509 -- x.509 v1 certificate' => sub {
    tconversion( -type => 'x509', -prefix => 'x509v1',
                 -in => srctop_file("test", "testx509.pem") );
};
subtest 'x509 -- first x.509 v3 certificate' => sub {
    tconversion( -type => 'x509', -prefix => 'x509v3-1',
                 -in => srctop_file("test", "v3-cert1.pem") );
};
subtest 'x509 -- second x.509 v3 certificate' => sub {
    tconversion( -type => 'x509', -prefix => 'x509v3-2',
                 -in => srctop_file("test", "v3-cert2.pem") );
};

subtest 'x509 -- pathlen' => sub {
    ok(run(test(["v3ext", srctop_file(@certs, "pathlen.pem")])));
};

cert_contains(srctop_file(@certs, "fake-gp.pem"),
              "2.16.528.1.1003.1.3.5.5.2-1-0000006666-Z-12345678-01.015-12345678",
              1, 'x500 -- subjectAltName');

sub test_errors { # actually tests diagnostics of OSSL_STORE
    my ($expected, $cert, @opts) = @_;
    my $infile = srctop_file(@certs, $cert);
    my @args = qw(openssl x509 -in);
    push(@args, $infile, @opts);
    my $tmpfile = 'out.txt';
    my $res =  grep(/-text/, @opts) ? run(app([@args], stdout => $tmpfile))
                                    : !run(app([@args], stderr => $tmpfile));
    my $found = 0;
    open(my $in, '<', $tmpfile) or die "Could not open file $tmpfile";
    while(<$in>) {
        print; # this may help debugging
        $res &&= !m/asn1 encoding/; # output must not include ASN.1 parse errors
        $found = 1 if m/$expected/; # output must include $expected
    }
    close $in;
    # $tmpfile is kept to help with investigation in case of failure
    return $res && $found;
}

# 3 tests for non-existence of spurious OSSL_STORE ASN.1 parse error output.
# This requires provoking a failure exit of the app after reading input files.
ok(test_errors("Bad output format", "root-cert.pem", '-outform', 'http'),
   "load root-cert errors");
ok(test_errors("RC2-40-CBC", "v3-certs-RC2.p12", '-passin', 'pass:v3-certs'),
   "load v3-certs-RC2 no asn1 errors"); # error msg should mention "RC2-40-CBC"
SKIP: {
    skip "sm2 not disabled", 1 if !disabled("sm2");

    ok(test_errors("Unable to load Public Key", "sm2.pem", '-text'),
       "error loading unsupported sm2 cert");
}

# 3 tests for -dateopts formats
ok(run(app(["openssl", "x509", "-noout", "-dates", "-dateopt", "rfc_822",
	     "-in", srctop_file("test/certs", "ca-cert.pem")])),
   "Run with rfc_8222 -dateopt format");
ok(run(app(["openssl", "x509", "-noout", "-dates", "-dateopt", "iso_8601",
	     "-in", srctop_file("test/certs", "ca-cert.pem")])),
   "Run with iso_8601 -dateopt format");
ok(!run(app(["openssl", "x509", "-noout", "-dates", "-dateopt", "invalid_format",
	     "-in", srctop_file("test/certs", "ca-cert.pem")])),
   "Run with invalid -dateopt format");

# extracts issuer from a -text formatted-output
sub get_issuer {
    my $f = shift(@_);
    my $issuer = "";
    open my $fh, $f or die;
    while (my $line = <$fh>) {
        if ($line =~ /Issuer:/) {
            $issuer = $line;
        }
    }
    close $fh;
    return $issuer;
}

# Tests for signing certs (broken in 1.1.1o)
my $a_key = "a-key.pem";
my $a_cert = "a-cert.pem";
my $a2_cert = "a2-cert.pem";
my $ca_key = "ca-key.pem";
my $ca_cert = "ca-cert.pem";
my $cnf = srctop_file('apps', 'openssl.cnf');

# Create cert A
ok(run(app(["openssl", "req", "-x509", "-newkey", "rsa:2048",
            "-config", $cnf,
            "-keyout", $a_key, "-out", $a_cert, "-days", "365",
            "-nodes", "-subj", "/CN=test.example.com"])));
# Create cert CA - note key size
ok(run(app(["openssl", "req", "-x509", "-newkey", "rsa:4096",
            "-config", $cnf,
            "-keyout", $ca_key, "-out", $ca_cert, "-days", "3650",
            "-nodes", "-subj", "/CN=ca.example.com"])));
# Sign cert A with CA (errors on 1.1.1o)
ok(run(app(["openssl", "x509", "-in", $a_cert, "-CA", $ca_cert,
            "-CAkey", $ca_key, "-set_serial", "1234567890",
            "-preserve_dates", "-sha256", "-text", "-out", $a2_cert])));
# verify issuer is CA
ok (get_issuer($a2_cert) =~ /CN = ca.example.com/);

my $in_csr = srctop_file('test', 'certs', 'x509-check.csr');
my $in_key = srctop_file('test', 'certs', 'x509-check-key.pem');
my $invextfile = srctop_file('test', 'invalid-x509.cnf');
# Test that invalid extensions settings fail
ok(!run(app(["openssl", "x509", "-req", "-in", $in_csr, "-signkey", $in_key,
            "-out", "/dev/null", "-days", "3650" , "-extensions", "ext",
            "-extfile", $invextfile])));

# Tests for issue #16080 (fixed in 1.1.1o)
my $b_key = "b-key.pem";
my $b_csr = "b-cert.csr";
my $b_cert = "b-cert.pem";
# Create the CSR
ok(run(app(["openssl", "req", "-new", "-newkey", "rsa:4096",
            "-keyout", $b_key, "-out", $b_csr, "-nodes",
            "-config", $cnf,
            "-subj", "/CN=b.example.com"])));
# Sign it - position of "-text" matters!
ok(run(app(["openssl", "x509", "-req", "-text", "-CAcreateserial",
            "-CA", $ca_cert, "-CAkey", $ca_key,
            "-in", $b_csr, "-out", $b_cert])));
# Verify issuer is CA
ok(get_issuer($b_cert) =~ /CN = ca.example.com/);
