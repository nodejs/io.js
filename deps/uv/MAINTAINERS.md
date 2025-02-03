# Project Maintainers

libuv is currently managed by the following individuals:

* **Ben Noordhuis** ([@bnoordhuis](https://github.com/bnoordhuis))
  - GPG key: D77B 1E34 243F BAF0 5F8E  9CC3 4F55 C8C8 46AB 89B9 (pubkey-bnoordhuis)
* **Colin Ihrig** ([@cjihrig](https://github.com/cjihrig))
  - GPG key: 94AE 3667 5C46 4D64 BAFA  68DD 7434 390B DBE9 B9C5 (pubkey-cjihrig)
  - GPG key: 5735 3E0D BDAA A7E8 39B6  6A1A FF47 D5E4 AD8B 4FDC (pubkey-cjihrig-kb)
* **Jameson Nash** ([@vtjnash](https://github.com/vtjnash))
  - GPG key: AEAD 0A4B 6867 6775 1A0E  4AEF 34A2 5FB1 2824 6514 (pubkey-vtjnash)
  - GPG key: CFBB 9CA9 A5BE AFD7 0E2B  3C5A 79A6 7C55 A367 9C8B (pubkey2022-vtjnash)
* **Jiawen Geng** ([@gengjiawen](https://github.com/gengjiawen))
* **Kaoru Takanashi** ([@erw7](https://github.com/erw7))
  - GPG Key: 5804 F999 8A92 2AFB A398  47A0 7183 5090 6134 887F (pubkey-erw7)
* **Richard Lau** ([@richardlau](https://github.com/richardlau))
  - GPG key: C82F A3AE 1CBE DC6B E46B  9360 C43C EC45 C17A B93C (pubkey-richardlau)
* **Santiago Gimeno** ([@santigimeno](https://github.com/santigimeno))
  - GPG key: 612F 0EAD 9401 6223 79DF  4402 F28C 3C8D A33C 03BE (pubkey-santigimeno)
* **Saúl Ibarra Corretgé** ([@saghul](https://github.com/saghul))
  - GPG key: FDF5 1936 4458 319F A823  3DC9 410E 5553 AE9B C059 (pubkey-saghul)
* **Trevor Norris** ([@trevnorris](https://github.com/trevnorris))
  - GPG key: AEFC 279A 0C93 0676 7E58  29A1 251C A676 820D C7F3 (pubkey-trevnorris)

## Project Maintainers emeriti

* **Anna Henningsen** ([@addaleax](https://github.com/addaleax))
* **Bartosz Sosnowski** ([@bzoz](https://github.com/bzoz))
* **Bert Belder** ([@piscisaureus](https://github.com/piscisaureus))
* **Fedor Indutny** ([@indutny](https://github.com/indutny))
  - GPG key: AF2E EA41 EC34 47BF DD86  FED9 D706 3CCE 19B7 E890 (pubkey-indutny)
* **Imran Iqbal** ([@imran-iq](https://github.com/imran-iq))
* **John Barboza** ([@jbarz](https://github.com/jbarz))

## Storing a maintainer key in Git

It's quite handy to store a maintainer's signature as a git blob, and have
that object tagged and signed with such key.

Export your public key:

    $ gpg --armor --export saghul@gmail.com > saghul.asc

Store it as a blob on the repo:

    $ git hash-object -w saghul.asc

The previous command returns a hash, copy it. For the sake of this explanation,
we'll assume it's 'abcd1234'. Storing the blob in git is not enough, it could
be garbage collected since nothing references it, so we'll create a tag for it:

    $ git tag -s pubkey-saghul abcd1234

Commit the changes and push:

    $ git push origin pubkey-saghul
