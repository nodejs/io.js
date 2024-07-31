#! /usr/bin/env perl
# Copyright 2021 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


use OpenSSL::Test::Simple;
use OpenSSL::Test qw/:DEFAULT srctop_file srctop_dir bldtop_dir/;
use OpenSSL::Test::Utils;

BEGIN {
setup("test_prov_config");
}

use lib srctop_dir('Configurations');
use lib bldtop_dir('.');

my $no_fips = disabled('fips') || ($ENV{NO_FIPS} // 0);

plan tests => 2;

ok(run(test(["prov_config_test", srctop_file("test", "default.cnf"),
                                 srctop_file("test", "recursive.cnf")])),
    "running prov_config_test default.cnf");

SKIP: {
    skip "Skipping FIPS test in this build", 1 if $no_fips;

    ok(run(test(["prov_config_test", srctop_file("test", "fips.cnf"),
                                     srctop_file("test", "recursive.cnf")])),
       "running prov_config_test fips.cnf");
}
