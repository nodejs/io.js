Welcome to the OpenSSL Project
==============================

[![openssl logo]][www.openssl.org]

[![github actions ci badge]][github actions ci]
[![appveyor badge]][appveyor jobs]

OpenSSL is a robust, commercial-grade, full-featured Open Source Toolkit
for the Transport Layer Security (TLS) protocol formerly known as the
Secure Sockets Layer (SSL) protocol. The protocol implementation is based
on a full-strength general purpose cryptographic library, which can also
be used stand-alone.

OpenSSL is descended from the SSLeay library developed by Eric A. Young
and Tim J. Hudson.

The official Home Page of the OpenSSL Project is [www.openssl.org].

Table of Contents
=================

 - [Overview](#overview)
 - [Download](#download)
 - [Build and Install](#build-and-install)
 - [Documentation](#documentation)
 - [License](#license)
 - [Support](#support)
 - [Contributing](#contributing)
 - [Legalities](#legalities)

Overview
========

The OpenSSL toolkit includes:

- **libssl**
  an implementation of all TLS protocol versions up to TLSv1.3 ([RFC 8446]).

- **libcrypto**
  a full-strength general purpose cryptographic library. It constitutes the
  basis of the TLS implementation, but can also be used independently.

- **openssl**
  the OpenSSL command line tool, a swiss army knife for cryptographic tasks,
  testing and analyzing. It can be used for
  - creation of key parameters
  - creation of X.509 certificates, CSRs and CRLs
  - calculation of message digests
  - encryption and decryption
  - SSL/TLS client and server tests
  - handling of S/MIME signed or encrypted mail
  - and more...

Download
========

For Production Use
------------------

Source code tarballs of the official releases can be downloaded from
[www.openssl.org/source](https://www.openssl.org/source).
The OpenSSL project does not distribute the toolkit in binary form.

However, for a large variety of operating systems precompiled versions
of the OpenSSL toolkit are available. In particular on Linux and other
Unix operating systems it is normally recommended to link against the
precompiled shared libraries provided by the distributor or vendor.

For Testing and Development
---------------------------

Although testing and development could in theory also be done using
the source tarballs, having a local copy of the git repository with
the entire project history gives you much more insight into the
code base.

The official OpenSSL Git Repository is located at [git.openssl.org].
There is a GitHub mirror of the repository at [github.com/openssl/openssl],
which is updated automatically from the former on every commit.

A local copy of the Git Repository can be obtained by cloning it from
the original OpenSSL repository using

    git clone git://git.openssl.org/openssl.git

or from the GitHub mirror using

    git clone https://github.com/openssl/openssl.git

If you intend to contribute to OpenSSL, either to fix bugs or contribute
new features, you need to fork the OpenSSL repository openssl/openssl on
GitHub and clone your public fork instead.

    git clone https://github.com/yourname/openssl.git

This is necessary, because all development of OpenSSL nowadays is done via
GitHub pull requests. For more details, see [Contributing](#contributing).

Build and Install
=================

After obtaining the Source, have a look at the [INSTALL](INSTALL.md) file for
detailed instructions about building and installing OpenSSL. For some
platforms, the installation instructions are amended by a platform specific
document.

 * [Notes for UNIX-like platforms](NOTES-UNIX.md)
 * [Notes for Android platforms](NOTES-ANDROID.md)
 * [Notes for Windows platforms](NOTES-WINDOWS.md)
 * [Notes for the DOS platform with DJGPP](NOTES-DJGPP.md)
 * [Notes for the OpenVMS platform](NOTES-VMS.md)
 * [Notes on Perl](NOTES-PERL.md)
 * [Notes on Valgrind](NOTES-VALGRIND.md)

Specific notes on upgrading to OpenSSL 3.0 from previous versions can be found
in the [migration_guide(7ossl)] manual page.

Documentation
=============

Manual Pages
------------

The manual pages for the master branch and all current stable releases are
available online.

- [OpenSSL master](https://www.openssl.org/docs/manmaster)
- [OpenSSL 3.0](https://www.openssl.org/docs/man3.0)
- [OpenSSL 1.1.1](https://www.openssl.org/docs/man1.1.1)

Wiki
----

There is a Wiki at [wiki.openssl.org] which is currently not very active.
It contains a lot of useful information, not all of which is up to date.

License
=======

OpenSSL is licensed under the Apache License 2.0, which means that
you are free to get and use it for commercial and non-commercial
purposes as long as you fulfill its conditions.

See the [LICENSE.txt](LICENSE.txt) file for more details.

Support
=======

There are various ways to get in touch. The correct channel depends on
your requirement. see the [SUPPORT](SUPPORT.md) file for more details.

Contributing
============

If you are interested and willing to contribute to the OpenSSL project,
please take a look at the [CONTRIBUTING](CONTRIBUTING.md) file.

Legalities
==========

A number of nations restrict the use or export of cryptography. If you are
potentially subject to such restrictions you should seek legal advice before
attempting to develop or distribute cryptographic code.

Copyright
=========

Copyright (c) 1998-2024 The OpenSSL Project

Copyright (c) 1995-1998 Eric A. Young, Tim J. Hudson

All rights reserved.

<!-- Links  -->

[www.openssl.org]:
    <https://www.openssl.org>
    "OpenSSL Homepage"

[git.openssl.org]:
    <https://git.openssl.org>
    "OpenSSL Git Repository"

[git.openssl.org]:
    <https://git.openssl.org>
    "OpenSSL Git Repository"

[github.com/openssl/openssl]:
    <https://github.com/openssl/openssl>
    "OpenSSL GitHub Mirror"

[wiki.openssl.org]:
    <https://wiki.openssl.org>
    "OpenSSL Wiki"

[migration_guide(7ossl)]:
    <https://www.openssl.org/docs/man3.0/man7/migration_guide.html>
    "OpenSSL Migration Guide"

[RFC 8446]:
     <https://tools.ietf.org/html/rfc8446>

<!-- Logos and Badges -->

[openssl logo]:
    doc/images/openssl.svg
    "OpenSSL Logo"

[github actions ci badge]:
    <https://github.com/openssl/openssl/workflows/GitHub%20CI/badge.svg>
    "GitHub Actions CI Status"

[github actions ci]:
    <https://github.com/openssl/openssl/actions?query=workflow%3A%22GitHub+CI%22>
    "GitHub Actions CI"

[appveyor badge]:
    <https://ci.appveyor.com/api/projects/status/8e10o7xfrg73v98f/branch/master?svg=true>
    "AppVeyor Build Status"

[appveyor jobs]:
    <https://ci.appveyor.com/project/openssl/openssl/branch/master>
    "AppVeyor Jobs"
