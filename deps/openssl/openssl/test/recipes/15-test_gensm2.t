#! /usr/bin/env perl
# Copyright 2023-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


use strict;
use warnings;

use File::Spec;
use OpenSSL::Test qw(:DEFAULT pipe);
use OpenSSL::Test::Utils;

# These are special key generation tests for SM2 keys specifically,
# as they could be said to be a bit special in their encoding.
# This is an auxilliary test to 15-test_genec.t

setup("test_gensm2");

plan skip_all => "This test is unsupported in a no-sm2 build"
    if disabled("sm2");

plan tests => 2;

# According to the example in  GM/T 0015-2012, appendix D.2,
# generating an EC key with the named SM2 curve or generating
# an SM2 key should end up with the same encoding (apart from
# key private key field itself).  This regular expressions
# shows us what 'openssl asn1parse' should display.

my $sm2_re = qr|
   ^
   .*?\Qcons: SEQUENCE\E\s+?\R
   .*?\Qprim:  INTEGER           :00\E\R
   .*?\Qcons:  SEQUENCE\E\s+?\R
   .*?\Qprim:   OBJECT            :id-ecPublicKey\E\R
   .*?\Qprim:   OBJECT            :sm2\E\R
   .*?\Qprim:  OCTET STRING      [HEX DUMP]:\E
   |mx;

my $cmd_genec = app([ 'openssl', 'genpkey',
                      '-algorithm', 'EC',
                      '-pkeyopt', 'ec_paramgen_curve:SM2',
                      '-pkeyopt', 'ec_param_enc:named_curve' ]);
my $cmd_gensm2 = app([ 'openssl', 'genpkey', '-algorithm', 'SM2' ]);
my $cmd_asn1parse = app([ 'openssl', 'asn1parse', '-i' ]);

my $result_ec = join("", run(pipe($cmd_genec, $cmd_asn1parse),
                             capture => 1));

like($result_ec, $sm2_re,
     "Check that 'genpkey -algorithm EC' resulted in a correctly encoded SM2 key");

my $result_sm2 = join("", run(pipe($cmd_gensm2, $cmd_asn1parse),
                              capture => 1));

like($result_sm2, $sm2_re,
     "Check that 'genpkey -algorithm SM2' resulted in a correctly encoded SM2 key");
