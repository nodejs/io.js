#! /usr/bin/env perl
# Copyright 2016-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use warnings;

use OpenSSL::Test qw/:DEFAULT srctop_file with/;
use OpenSSL::Test::Utils;

use Encode;

setup("test_pkcs12");

my $pass = "σύνθημα γνώρισμα";

my $savedcp;
if (eval { require Win32::API; 1; }) {
    # Trouble is that Win32 perl uses CreateProcessA, which
    # makes it problematic to pass non-ASCII arguments, from perl[!]
    # that is. This is because CreateProcessA is just a wrapper for
    # CreateProcessW and will call MultiByteToWideChar and use
    # system default locale. Since we attempt Greek pass-phrase
    # conversion can be done only with Greek locale.

    Win32::API->Import("kernel32","UINT GetSystemDefaultLCID()");
    if (GetSystemDefaultLCID() != 0x408) {
        plan skip_all => "Non-Greek system locale";
    } else {
        # Ensure correct code page so that VERBOSE output is right.
        Win32::API->Import("kernel32","UINT GetConsoleOutputCP()");
        Win32::API->Import("kernel32","BOOL SetConsoleOutputCP(UINT cp)");
        $savedcp = GetConsoleOutputCP();
        SetConsoleOutputCP(1253);
        $pass = Encode::encode("cp1253",Encode::decode("utf-8",$pass));
    }
} elsif ($^O eq "MSWin32") {
    plan skip_all => "Win32::API unavailable";
} elsif ($^O ne "VMS") {
    # Running MinGW tests transparently under Wine apparently requires
    # UTF-8 locale...

    foreach(`locale -a`) {
        s/\R$//;
        if ($_ =~ m/^C\.UTF\-?8/i) {
            $ENV{LC_ALL} = $_;
            last;
        }
    }
}
$ENV{OPENSSL_WIN32_UTF8}=1;

plan tests => 17;

# Test different PKCS#12 formats
ok(run(test(["pkcs12_format_test"])), "test pkcs12 formats");
# Test with legacy APIs
ok(run(test(["pkcs12_format_test", "-legacy"])), "test pkcs12 formats using legacy APIs");
# Test with a non-default library context (and no loaded providers in the default context)
ok(run(test(["pkcs12_format_test", "-context"])), "test pkcs12 formats using a non-default library context");

SKIP: {
     skip "VMS doesn't have command line UTF-8 support yet in DCL", 1
         if $^O eq "VMS";

     # just see that we can read shibboleth.pfx protected with $pass
     ok(run(app(["openssl", "pkcs12", "-noout",
                 "-password", "pass:$pass",
                 "-in", srctop_file("test", "shibboleth.pfx")])),
        "test_load_cert_pkcs12");
}

my @path = qw(test certs);
my $outfile1 = "out1.p12";
my $outfile2 = "out2.p12";
my $outfile3 = "out3.p12";
my $outfile4 = "out4.p12";
my $outfile5 = "out5.p12";

# Test the -chain option with -untrusted
ok(run(app(["openssl", "pkcs12", "-export", "-chain",
            "-CAfile",  srctop_file(@path,  "sroot-cert.pem"),
            "-untrusted", srctop_file(@path, "ca-cert.pem"),
            "-in", srctop_file(@path, "ee-cert.pem"),
            "-nokeys", "-passout", "pass:", "-out", $outfile1])),
   "test_pkcs12_chain_untrusted");

# Test the -passcerts option
SKIP: {
    skip "Skipping PKCS#12 test because DES is disabled in this build", 1
        if disabled("des");
    ok(run(app(["openssl", "pkcs12", "-export",
            "-in", srctop_file(@path, "ee-cert.pem"),
            "-certfile", srctop_file(@path, "v3-certs-TDES.p12"),
            "-passcerts", "pass:v3-certs",
            "-nokeys", "-passout", "pass:v3-certs", "-descert",
            "-out", $outfile2])),
   "test_pkcs12_passcerts");
}

SKIP: {
    skip "Skipping legacy PKCS#12 test because the required algorithms are disabled", 1
        if disabled("des") || disabled("rc2") || disabled("legacy");
    # Test reading legacy PKCS#12 file
    ok(run(app(["openssl", "pkcs12", "-export",
                "-in", srctop_file(@path, "v3-certs-RC2.p12"),
                "-passin", "pass:v3-certs",
                "-provider", "default", "-provider", "legacy",
                "-nokeys", "-passout", "pass:v3-certs", "-descert",
                "-out", $outfile3])),
    "test_pkcs12_passcerts_legacy");
}

# Test export of PEM file with both cert and key
# -nomac necessary to avoid legacy provider requirement
ok(run(app(["openssl", "pkcs12", "-export",
        "-inkey", srctop_file(@path, "cert-key-cert.pem"),
        "-in", srctop_file(@path, "cert-key-cert.pem"),
        "-passout", "pass:v3-certs",
        "-nomac", "-out", $outfile4], stderr => "outerr.txt")),
   "test_export_pkcs12_cert_key_cert");
open DATA, "outerr.txt";
my @match = grep /:error:/, <DATA>;
close DATA;
ok(scalar @match > 0 ? 0 : 1, "test_export_pkcs12_outerr_empty");

ok(run(app(["openssl", "pkcs12",
            "-in", $outfile4,
            "-passin", "pass:v3-certs",
            "-nomacver", "-nodes"])),
  "test_import_pkcs12_cert_key_cert");

ok(run(app(["openssl", "pkcs12", "-export", "-out", $outfile5,
            "-in", srctop_file(@path, "ee-cert.pem"), "-caname", "testname",
            "-nokeys", "-passout", "pass:", "-certpbe", "NONE"])),
   "test nokeys single cert");

my @pkcs12info = run(app(["openssl", "pkcs12", "-info", "-in", $outfile5,
                          "-passin", "pass:"]), capture => 1);

# Test that with one input certificate, we get one output certificate
ok(grep(/subject=CN = server.example/, @pkcs12info) == 1,
   "test one cert in output");
# Test that the expected friendly name is present in the output
ok(grep(/testname/, @pkcs12info) == 1, "test friendly name in output");

# Test some bad pkcs12 files
my $bad1 = srctop_file("test", "recipes", "80-test_pkcs12_data", "bad1.p12");
my $bad2 = srctop_file("test", "recipes", "80-test_pkcs12_data", "bad2.p12");
my $bad3 = srctop_file("test", "recipes", "80-test_pkcs12_data", "bad3.p12");

with({ exit_checker => sub { return shift == 1; } },
     sub {
        ok(run(app(["openssl", "pkcs12", "-in", $bad1, "-password", "pass:"])),
           "test bad pkcs12 file 1");

        ok(run(app(["openssl", "pkcs12", "-in", $bad1, "-password", "pass:",
                    "-nomacver"])),
           "test bad pkcs12 file 1 (nomacver)");

        ok(run(app(["openssl", "pkcs12", "-in", $bad2, "-password", "pass:"])),
           "test bad pkcs12 file 2");

        ok(run(app(["openssl", "pkcs12", "-in", $bad3, "-password", "pass:"])),
           "test bad pkcs12 file 3");
     });

SetConsoleOutputCP($savedcp) if (defined($savedcp));
