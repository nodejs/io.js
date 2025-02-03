#! /usr/bin/env perl
# Copyright 2017-2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use warnings;

use OpenSSL::Test qw(:DEFAULT data_file);
use OpenSSL::Test::Utils;
use File::Compare qw(compare_text);

setup('test_conf');

my %input_result = (
    'dollarid_on.cnf'  => 'dollarid_on.txt',
    'dollarid_off.cnf' => 'dollarid_off.txt',
    'oversized_line.cnf' => 'oversized_line.txt',
);

plan skip_all => 'This is unsupported for cross compiled configurations'
    if config('CROSS_COMPILE');

plan tests => 2 * scalar(keys %input_result);

foreach (sort keys %input_result) {
  SKIP: {
      my $input_path = data_file($_);
      my $expected_path = data_file($input_result{$_});
      my $result_path = "test_conf-$_-stdout";

      skip "Problem dumping $_", 1
          unless ok(run(test([ 'confdump', $input_path ],
                             stdout => $result_path)),
                    "dumping $_");
      is(compare_text($result_path, $expected_path, sub {
            my $in1 = $_[0];
            my $in2 = $_[1];
            $in1 =~ s/\r\n/\n/g;
            $in2 =~ s/\r\n/\n/g;
            $in1 ne $in2}), 0,
            "comparing the dump of $_ with $input_result{$_}");
    }
}
