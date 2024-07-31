#! /usr/bin/env perl
# Copyright 2023-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


use OpenSSL::Test::Simple;
use OpenSSL::Test qw/:DEFAULT srctop_file srctop_dir bldtop_dir bldtop_file data_dir/;
use OpenSSL::Test::Utils;
use Cwd qw(abs_path);

BEGIN {
setup("test_asn1_stable_parse");
}
my $config_path = srctop_file("test", "recipes", "04-test_asn1_stable_parse_data", "asn1_stable_parse.cnf");

plan tests => 1;

ok(run(test(["asn1_stable_parse_test", "-config", $config_path])),
   "Confirm that malformed entries in stable section are not parsed");

