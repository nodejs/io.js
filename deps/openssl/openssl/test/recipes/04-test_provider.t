#! /usr/bin/env perl
# Copyright 2019-2021 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use OpenSSL::Test qw(:DEFAULT bldtop_dir);
use OpenSSL::Test::Utils;

setup("test_provider");

plan tests => 3;

ok(run(test(['provider_test'])), "provider_test");

$ENV{"OPENSSL_MODULES"} = bldtop_dir("test");

ok(run(test(['provider_test', '-loaded'])), "provider_test -loaded");

 SKIP: {
     skip "no module support", 1 if disabled("module");

     ok(run(app(['openssl', 'list', '-provider', 'p_minimal',
                 '-providers', '-verbose'])));
}
