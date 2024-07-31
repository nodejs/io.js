# Node.js

Node.js is an open-source, cross-platform JavaScript runtime environment.

For information on using Node.js, see the [Node.js website][].

The Node.js project uses an [open governance model](./GOVERNANCE.md). The
[OpenJS Foundation][] provides support for the project.

Contributors are expected to act in a collaborative manner to move
the project forward. We encourage the constructive exchange of contrary
opinions and compromise. The [TSC](./GOVERNANCE.md#technical-steering-committee)
reserves the right to limit or block contributors who repeatedly act in ways
that discourage, exhaust, or otherwise negatively affect other participants.

**This project has a [Code of Conduct][].**

## Table of contents

* [Support](#support)
* [Release types](#release-types)
  * [Download](#download)
    * [Current and LTS releases](#current-and-lts-releases)
    * [Nightly releases](#nightly-releases)
    * [API documentation](#api-documentation)
  * [Verifying binaries](#verifying-binaries)
* [Building Node.js](#building-nodejs)
* [Security](#security)
* [Contributing to Node.js](#contributing-to-nodejs)
* [Current project team members](#current-project-team-members)
  * [TSC (Technical Steering Committee)](#tsc-technical-steering-committee)
  * [Collaborators](#collaborators)
  * [Triagers](#triagers)
  * [Release keys](#release-keys)
* [License](#license)

## Support

Looking for help? Check out the
[instructions for getting support](.github/SUPPORT.md).

## Release types

* **Current**: Under active development. Code for the Current release is in the
  branch for its major version number (for example,
  [v19.x](https://github.com/nodejs/node/tree/v19.x)). Node.js releases a new
  major version every 6 months, allowing for breaking changes. This happens in
  April and October every year. Releases appearing each October have a support
  life of 8 months. Releases appearing each April convert to LTS (see below)
  each October.
* **LTS**: Releases that receive Long Term Support, with a focus on stability
  and security. Every even-numbered major version will become an LTS release.
  LTS releases receive 12 months of _Active LTS_ support and a further 18 months
  of _Maintenance_. LTS release lines have alphabetically-ordered code names,
  beginning with v4 Argon. There are no breaking changes or feature additions,
  except in some special circumstances.
* **Nightly**: Code from the Current branch built every 24-hours when there are
  changes. Use with caution.

Current and LTS releases follow [semantic versioning](https://semver.org). A
member of the Release Team [signs](#release-keys) each Current and LTS release.
For more information, see the
[Release README](https://github.com/nodejs/Release#readme).

### Download

Binaries, installers, and source tarballs are available at
<https://nodejs.org/en/download/>.

#### Current and LTS releases

<https://nodejs.org/download/release/>

The [latest](https://nodejs.org/download/release/latest/) directory is an
alias for the latest Current release. The latest-_codename_ directory is an
alias for the latest release from an LTS line. For example, the
[latest-hydrogen](https://nodejs.org/download/release/latest-hydrogen/)
directory contains the latest Hydrogen (Node.js 18) release.

#### Nightly releases

<https://nodejs.org/download/nightly/>

Each directory name and filename contains a date (in UTC) and the commit
SHA at the HEAD of the release.

#### API documentation

Documentation for the latest Current release is at <https://nodejs.org/api/>.
Version-specific documentation is available in each release directory in the
_docs_ subdirectory. Version-specific documentation is also at
<https://nodejs.org/download/docs/>.

### Verifying binaries

Download directories contain a `SHASUMS256.txt` file with SHA checksums for the
files.

To download `SHASUMS256.txt` using `curl`:

```bash
curl -O https://nodejs.org/dist/vx.y.z/SHASUMS256.txt
```

To check that a downloaded file matches the checksum, run
it through `sha256sum` with a command such as:

```bash
grep node-vx.y.z.tar.gz SHASUMS256.txt | sha256sum -c -
```

For Current and LTS, the GPG detached signature of `SHASUMS256.txt` is in
`SHASUMS256.txt.sig`. You can use it with `gpg` to verify the integrity of
`SHASUMS256.txt`. You will first need to import
[the GPG keys of individuals authorized to create releases](#release-keys). To
import the keys:

```bash
gpg --keyserver hkps://keys.openpgp.org --recv-keys 4ED778F539E3634C779C87C6D7062848A1AB005C
```

See [Release keys](#release-keys) for a script to import active release keys.

Next, download the `SHASUMS256.txt.sig` for the release:

```bash
curl -O https://nodejs.org/dist/vx.y.z/SHASUMS256.txt.sig
```

Then use `gpg --verify SHASUMS256.txt.sig SHASUMS256.txt` to verify
the file's signature.

## Building Node.js

See [BUILDING.md](BUILDING.md) for instructions on how to build Node.js from
source and a list of supported platforms.

## Security

For information on reporting security vulnerabilities in Node.js, see
[SECURITY.md](./SECURITY.md).

## Contributing to Node.js

* [Contributing to the project][]
* [Working Groups][]
* [Strategic initiatives][]
* [Technical values and prioritization][]

## Current project team members

For information about the governance of the Node.js project, see
[GOVERNANCE.md](./GOVERNANCE.md).

<!-- node-core-utils and find-inactive-tsc.mjs depend on the format of the TSC
     list. If the format changes, those utilities need to be tested and
     updated. -->

### TSC (Technical Steering Committee)

#### TSC voting members

<!--lint disable prohibited-strings-->

* [aduh95](https://github.com/aduh95) -
  **Antoine du Hamel** <<duhamelantoine1995@gmail.com>> (he/him)
* [anonrig](https://github.com/anonrig) -
  **Yagiz Nizipli** <<yagiz@nizipli.com>> (he/him)
* [benjamingr](https://github.com/benjamingr) -
  **Benjamin Gruenbaum** <<benjamingr@gmail.com>>
* [BridgeAR](https://github.com/BridgeAR) -
  **Ruben Bridgewater** <<ruben@bridgewater.de>> (he/him)
* [gireeshpunathil](https://github.com/gireeshpunathil) -
  **Gireesh Punathil** <<gpunathi@in.ibm.com>> (he/him)
* [jasnell](https://github.com/jasnell) -
  **James M Snell** <<jasnell@gmail.com>> (he/him)
* [joyeecheung](https://github.com/joyeecheung) -
  **Joyee Cheung** <<joyeec9h3@gmail.com>> (she/her)
* [legendecas](https://github.com/legendecas) -
  **Chengzhong Wu** <<legendecas@gmail.com>> (he/him)
* [marco-ippolito](https://github.com/marco-ippolito) -
  **Marco Ippolito** <<marcoippolito54@gmail.com>> (he/him)
* [mcollina](https://github.com/mcollina) -
  **Matteo Collina** <<matteo.collina@gmail.com>> (he/him)
* [mhdawson](https://github.com/mhdawson) -
  **Michael Dawson** <<midawson@redhat.com>> (he/him)
* [MoLow](https://github.com/MoLow) -
  **Moshe Atlow** <<moshe@atlow.co.il>> (he/him)
* [RafaelGSS](https://github.com/RafaelGSS) -
  **Rafael Gonzaga** <<rafael.nunu@hotmail.com>> (he/him)
* [richardlau](https://github.com/richardlau) -
  **Richard Lau** <<rlau@redhat.com>>
* [ronag](https://github.com/ronag) -
  **Robert Nagy** <<ronagy@icloud.com>>
* [ruyadorno](https://github.com/ruyadorno) -
  **Ruy Adorno** <<ruy@vlt.sh>> (he/him)
* [ShogunPanda](https://github.com/ShogunPanda) -
  **Paolo Insogna** <<paolo@cowtech.it>> (he/him)
* [targos](https://github.com/targos) -
  **Michaël Zasso** <<targos@protonmail.com>> (he/him)
* [tniessen](https://github.com/tniessen) -
  **Tobias Nießen** <<tniessen@tnie.de>> (he/him)

#### TSC regular members

* [apapirovski](https://github.com/apapirovski) -
  **Anatoli Papirovski** <<apapirovski@mac.com>> (he/him)
* [BethGriggs](https://github.com/BethGriggs) -
  **Beth Griggs** <<bethanyngriggs@gmail.com>> (she/her)
* [bnoordhuis](https://github.com/bnoordhuis) -
  **Ben Noordhuis** <<info@bnoordhuis.nl>>
* [cjihrig](https://github.com/cjihrig) -
  **Colin Ihrig** <<cjihrig@gmail.com>> (he/him)
* [codebytere](https://github.com/codebytere) -
  **Shelley Vohr** <<shelley.vohr@gmail.com>> (she/her)
* [GeoffreyBooth](https://github.com/GeoffreyBooth) -
  **Geoffrey Booth** <<webadmin@geoffreybooth.com>> (he/him)
* [Trott](https://github.com/Trott) -
  **Rich Trott** <<rtrott@gmail.com>> (he/him)

<details>

<summary>TSC emeriti members</summary>

#### TSC emeriti members

* [addaleax](https://github.com/addaleax) -
  **Anna Henningsen** <<anna@addaleax.net>> (she/her)
* [ChALkeR](https://github.com/ChALkeR) -
  **Сковорода Никита Андреевич** <<chalkerx@gmail.com>> (he/him)
* [chrisdickinson](https://github.com/chrisdickinson) -
  **Chris Dickinson** <<christopher.s.dickinson@gmail.com>>
* [danbev](https://github.com/danbev) -
  **Daniel Bevenius** <<daniel.bevenius@gmail.com>> (he/him)
* [danielleadams](https://github.com/danielleadams) -
  **Danielle Adams** <<adamzdanielle@gmail.com>> (she/her)
* [evanlucas](https://github.com/evanlucas) -
  **Evan Lucas** <<evanlucas@me.com>> (he/him)
* [fhinkel](https://github.com/fhinkel) -
  **Franziska Hinkelmann** <<franziska.hinkelmann@gmail.com>> (she/her)
* [Fishrock123](https://github.com/Fishrock123) -
  **Jeremiah Senkpiel** <<fishrock123@rocketmail.com>> (he/they)
* [gabrielschulhof](https://github.com/gabrielschulhof) -
  **Gabriel Schulhof** <<gabrielschulhof@gmail.com>>
* [gibfahn](https://github.com/gibfahn) -
  **Gibson Fahnestock** <<gibfahn@gmail.com>> (he/him)
* [indutny](https://github.com/indutny) -
  **Fedor Indutny** <<fedor@indutny.com>>
* [isaacs](https://github.com/isaacs) -
  **Isaac Z. Schlueter** <<i@izs.me>>
* [joshgav](https://github.com/joshgav) -
  **Josh Gavant** <<josh.gavant@outlook.com>>
* [mmarchini](https://github.com/mmarchini) -
  **Mary Marchini** <<oss@mmarchini.me>> (she/her)
* [mscdex](https://github.com/mscdex) -
  **Brian White** <<mscdex@mscdex.net>>
* [MylesBorins](https://github.com/MylesBorins) -
  **Myles Borins** <<myles.borins@gmail.com>> (he/him)
* [nebrius](https://github.com/nebrius) -
  **Bryan Hughes** <<bryan@nebri.us>>
* [ofrobots](https://github.com/ofrobots) -
  **Ali Ijaz Sheikh** <<ofrobots@google.com>> (he/him)
* [orangemocha](https://github.com/orangemocha) -
  **Alexis Campailla** <<orangemocha@nodejs.org>>
* [piscisaureus](https://github.com/piscisaureus) -
  **Bert Belder** <<bertbelder@gmail.com>>
* [RaisinTen](https://github.com/RaisinTen) -
  **Darshan Sen** <<raisinten@gmail.com>> (he/him)
* [rvagg](https://github.com/rvagg) -
  **Rod Vagg** <<r@va.gg>>
* [sam-github](https://github.com/sam-github) -
  **Sam Roberts** <<vieuxtech@gmail.com>>
* [shigeki](https://github.com/shigeki) -
  **Shigeki Ohtsu** <<ohtsu@ohtsu.org>> (he/him)
* [thefourtheye](https://github.com/thefourtheye) -
  **Sakthipriyan Vairamani** <<thechargingvolcano@gmail.com>> (he/him)
* [TimothyGu](https://github.com/TimothyGu) -
  **Tiancheng "Timothy" Gu** <<timothygu99@gmail.com>> (he/him)
* [trevnorris](https://github.com/trevnorris) -
  **Trevor Norris** <<trev.norris@gmail.com>>

</details>

<!-- node-core-utils and find-inactive-collaborators.mjs depend on the format
     of the collaborator list. If the format changes, those utilities need to be
     tested and updated. -->

### Collaborators

* [addaleax](https://github.com/addaleax) -
  **Anna Henningsen** <<anna@addaleax.net>> (she/her)
* [aduh95](https://github.com/aduh95) -
  **Antoine du Hamel** <<duhamelantoine1995@gmail.com>> (he/him)
* [anonrig](https://github.com/anonrig) -
  **Yagiz Nizipli** <<yagiz@nizipli.com>> (he/him)
* [apapirovski](https://github.com/apapirovski) -
  **Anatoli Papirovski** <<apapirovski@mac.com>> (he/him)
* [atlowChemi](https://github.com/atlowChemi) -
  **Chemi Atlow** <<chemi@atlow.co.il>> (he/him)
* [Ayase-252](https://github.com/Ayase-252) -
  **Qingyu Deng** <<i@ayase-lab.com>>
* [bengl](https://github.com/bengl) -
  **Bryan English** <<bryan@bryanenglish.com>> (he/him)
* [benjamingr](https://github.com/benjamingr) -
  **Benjamin Gruenbaum** <<benjamingr@gmail.com>>
* [BethGriggs](https://github.com/BethGriggs) -
  **Beth Griggs** <<bethanyngriggs@gmail.com>> (she/her)
* [bnb](https://github.com/bnb) -
  **Tierney Cyren** <<hello@bnb.im>> (they/them)
* [bnoordhuis](https://github.com/bnoordhuis) -
  **Ben Noordhuis** <<info@bnoordhuis.nl>>
* [BridgeAR](https://github.com/BridgeAR) -
  **Ruben Bridgewater** <<ruben@bridgewater.de>> (he/him)
* [cclauss](https://github.com/cclauss) -
  **Christian Clauss** <<cclauss@me.com>> (he/him)
* [cjihrig](https://github.com/cjihrig) -
  **Colin Ihrig** <<cjihrig@gmail.com>> (he/him)
* [codebytere](https://github.com/codebytere) -
  **Shelley Vohr** <<shelley.vohr@gmail.com>> (she/her)
* [cola119](https://github.com/cola119) -
  **Kohei Ueno** <<kohei.ueno119@gmail.com>> (he/him)
* [daeyeon](https://github.com/daeyeon) -
  **Daeyeon Jeong** <<daeyeon.dev@gmail.com>> (he/him)
* [danielleadams](https://github.com/danielleadams) -
  **Danielle Adams** <<adamzdanielle@gmail.com>> (she/her)
* [debadree25](https://github.com/debadree25) -
  **Debadree Chatterjee** <<debadree333@gmail.com>> (he/him)
* [deokjinkim](https://github.com/deokjinkim) -
  **Deokjin Kim** <<deokjin81.kim@gmail.com>> (he/him)
* [edsadr](https://github.com/edsadr) -
  **Adrian Estrada** <<edsadr@gmail.com>> (he/him)
* [ErickWendel](https://github.com/ErickWendel) -
  **Erick Wendel** <<erick.workspace@gmail.com>> (he/him)
* [Ethan-Arrowood](https://github.com/Ethan-Arrowood) -
  **Ethan Arrowood** <<ethan@arrowood.dev>> (he/him)
* [F3n67u](https://github.com/F3n67u) -
  **Feng Yu** <<F3n67u@outlook.com>> (he/him)
* [fhinkel](https://github.com/fhinkel) -
  **Franziska Hinkelmann** <<franziska.hinkelmann@gmail.com>> (she/her)
* [Flarna](https://github.com/Flarna) -
  **Gerhard Stöbich** <<deb2001-github@yahoo.de>> (he/they)
* [gabrielschulhof](https://github.com/gabrielschulhof) -
  **Gabriel Schulhof** <<gabrielschulhof@gmail.com>>
* [gengjiawen](https://github.com/gengjiawen) -
  **Jiawen Geng** <<technicalcute@gmail.com>>
* [GeoffreyBooth](https://github.com/GeoffreyBooth) -
  **Geoffrey Booth** <<webadmin@geoffreybooth.com>> (he/him)
* [gireeshpunathil](https://github.com/gireeshpunathil) -
  **Gireesh Punathil** <<gpunathi@in.ibm.com>> (he/him)
* [guybedford](https://github.com/guybedford) -
  **Guy Bedford** <<guybedford@gmail.com>> (he/him)
* [H4ad](https://github.com/H4ad) -
  **Vinícius Lourenço Claro Cardoso** <<contact@viniciusl.com.br>> (he/him)
* [HarshithaKP](https://github.com/HarshithaKP) -
  **Harshitha K P** <<harshitha014@gmail.com>> (she/her)
* [himself65](https://github.com/himself65) -
  **Zeyu "Alex" Yang** <<himself65@outlook.com>> (he/him)
* [jakecastelli](https://github.com/jakecastelli) -
  **Jake Yuesong Li** <<jake.yuesong@gmail.com>> (he/him)
* [JakobJingleheimer](https://github.com/JakobJingleheimer) -
  **Jacob Smith** <<jacob@frende.me>> (he/him)
* [jasnell](https://github.com/jasnell) -
  **James M Snell** <<jasnell@gmail.com>> (he/him)
* [jkrems](https://github.com/jkrems) -
  **Jan Krems** <<jan.krems@gmail.com>> (he/him)
* [joesepi](https://github.com/joesepi) -
  **Joe Sepi** <<sepi@joesepi.com>> (he/him)
* [joyeecheung](https://github.com/joyeecheung) -
  **Joyee Cheung** <<joyeec9h3@gmail.com>> (she/her)
* [juanarbol](https://github.com/juanarbol) -
  **Juan José Arboleda** <<soyjuanarbol@gmail.com>> (he/him)
* [JungMinu](https://github.com/JungMinu) -
  **Minwoo Jung** <<nodecorelab@gmail.com>> (he/him)
* [KhafraDev](https://github.com/KhafraDev) -
  **Matthew Aitken** <<maitken033380023@gmail.com>> (he/him)
* [kuriyosh](https://github.com/kuriyosh) -
  **Yoshiki Kurihara** <<yosyos0306@gmail.com>> (he/him)
* [kvakil](https://github.com/kvakil) -
  **Keyhan Vakil** <<kvakil@sylph.kvakil.me>>
* [legendecas](https://github.com/legendecas) -
  **Chengzhong Wu** <<legendecas@gmail.com>> (he/him)
* [lemire](https://github.com/lemire) -
  **Daniel Lemire** <<daniel@lemire.me>>
* [Linkgoron](https://github.com/Linkgoron) -
  **Nitzan Uziely** <<linkgoron@gmail.com>>
* [LiviaMedeiros](https://github.com/LiviaMedeiros) -
  **LiviaMedeiros** <<livia@cirno.name>>
* [lpinca](https://github.com/lpinca) -
  **Luigi Pinca** <<luigipinca@gmail.com>> (he/him)
* [lukekarrys](https://github.com/lukekarrys) -
  **Luke Karrys** <<luke@lukekarrys.com>> (he/him)
* [Lxxyx](https://github.com/Lxxyx) -
  **Zijian Liu** <<lxxyxzj@gmail.com>> (he/him)
* [marco-ippolito](https://github.com/marco-ippolito) -
  **Marco Ippolito** <<marcoippolito54@gmail.com>> (he/him)
* [marsonya](https://github.com/marsonya) -
  **Akhil Marsonya** <<akhil.marsonya27@gmail.com>> (he/him)
* [MattiasBuelens](https://github.com/MattiasBuelens) -
  **Mattias Buelens** <<mattias@buelens.com>> (he/him)
* [mcollina](https://github.com/mcollina) -
  **Matteo Collina** <<matteo.collina@gmail.com>> (he/him)
* [meixg](https://github.com/meixg) -
  **Xuguang Mei** <<meixuguang@gmail.com>> (he/him)
* [mhdawson](https://github.com/mhdawson) -
  **Michael Dawson** <<midawson@redhat.com>> (he/him)
* [mildsunrise](https://github.com/mildsunrise) -
  **Alba Mendez** <<me@alba.sh>> (she/her)
* [MoLow](https://github.com/MoLow) -
  **Moshe Atlow** <<moshe@atlow.co.il>> (he/him)
* [MrJithil](https://github.com/MrJithil) -
  **Jithil P Ponnan** <<jithil@outlook.com>> (he/him)
* [ovflowd](https://github.com/ovflowd) -
  **Claudio Wunder** <<cwunder@gnome.org>> (he/they)
* [panva](https://github.com/panva) -
  **Filip Skokan** <<panva.ip@gmail.com>> (he/him)
* [pimterry](https://github.com/pimterry) -
  **Tim Perry** <<pimterry@gmail.com>> (he/him)
* [Qard](https://github.com/Qard) -
  **Stephen Belanger** <<admin@stephenbelanger.com>> (he/him)
* [RafaelGSS](https://github.com/RafaelGSS) -
  **Rafael Gonzaga** <<rafael.nunu@hotmail.com>> (he/him)
* [richardlau](https://github.com/richardlau) -
  **Richard Lau** <<rlau@redhat.com>>
* [rluvaton](https://github.com/rluvaton) -
  **Raz Luvaton** <<rluvaton@gmail.com>> (he/him)
* [ronag](https://github.com/ronag) -
  **Robert Nagy** <<ronagy@icloud.com>>
* [ruyadorno](https://github.com/ruyadorno) -
  **Ruy Adorno** <<ruy@vlt.sh>> (he/him)
* [santigimeno](https://github.com/santigimeno) -
  **Santiago Gimeno** <<santiago.gimeno@gmail.com>>
* [ShogunPanda](https://github.com/ShogunPanda) -
  **Paolo Insogna** <<paolo@cowtech.it>> (he/him)
* [srl295](https://github.com/srl295) -
  **Steven R Loomis** <<srl295@gmail.com>>
* [StefanStojanovic](https://github.com/StefanStojanovic) -
  **Stefan Stojanovic** <<stefan.stojanovic@janeasystems.com>> (he/him)
* [sxa](https://github.com/sxa) -
  **Stewart X Addison** <<sxa@redhat.com>> (he/him)
* [targos](https://github.com/targos) -
  **Michaël Zasso** <<targos@protonmail.com>> (he/him)
* [theanarkh](https://github.com/theanarkh) -
  **theanarkh** <<theratliter@gmail.com>> (he/him)
* [tniessen](https://github.com/tniessen) -
  **Tobias Nießen** <<tniessen@tnie.de>> (he/him)
* [trivikr](https://github.com/trivikr) -
  **Trivikram Kamat** <<trivikr.dev@gmail.com>>
* [Trott](https://github.com/Trott) -
  **Rich Trott** <<rtrott@gmail.com>> (he/him)
* [UlisesGascon](https://github.com/UlisesGascon) -
  **Ulises Gascón** <<ulisesgascongonzalez@gmail.com>> (he/him)
* [vmoroz](https://github.com/vmoroz) -
  **Vladimir Morozov** <<vmorozov@microsoft.com>> (he/him)
* [VoltrexKeyva](https://github.com/VoltrexKeyva) -
  **Mohammed Keyvanzadeh** <<mohammadkeyvanzade94@gmail.com>> (he/him)
* [watilde](https://github.com/watilde) -
  **Daijiro Wachi** <<daijiro.wachi@gmail.com>> (he/him)
* [zcbenz](https://github.com/zcbenz) -
  **Cheng Zhao** <<zcbenz@gmail.com>> (he/him)
* [ZYSzys](https://github.com/ZYSzys) -
  **Yongsheng Zhang** <<zyszys98@gmail.com>> (he/him)

<details>

<summary>Emeriti</summary>

<!-- find-inactive-collaborators.mjs depends on the format of the emeriti list.
     If the format changes, those utilities need to be tested and updated. -->

### Collaborator emeriti

* [ak239](https://github.com/ak239) -
  **Aleksei Koziatinskii** <<ak239spb@gmail.com>>
* [andrasq](https://github.com/andrasq) -
  **Andras** <<andras@kinvey.com>>
* [AndreasMadsen](https://github.com/AndreasMadsen) -
  **Andreas Madsen** <<amwebdk@gmail.com>> (he/him)
* [AnnaMag](https://github.com/AnnaMag) -
  **Anna M. Kedzierska** <<anna.m.kedzierska@gmail.com>>
* [antsmartian](https://github.com/antsmartian) -
  **Anto Aravinth** <<anto.aravinth.cse@gmail.com>> (he/him)
* [aqrln](https://github.com/aqrln) -
  **Alexey Orlenko** <<eaglexrlnk@gmail.com>> (he/him)
* [AshCripps](https://github.com/AshCripps) -
  **Ash Cripps** <<email@ashleycripps.co.uk>>
* [bcoe](https://github.com/bcoe) -
  **Ben Coe** <<bencoe@gmail.com>> (he/him)
* [bmeck](https://github.com/bmeck) -
  **Bradley Farias** <<bradley.meck@gmail.com>>
* [bmeurer](https://github.com/bmeurer) -
  **Benedikt Meurer** <<benedikt.meurer@gmail.com>>
* [boneskull](https://github.com/boneskull) -
  **Christopher Hiller** <<boneskull@boneskull.com>> (he/him)
* [brendanashworth](https://github.com/brendanashworth) -
  **Brendan Ashworth** <<brendan.ashworth@me.com>>
* [bzoz](https://github.com/bzoz) -
  **Bartosz Sosnowski** <<bartosz@janeasystems.com>>
* [calvinmetcalf](https://github.com/calvinmetcalf) -
  **Calvin Metcalf** <<calvin.metcalf@gmail.com>>
* [ChALkeR](https://github.com/ChALkeR) -
  **Сковорода Никита Андреевич** <<chalkerx@gmail.com>> (he/him)
* [chrisdickinson](https://github.com/chrisdickinson) -
  **Chris Dickinson** <<christopher.s.dickinson@gmail.com>>
* [claudiorodriguez](https://github.com/claudiorodriguez) -
  **Claudio Rodriguez** <<cjrodr@yahoo.com>>
* [danbev](https://github.com/danbev) -
  **Daniel Bevenius** <<daniel.bevenius@gmail.com>> (he/him)
* [DavidCai1993](https://github.com/DavidCai1993) -
  **David Cai** <<davidcai1993@yahoo.com>> (he/him)
* [davisjam](https://github.com/davisjam) -
  **Jamie Davis** <<davisjam@vt.edu>> (he/him)
* [devnexen](https://github.com/devnexen) -
  **David Carlier** <<devnexen@gmail.com>>
* [devsnek](https://github.com/devsnek) -
  **Gus Caplan** <<me@gus.host>> (they/them)
* [digitalinfinity](https://github.com/digitalinfinity) -
  **Hitesh Kanwathirtha** <<digitalinfinity@gmail.com>> (he/him)
* [dmabupt](https://github.com/dmabupt) -
  **Xu Meng** <<dmabupt@gmail.com>> (he/him)
* [dnlup](https://github.com/dnlup)
  **dnlup** <<dnlup.dev@gmail.com>>
* [eljefedelrodeodeljefe](https://github.com/eljefedelrodeodeljefe) -
  **Robert Jefe Lindstaedt** <<robert.lindstaedt@gmail.com>>
* [estliberitas](https://github.com/estliberitas) -
  **Alexander Makarenko** <<estliberitas@gmail.com>>
* [eugeneo](https://github.com/eugeneo) -
  **Eugene Ostroukhov** <<eostroukhov@google.com>>
* [evanlucas](https://github.com/evanlucas) -
  **Evan Lucas** <<evanlucas@me.com>> (he/him)
* [firedfox](https://github.com/firedfox) -
  **Daniel Wang** <<wangyang0123@gmail.com>>
* [Fishrock123](https://github.com/Fishrock123) -
  **Jeremiah Senkpiel** <<fishrock123@rocketmail.com>> (he/they)
* [gdams](https://github.com/gdams) -
  **George Adams** <<gadams@microsoft.com>> (he/him)
* [geek](https://github.com/geek) -
  **Wyatt Preul** <<wpreul@gmail.com>>
* [gibfahn](https://github.com/gibfahn) -
  **Gibson Fahnestock** <<gibfahn@gmail.com>> (he/him)
* [glentiki](https://github.com/glentiki) -
  **Glen Keane** <<glenkeane.94@gmail.com>> (he/him)
* [hashseed](https://github.com/hashseed) -
  **Yang Guo** <<yangguo@chromium.org>> (he/him)
* [hiroppy](https://github.com/hiroppy) -
  **Yuta Hiroto** <<hello@hiroppy.me>> (he/him)
* [iansu](https://github.com/iansu) -
  **Ian Sutherland** <<ian@iansutherland.ca>>
* [iarna](https://github.com/iarna) -
  **Rebecca Turner** <<me@re-becca.org>>
* [imran-iq](https://github.com/imran-iq) -
  **Imran Iqbal** <<imran@imraniqbal.org>>
* [imyller](https://github.com/imyller) -
  **Ilkka Myller** <<ilkka.myller@nodefield.com>>
* [indutny](https://github.com/indutny) -
  **Fedor Indutny** <<fedor@indutny.com>>
* [isaacs](https://github.com/isaacs) -
  **Isaac Z. Schlueter** <<i@izs.me>>
* [italoacasas](https://github.com/italoacasas) -
  **Italo A. Casas** <<me@italoacasas.com>> (he/him)
* [JacksonTian](https://github.com/JacksonTian) -
  **Jackson Tian** <<shyvo1987@gmail.com>>
* [jasongin](https://github.com/jasongin) -
  **Jason Ginchereau** <<jasongin@microsoft.com>>
* [jbergstroem](https://github.com/jbergstroem) -
  **Johan Bergström** <<bugs@bergstroem.nu>>
* [jdalton](https://github.com/jdalton) -
  **John-David Dalton** <<john.david.dalton@gmail.com>>
* [jhamhader](https://github.com/jhamhader) -
  **Yuval Brik** <<yuval@brik.org.il>>
* [joaocgreis](https://github.com/joaocgreis) -
  **João Reis** <<reis@janeasystems.com>>
* [joshgav](https://github.com/joshgav) -
  **Josh Gavant** <<josh.gavant@outlook.com>>
* [julianduque](https://github.com/julianduque) -
  **Julian Duque** <<julianduquej@gmail.com>> (he/him)
* [kfarnung](https://github.com/kfarnung) -
  **Kyle Farnung** <<kfarnung@microsoft.com>> (he/him)
* [kunalspathak](https://github.com/kunalspathak) -
  **Kunal Pathak** <<kunal.pathak@microsoft.com>>
* [lance](https://github.com/lance) -
  **Lance Ball** <<lball@redhat.com>> (he/him)
* [Leko](https://github.com/Leko) -
  **Shingo Inoue** <<leko.noor@gmail.com>> (he/him)
* [lucamaraschi](https://github.com/lucamaraschi) -
  **Luca Maraschi** <<luca.maraschi@gmail.com>> (he/him)
* [lundibundi](https://github.com/lundibundi) -
  **Denys Otrishko** <<shishugi@gmail.com>> (he/him)
* [lxe](https://github.com/lxe) -
  **Aleksey Smolenchuk** <<lxe@lxe.co>>
* [maclover7](https://github.com/maclover7) -
  **Jon Moss** <<me@jonathanmoss.me>> (he/him)
* [mafintosh](https://github.com/mafintosh) -
  **Mathias Buus** <<mathiasbuus@gmail.com>> (he/him)
* [matthewloring](https://github.com/matthewloring) -
  **Matthew Loring** <<mattloring@google.com>>
* [Mesteery](https://github.com/Mesteery) -
  **Mestery** <<mestery@protonmail.com>> (he/him)
* [micnic](https://github.com/micnic) -
  **Nicu Micleușanu** <<micnic90@gmail.com>> (he/him)
* [mikeal](https://github.com/mikeal) -
  **Mikeal Rogers** <<mikeal.rogers@gmail.com>>
* [miladfarca](https://github.com/miladfarca) -
  **Milad Fa** <<mfarazma@redhat.com>> (he/him)
* [misterdjules](https://github.com/misterdjules) -
  **Julien Gilli** <<jgilli@netflix.com>>
* [mmarchini](https://github.com/mmarchini) -
  **Mary Marchini** <<oss@mmarchini.me>> (she/her)
* [monsanto](https://github.com/monsanto) -
  **Christopher Monsanto** <<chris@monsan.to>>
* [MoonBall](https://github.com/MoonBall) -
  **Chen Gang** <<gangc.cxy@foxmail.com>>
* [mscdex](https://github.com/mscdex) -
  **Brian White** <<mscdex@mscdex.net>>
* [MylesBorins](https://github.com/MylesBorins) -
  **Myles Borins** <<myles.borins@gmail.com>> (he/him)
* [not-an-aardvark](https://github.com/not-an-aardvark) -
  **Teddy Katz** <<teddy.katz@gmail.com>> (he/him)
* [ofrobots](https://github.com/ofrobots) -
  **Ali Ijaz Sheikh** <<ofrobots@google.com>> (he/him)
* [Olegas](https://github.com/Olegas) -
  **Oleg Elifantiev** <<oleg@elifantiev.ru>>
* [orangemocha](https://github.com/orangemocha) -
  **Alexis Campailla** <<orangemocha@nodejs.org>>
* [othiym23](https://github.com/othiym23) -
  **Forrest L Norvell** <<ogd@aoaioxxysz.net>> (they/them/themself)
* [oyyd](https://github.com/oyyd) -
  **Ouyang Yadong** <<oyydoibh@gmail.com>> (he/him)
* [petkaantonov](https://github.com/petkaantonov) -
  **Petka Antonov** <<petka_antonov@hotmail.com>>
* [phillipj](https://github.com/phillipj) -
  **Phillip Johnsen** <<johphi@gmail.com>>
* [piscisaureus](https://github.com/piscisaureus) -
  **Bert Belder** <<bertbelder@gmail.com>>
* [pmq20](https://github.com/pmq20) -
  **Minqi Pan** <<pmq2001@gmail.com>>
* [PoojaDurgad](https://github.com/PoojaDurgad) -
  **Pooja D P** <<Pooja.D.P@ibm.com>> (she/her)
* [princejwesley](https://github.com/princejwesley) -
  **Prince John Wesley** <<princejohnwesley@gmail.com>>
* [psmarshall](https://github.com/psmarshall) -
  **Peter Marshall** <<petermarshall@chromium.org>> (he/him)
* [puzpuzpuz](https://github.com/puzpuzpuz) -
  **Andrey Pechkurov** <<apechkurov@gmail.com>> (he/him)
* [RaisinTen](https://github.com/RaisinTen) -
  **Darshan Sen** <<raisinten@gmail.com>> (he/him)
* [refack](https://github.com/refack) -
  **Refael Ackermann (רפאל פלחי)** <<refack@gmail.com>> (he/him/הוא/אתה)
* [rexagod](https://github.com/rexagod) -
  **Pranshu Srivastava** <<rexagod@gmail.com>> (he/him)
* [rickyes](https://github.com/rickyes) -
  **Ricky Zhou** <<0x19951125@gmail.com>> (he/him)
* [rlidwka](https://github.com/rlidwka) -
  **Alex Kocharin** <<alex@kocharin.ru>>
* [rmg](https://github.com/rmg) -
  **Ryan Graham** <<r.m.graham@gmail.com>>
* [robertkowalski](https://github.com/robertkowalski) -
  **Robert Kowalski** <<rok@kowalski.gd>>
* [romankl](https://github.com/romankl) -
  **Roman Klauke** <<romaaan.git@gmail.com>>
* [ronkorving](https://github.com/ronkorving) -
  **Ron Korving** <<ron@ronkorving.nl>>
* [RReverser](https://github.com/RReverser) -
  **Ingvar Stepanyan** <<me@rreverser.com>>
* [rubys](https://github.com/rubys) -
  **Sam Ruby** <<rubys@intertwingly.net>>
* [rvagg](https://github.com/rvagg) -
  **Rod Vagg** <<rod@vagg.org>>
* [ryzokuken](https://github.com/ryzokuken) -
  **Ujjwal Sharma** <<ryzokuken@disroot.org>> (he/him)
* [saghul](https://github.com/saghul) -
  **Saúl Ibarra Corretgé** <<s@saghul.net>>
* [sam-github](https://github.com/sam-github) -
  **Sam Roberts** <<vieuxtech@gmail.com>>
* [sebdeckers](https://github.com/sebdeckers) -
  **Sebastiaan Deckers** <<sebdeckers83@gmail.com>>
* [seishun](https://github.com/seishun) -
  **Nikolai Vavilov** <<vvnicholas@gmail.com>>
* [shigeki](https://github.com/shigeki) -
  **Shigeki Ohtsu** <<ohtsu@ohtsu.org>> (he/him)
* [shisama](https://github.com/shisama) -
  **Masashi Hirano** <<shisama07@gmail.com>> (he/him)
* [silverwind](https://github.com/silverwind) -
  **Roman Reiss** <<me@silverwind.io>>
* [starkwang](https://github.com/starkwang) -
  **Weijia Wang** <<starkwang@126.com>>
* [stefanmb](https://github.com/stefanmb) -
  **Stefan Budeanu** <<stefan@budeanu.com>>
* [tellnes](https://github.com/tellnes) -
  **Christian Tellnes** <<christian@tellnes.no>>
* [thefourtheye](https://github.com/thefourtheye) -
  **Sakthipriyan Vairamani** <<thechargingvolcano@gmail.com>> (he/him)
* [thlorenz](https://github.com/thlorenz) -
  **Thorsten Lorenz** <<thlorenz@gmx.de>>
* [TimothyGu](https://github.com/TimothyGu) -
  **Tiancheng "Timothy" Gu** <<timothygu99@gmail.com>> (he/him)
* [trevnorris](https://github.com/trevnorris) -
  **Trevor Norris** <<trev.norris@gmail.com>>
* [tunniclm](https://github.com/tunniclm) -
  **Mike Tunnicliffe** <<m.j.tunnicliffe@gmail.com>>
* [vdeturckheim](https://github.com/vdeturckheim) -
  **Vladimir de Turckheim** <<vlad2t@hotmail.com>> (he/him)
* [vkurchatkin](https://github.com/vkurchatkin) -
  **Vladimir Kurchatkin** <<vladimir.kurchatkin@gmail.com>>
* [vsemozhetbyt](https://github.com/vsemozhetbyt) -
  **Vse Mozhet Byt** <<vsemozhetbyt@gmail.com>> (he/him)
* [watson](https://github.com/watson) -
  **Thomas Watson** <<w@tson.dk>>
* [whitlockjc](https://github.com/whitlockjc) -
  **Jeremy Whitlock** <<jwhitlock@apache.org>>
* [XadillaX](https://github.com/XadillaX) -
  **Khaidi Chu** <<i@2333.moe>> (he/him)
* [yashLadha](https://github.com/yashLadha) -
  **Yash Ladha** <<yash@yashladha.in>> (he/him)
* [yhwang](https://github.com/yhwang) -
  **Yihong Wang** <<yh.wang@ibm.com>>
* [yorkie](https://github.com/yorkie) -
  **Yorkie Liu** <<yorkiefixer@gmail.com>>
* [yosuke-furukawa](https://github.com/yosuke-furukawa) -
  **Yosuke Furukawa** <<yosuke.furukawa@gmail.com>>

</details>

<!--lint enable prohibited-strings-->

Collaborators follow the [Collaborator Guide](./doc/contributing/collaborator-guide.md) in
maintaining the Node.js project.

### Triagers

* [atlowChemi](https://github.com/atlowChemi) -
  **Chemi Atlow** <<chemi@atlow.co.il>> (he/him)
* [Ayase-252](https://github.com/Ayase-252) -
  **Qingyu Deng** <<i@ayase-lab.com>>
* [bmuenzenmeyer](https://github.com/bmuenzenmeyer) -
  **Brian Muenzenmeyer** <<brian.muenzenmeyer@gmail.com>> (he/him)
* [CanadaHonk](https://github.com/CanadaHonk) -
  **Oliver Medhurst** <<honk@goose.icu>> (they/them)
* [daeyeon](https://github.com/daeyeon) -
  **Daeyeon Jeong** <<daeyeon.dev@gmail.com>> (he/him)
* [F3n67u](https://github.com/F3n67u) -
  **Feng Yu** <<F3n67u@outlook.com>> (he/him)
* [himadriganguly](https://github.com/himadriganguly) -
  **Himadri Ganguly** <<himadri.tech@gmail.com>> (he/him)
* [iam-frankqiu](https://github.com/iam-frankqiu) -
  **Frank Qiu** <<iam.frankqiu@gmail.com>> (he/him)
* [marsonya](https://github.com/marsonya) -
  **Akhil Marsonya** <<akhil.marsonya27@gmail.com>> (he/him)
* [meixg](https://github.com/meixg) -
  **Xuguang Mei** <<meixuguang@gmail.com>> (he/him)
* [mertcanaltin](https://github.com/mertcanaltin) -
  **Mert Can Altin** <<mertgold60@gmail.com>>
* [Mesteery](https://github.com/Mesteery) -
  **Mestery** <<mestery@protonmail.com>> (he/him)
* [PoojaDurgad](https://github.com/PoojaDurgad) -
  **Pooja Durgad** <<Pooja.D.P@ibm.com>>
* [preveen-stack](https://github.com/preveen-stack) -
  **Preveen Padmanabhan** <<wide4head@gmail.com>> (he/him)
* [RedYetiDev](https://github.com/redyetidev) -
  **Aviv Keller** <<redyetidev@gmail.com>> (they/them)
* [VoltrexKeyva](https://github.com/VoltrexKeyva) -
  **Mohammed Keyvanzadeh** <<mohammadkeyvanzade94@gmail.com>> (he/him)

Triagers follow the [Triage Guide](./doc/contributing/issues.md#triaging-a-bug-report) when
responding to new issues.

### Release keys

Primary GPG keys for Node.js Releasers (some Releasers sign with subkeys):

* **Beth Griggs** <<bethanyngriggs@gmail.com>>
  `4ED778F539E3634C779C87C6D7062848A1AB005C`
* **Bryan English** <<bryan@bryanenglish.com>>
  `141F07595B7B3FFE74309A937405533BE57C7D57`
* **Danielle Adams** <<adamzdanielle@gmail.com>>
  `74F12602B6F1C4E913FAA37AD3A89613643B6201`
* **Juan José Arboleda** <<soyjuanarbol@gmail.com>>
  `DD792F5973C6DE52C432CBDAC77ABFA00DDBF2B7`
* **Marco Ippolito** <<marcoippolito54@gmail.com>>
  `CC68F5A3106FF448322E48ED27F5E38D5B0A215F`
* **Michaël Zasso** <<targos@protonmail.com>>
  `8FCCA13FEF1D0C2E91008E09770F7A9A5AE15600`
* **Myles Borins** <<myles.borins@gmail.com>>
  `C4F0DFFF4E8C1A8236409D08E73BC641CC11F4C8`
* **Rafael Gonzaga** <<rafael.nunu@hotmail.com>>
  `890C08DB8579162FEE0DF9DB8BEAB4DFCF555EF4`
* **Richard Lau** <<rlau@redhat.com>>
  `C82FA3AE1CBEDC6BE46B9360C43CEC45C17AB93C`
* **Ruy Adorno** <<ruyadorno@hotmail.com>>
  `108F52B48DB57BB0CC439B2997B01419BD92F80A`
* **Ulises Gascón** <<ulisesgascongonzalez@gmail.com>>
  `A363A499291CBBC940DD62E41F10027AF002F8B0`

To import the full set of trusted release keys (including subkeys possibly used
to sign releases):

```bash
gpg --keyserver hkps://keys.openpgp.org --recv-keys 4ED778F539E3634C779C87C6D7062848A1AB005C # Beth Griggs
gpg --keyserver hkps://keys.openpgp.org --recv-keys 141F07595B7B3FFE74309A937405533BE57C7D57 # Bryan English
gpg --keyserver hkps://keys.openpgp.org --recv-keys 74F12602B6F1C4E913FAA37AD3A89613643B6201 # Danielle Adams
gpg --keyserver hkps://keys.openpgp.org --recv-keys DD792F5973C6DE52C432CBDAC77ABFA00DDBF2B7 # Juan José Arboleda
gpg --keyserver hkps://keys.openpgp.org --recv-keys CC68F5A3106FF448322E48ED27F5E38D5B0A215F # Marco Ippolito
gpg --keyserver hkps://keys.openpgp.org --recv-keys 8FCCA13FEF1D0C2E91008E09770F7A9A5AE15600 # Michaël Zasso
gpg --keyserver hkps://keys.openpgp.org --recv-keys C4F0DFFF4E8C1A8236409D08E73BC641CC11F4C8 # Myles Borins
gpg --keyserver hkps://keys.openpgp.org --recv-keys 890C08DB8579162FEE0DF9DB8BEAB4DFCF555EF4 # Rafael Gonzaga
gpg --keyserver hkps://keys.openpgp.org --recv-keys C82FA3AE1CBEDC6BE46B9360C43CEC45C17AB93C # Richard Lau
gpg --keyserver hkps://keys.openpgp.org --recv-keys 108F52B48DB57BB0CC439B2997B01419BD92F80A # Ruy Adorno
gpg --keyserver hkps://keys.openpgp.org --recv-keys A363A499291CBBC940DD62E41F10027AF002F8B0 # Ulises Gascón
```

See [Verifying binaries](#verifying-binaries) for how to use these keys to
verify a downloaded file.

<details>

<summary>Other keys used to sign some previous releases</summary>

* **Chris Dickinson** <<christopher.s.dickinson@gmail.com>>
  `9554F04D7259F04124DE6B476D5A82AC7E37093B`
* **Colin Ihrig** <<cjihrig@gmail.com>>
  `94AE36675C464D64BAFA68DD7434390BDBE9B9C5`
* **Danielle Adams** <<adamzdanielle@gmail.com>>
  `1C050899334244A8AF75E53792EF661D867B9DFA`
* **Evan Lucas** <<evanlucas@me.com>>
  `B9AE9905FFD7803F25714661B63B535A4C206CA9`
* **Gibson Fahnestock** <<gibfahn@gmail.com>>
  `77984A986EBC2AA786BC0F66B01FBB92821C587A`
* **Isaac Z. Schlueter** <<i@izs.me>>
  `93C7E9E91B49E432C2F75674B0A78B0A6C481CF6`
* **Italo A. Casas** <<me@italoacasas.com>>
  `56730D5401028683275BD23C23EFEFE93C4CFFFE`
* **James M Snell** <<jasnell@keybase.io>>
  `71DCFD284A79C3B38668286BC97EC7A07EDE3FC1`
* **Jeremiah Senkpiel** <<fishrock@keybase.io>>
  `FD3A5288F042B6850C66B31F09FE44734EB7990E`
* **Juan José Arboleda** <<soyjuanarbol@gmail.com>>
  `61FC681DFB92A079F1685E77973F295594EC4689`
* **Julien Gilli** <<jgilli@fastmail.fm>>
  `114F43EE0176B71C7BC219DD50A3051F888C628D`
* **Rod Vagg** <<rod@vagg.org>>
  `DD8F2338BAE7501E3DD5AC78C273792F7D83545D`
* **Ruben Bridgewater** <<ruben@bridgewater.de>>
  `A48C2BEE680E841632CD4E44F07496B3EB3C1762`
* **Shelley Vohr** <<shelley.vohr@gmail.com>>
  `B9E2F5981AA6E0CD28160D9FF13993A75599653C`
* **Timothy J Fontaine** <<tjfontaine@gmail.com>>
  `7937DFD2AB06298B2293C3187D33FF9D0246406D`

</details>

### Security release stewards

When possible, the commitment to take slots in the
security release steward rotation is made by companies in order
to ensure individuals who act as security stewards have the
support and recognition from their employer to be able to
prioritize security releases. Security release stewards manage security
releases on a rotation basis as outlined in the
[security release process](./doc/contributing/security-release-process.md).

* Datadog
  * [bengl](https://github.com/bengl) -
    **Bryan English** <<bryan@bryanenglish.com>> (he/him)
* NodeSource
  * [juanarbol](https://github.com/juanarbol) -
    **Juan José Arboleda** <<soyjuanarbol@gmail.com>> (he/him)
  * [RafaelGSS](https://github.com/RafaelGSS) -
    **Rafael Gonzaga** <<rafael.nunu@hotmail.com>> (he/him)
* Platformatic
  * [mcollina](https://github.com/mcollina) -
    **Matteo Collina** <<matteo.collina@gmail.com>> (he/him)
* Red Hat and IBM
  * [joesepi](https://github.com/joesepi) -
    **Joe Sepi** <<joesepi@ibm.com>> (he/him)
  * [mhdawson](https://github.com/mhdawson) -
    **Michael Dawson** <<midawson@redhat.com>> (he/him)

## License

Node.js is available under the
[MIT license](https://opensource.org/licenses/MIT). Node.js also includes
external libraries that are available under a variety of licenses.  See
[LICENSE](https://github.com/nodejs/node/blob/HEAD/LICENSE) for the full
license text.

[Code of Conduct]: https://github.com/nodejs/admin/blob/HEAD/CODE_OF_CONDUCT.md
[Contributing to the project]: CONTRIBUTING.md
[Node.js website]: https://nodejs.org/
[OpenJS Foundation]: https://openjsf.org/
[Strategic initiatives]: doc/contributing/strategic-initiatives.md
[Technical values and prioritization]: doc/contributing/technical-values.md
[Working Groups]: https://github.com/nodejs/TSC/blob/HEAD/WORKING_GROUPS.md
