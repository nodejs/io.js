#! /usr/bin/env perl
# Copyright 2016-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use OpenSSL::Test qw/:DEFAULT srctop_dir bldtop_dir/;
use OpenSSL::Test::Utils;

#Load configdata.pm

BEGIN {
    setup("test_shlibload");
}
use lib srctop_dir('Configurations');
use lib bldtop_dir('.');
use platform;

plan skip_all => "Test only supported in a shared build" if disabled("shared");
plan skip_all => "Test is disabled on AIX" if config('target') =~ m|^aix|;
plan skip_all => "Test is disabled on NonStop" if config('target') =~ m|^nonstop|;
plan skip_all => "Test only supported in a dso build" if disabled("dso");
plan skip_all => "Test is disabled in an address sanitizer build" unless disabled("asan");
plan skip_all => "Test is disabled in no-atexit build" if disabled("atexit");

plan tests => 10;

my $libcrypto = platform->sharedlib('libcrypto');
my $libssl = platform->sharedlib('libssl');
my $atexit_outfile;

$atexit_outfile = 'atexit-cryptofirst.txt';
1 while unlink $atexit_outfile;
ok(run(test(["shlibloadtest", "-crypto_first", $libcrypto, $libssl, $atexit_outfile])),
   "running shlibloadtest -crypto_first $atexit_outfile");
ok(check_atexit($atexit_outfile));

$atexit_outfile = 'atexit-sslfirst.txt';
1 while unlink $atexit_outfile;
ok(run(test(["shlibloadtest", "-ssl_first", $libcrypto, $libssl, $atexit_outfile])),
   "running shlibloadtest -ssl_first $atexit_outfile");
ok(check_atexit($atexit_outfile));

$atexit_outfile = 'atexit-justcrypto.txt';
1 while unlink $atexit_outfile;
ok(run(test(["shlibloadtest", "-just_crypto", $libcrypto, $libssl, $atexit_outfile])),
   "running shlibloadtest -just_crypto $atexit_outfile");
ok(check_atexit($atexit_outfile));

$atexit_outfile = 'atexit-dsoref.txt';
1 while unlink $atexit_outfile;
ok(run(test(["shlibloadtest", "-dso_ref", $libcrypto, $libssl, $atexit_outfile])),
   "running shlibloadtest -dso_ref $atexit_outfile");
ok(check_atexit($atexit_outfile));

$atexit_outfile = 'atexit-noatexit.txt';
1 while unlink $atexit_outfile;
ok(run(test(["shlibloadtest", "-no_atexit", $libcrypto, $libssl, $atexit_outfile])),
   "running shlibloadtest -no_atexit $atexit_outfile");
ok(!check_atexit($atexit_outfile));

sub check_atexit {
    my $filename = shift;

    open my $fh, '<', $filename;
    return 0 unless defined $fh;

    my $data = <$fh>;

    return 1 if (defined $data && $data =~ m/atexit\(\) run/);

    return 0;
}
