#! /usr/bin/env perl
# Copyright 2024 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use OpenSSL::Test qw/:DEFAULT cmdstr srctop_file/;
use OpenSSL::Test::Utils;

use TLSProxy::Proxy;

my $test_name = "test_npn";
setup($test_name);

plan skip_all => "TLSProxy isn't usable on $^O"
    if $^O =~ /^(VMS)$/;

plan skip_all => "$test_name needs the dynamic engine feature enabled"
    if disabled("engine") || disabled("dynamic-engine");

plan skip_all => "$test_name needs the sock feature enabled"
    if disabled("sock");

plan skip_all => "$test_name needs NPN enabled"
    if disabled("nextprotoneg");

plan skip_all => "$test_name needs TLSv1.2 enabled"
    if disabled("tls1_2");

my $proxy = TLSProxy::Proxy->new(
    undef,
    cmdstr(app(["openssl"]), display => 1),
    srctop_file("apps", "server.pem"),
    (!$ENV{HARNESS_ACTIVE} || $ENV{HARNESS_VERBOSE})
);

$proxy->start() or plan skip_all => "Unable to start up Proxy for tests";
plan tests => 1;

my $npnseen = 0;

# Test 1: Check sending an empty NextProto message from the client works. This is
#         valid as per the spec, but OpenSSL does not allow you to send it.
#         Therefore we must be prepared to receive such a message but we cannot
#         generate it except via TLSProxy
$proxy->clear();
$proxy->filter(\&npn_filter);
$proxy->clientflags("-nextprotoneg foo -no_tls1_3");
$proxy->serverflags("-nextprotoneg foo");
$proxy->start();
ok($npnseen && TLSProxy::Message->success(), "Empty NPN message");

sub npn_filter
{
    my $proxy = shift;
    my $message;

    # The NextProto message always appears in flight 2
    return if $proxy->flight != 2;

    foreach my $message (@{$proxy->message_list}) {
        if ($message->mt == TLSProxy::Message::MT_NEXT_PROTO) {
            # Our TLSproxy NextProto message support doesn't support parsing of
            # the message. If we repack it just creates an empty NextProto
            # message - which is exactly the scenario we want to test here.
            $message->repack();
            $npnseen = 1;
        }
    }
}
