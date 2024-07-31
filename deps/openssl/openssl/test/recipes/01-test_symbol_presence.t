#! /usr/bin/env perl
# -*- mode: Perl -*-
# Copyright 2016-2021 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use File::Spec::Functions qw(devnull);
use OpenSSL::Test qw(:DEFAULT srctop_file srctop_dir bldtop_dir bldtop_file);
use OpenSSL::Test::Utils;

BEGIN {
    setup("test_symbol_presence");
}

use lib srctop_dir('Configurations');
use lib bldtop_dir('.');
use platform;

plan skip_all => "Test is disabled on NonStop" if config('target') =~ m|^nonstop|;
# MacOS arranges symbol names differently
plan skip_all => "Test is disabled on MacOS" if config('target') =~ m|^darwin|;
plan skip_all => "This is unsupported on MSYS, MinGW or MSWin32"
    if $^O eq 'msys' or $^O eq 'MSWin32' or config('target') =~ m|^mingw|;
plan skip_all => "Only useful when building shared libraries"
    if disabled("shared");

my @libnames = ("crypto", "ssl");
my $testcount = scalar @libnames;

plan tests => $testcount * 2;

note
    "NOTE: developer test!  It's possible that it won't run on your\n",
    "platform, and that's perfectly fine.  This is mainly for developers\n",
    "on Unix to check that our shared libraries are consistent with the\n",
    "ordinals (util/*.num in the source tree), something that should be\n",
    "good enough a check for the other platforms as well.\n";

foreach my $libname (@libnames) {
 SKIP:
    {
        my $shlibname = platform->sharedlib("lib$libname");
        my $shlibpath = bldtop_file($shlibname);
        *OSTDERR = *STDERR;
        *OSTDOUT = *STDOUT;
        open STDERR, ">", devnull();
        open STDOUT, ">", devnull();
        my @nm_lines = map { s|\R$||; $_ } `nm -DPg $shlibpath 2> /dev/null`;
        close STDERR;
        close STDOUT;
        *STDERR = *OSTDERR;
        *STDOUT = *OSTDOUT;
        skip "Can't run 'nm -DPg $shlibpath' => $?...  ignoring", 2
            unless $? == 0;

        my $bldtop = bldtop_dir();
        my @def_lines;
        indir $bldtop => sub {
            my $mkdefpath = srctop_file("util", "mkdef.pl");
            my $libnumpath = srctop_file("util", "lib$libname.num");
            @def_lines = map { s|\R$||; $_ } `$^X $mkdefpath --ordinals $libnumpath --name $libname --OS linux 2> /dev/null`;
            ok($? == 0, "running 'cd $bldtop; $^X $mkdefpath --ordinals $libnumpath --name $libname --OS linux' => $?");
        }, create => 0, cleanup => 0;

        note "Number of lines in \@nm_lines before massaging: ", scalar @nm_lines;
        note "Number of lines in \@def_lines before massaging: ", scalar @def_lines;

        # Massage the nm output to only contain defined symbols
        # Common symbols need separate treatment
        my %commons;
        foreach (@nm_lines) {
            if (m|^(.*) C .*|) {
                $commons{$1}++;
            }
        }
        foreach (sort keys %commons) {
            note "Common symbol: $_";
        }

        @nm_lines =
            sort
            ( map {
                  # Drop the first space and everything following it
                  s| .*||;
                  # Drop OpenSSL dynamic version information if there is any
                  s|\@\@.+$||;
                  # Return the result
                  $_
              }
              # Drop any symbol starting with a double underscore, they
              # are reserved for the compiler / system ABI and are none
              # of our business
              grep !m|^__|,
              # Only look at external definitions
              grep m|.* [BDST] .*|,
              @nm_lines ),
            keys %commons;

        # Massage the mkdef.pl output to only contain global symbols
        # The output we got is in Unix .map format, which has a global
        # and a local section.  We're only interested in the global
        # section.
        my $in_global = 0;
        @def_lines =
            sort
            map { s|;||; s|\s+||g; $_ }
            grep { $in_global = 1 if m|global:|;
                   $in_global = 0 if m|local:|;
                   $in_global = 0 if m|\}|;
                   $in_global && m|;|; } @def_lines;

        note "Number of lines in \@nm_lines after massaging: ", scalar @nm_lines;
        note "Number of lines in \@def_lines after massaging: ", scalar @def_lines;

        # Maintain lists of symbols that are missing in the shared library,
        # or that are extra.
        my @missing = ();
        my @extra = ();

        while (scalar @nm_lines || scalar @def_lines) {
            my $nm_first = $nm_lines[0];
            my $def_first = $def_lines[0];

            if (!defined($nm_first)) {
                push @missing, shift @def_lines;
            } elsif (!defined($def_first)) {
                push @extra, shift @nm_lines;
            } elsif ($nm_first gt $def_first) {
                push @missing, shift @def_lines;
            } elsif ($nm_first lt $def_first) {
                push @extra, shift @nm_lines;
            } else {
                shift @def_lines;
                shift @nm_lines;
            }
        }

        if (scalar @missing) {
            note "The following symbols are missing in ${shlibname}:";
            foreach (@missing) {
                note "  $_";
            }
        }
        if (scalar @extra) {
            note "The following symbols are extra in ${shlibname}:";
            foreach (@extra) {
                note "  $_";
            }
        }
        ok(scalar @missing == 0,
           "check that there are no missing symbols in ${shlibname}");
    }
}
