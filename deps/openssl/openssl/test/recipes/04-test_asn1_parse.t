#! /usr/bin/env perl
# Copyright 2023 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use OpenSSL::Test qw(:DEFAULT srctop_file);
use OpenSSL::Test::Utils;

setup("test_asn1_parse");

plan tests => 3;

$ENV{OPENSSL_CONF} = srctop_file("test", "test_asn1_parse.cnf");

ok(run(app(([ 'openssl', 'asn1parse',
              '-genstr', 'OID:1.2.3.4.1']))));

ok(run(app(([ 'openssl', 'asn1parse',
              '-genstr', 'OID:1.2.3.4.2']))));

ok(run(app(([ 'openssl', 'asn1parse',
              '-genstr', 'OID:1.2.3.4.3']))));
