'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const { subtle } = globalThis.crypto;

const sizes = [1024, 2048, 4096];

const hashes = [
  'SHA-1',
  'SHA-256',
  'SHA-384',
  'SHA-512',
];

const keyData = {
  1024: {
    spki: Buffer.from(
      '30819f300d06092a864886f70d010101050003818d0030818902818100cd99f8b111' +
      '9f8d0a2ce7ac8bfd0cb547d348f931cc9c5ca79fde20e51c40eb01ab261e01253df1' +
      'e88f71d086e94b7abe77839103a476bee0cc87c743151afd4431fa5d8fa051271cf5' +
      '4e49cf7500d8a9957ec09b9d43ef70098c57f10d03bfd31748af563b881687720d3c' +
      '7b10a1cd553ac71d296b6edeeca5b99c8afb36dd970203010001', 'hex'),
    pkcs8: Buffer.from(
      '30820278020100300d06092a864886f70d0101010500048202623082025e02010002' +
      '818100cd99f8b1119f8d0a2ce7ac8bfd0cb547d348f931cc9c5ca79fde20e51c40eb' +
      '01ab261e01253df1e88f71d086e94b7abe77839103a476bee0cc87c743151afd4431' +
      'fa5d8fa051271cf54e49cf7500d8a9957ec09b9d43ef70098c57f10d03bfd31748af' +
      '563b881687720d3c7b10a1cd553ac71d296b6edeeca5b99c8afb36dd970203010001' +
      '02818062a20afc6747f3917e19665d81f826bf5e4d13bf2039a2f9876838bfb0de33' +
      'df890bb0393c748b28d627f3b1c519c0b8befd0f048051b72080fe62497c468658e4' +
      '5508e5d206958d7a9318a62a39da7df0e6e8f951912c0676ed65cd04b5685517602e' +
      'a9aed56e22ab59c414120108f15d201390f8b72060f065eff7def97501024100f41a' +
      'c08392f5cdfa863ee5890ee0c2057f939ad65dace23762ce1968dfb230f9538f0592' +
      '10f3b4aa77e3119730d958171e024999b55ca3a4f172424298462a79024100d79ee3' +
      '0c9d586b99e642f4cf6e12803c078c5a88310b26904e406ba77d2910a77a986481df' +
      'ce61aabe01224f2cddfecc757a4cf944a9699814a13e28ff65448f024100a9d77f41' +
      '4cdc681fba8e42a8d5483ed712880200cb16c22325451f5adfe21cbf2d8b62a5d9d3' +
      'a74dc0b2a6079b3e6e534f56ea1cdf9a80660074ae73a57d948902410084d45fc0e4' +
      'a994d7e12efc4b50dedadaa037c989bed4c4b3ff50d640feecae52ce46551c60f86d' +
      'd85666b2711e0dc02aca70463d051c6c6d80bff8601f3d8e67024100cdba49400862' +
      '9ebc526d52b1050d846461540f67b75825db009458a64f07550e40039d8e84a4e270' +
      'ec9eda11079eb82914acc2f22ce74ec086dc5324bf0723e1', 'hex'),
    jwk: {
      kty: 'RSA',
      n: 'zZn4sRGfjQos56yL_Qy1R9NI-THMnFynn94g5RxA6wGrJh4BJT3x6I9x0IbpS3q-d' +
         '4ORA6R2vuDMh8dDFRr9RDH6XY-gUScc9U5Jz3UA2KmVfsCbnUPvcAmMV_ENA7_TF0' +
         'ivVjuIFodyDTx7EKHNVTrHHSlrbt7spbmcivs23Zc',
      e: 'AQAB',
      d: 'YqIK_GdH85F-GWZdgfgmv15NE78gOaL5h2g4v7DeM9-JC7A5PHSLKNYn87HFGcC4v' +
         'v0PBIBRtyCA_mJJfEaGWORVCOXSBpWNepMYpio52n3w5uj5UZEsBnbtZc0EtWhVF2' +
         'Auqa7VbiKrWcQUEgEI8V0gE5D4tyBg8GXv9975dQE',
      p: '9BrAg5L1zfqGPuWJDuDCBX-TmtZdrOI3Ys4ZaN-yMPlTjwWSEPO0qnfjEZcw2VgXH' +
         'gJJmbVco6TxckJCmEYqeQ',
      q: '157jDJ1Ya5nmQvTPbhKAPAeMWogxCyaQTkBrp30pEKd6mGSB385hqr4BIk8s3f7Md' +
         'XpM-USpaZgUoT4o_2VEjw',
      dp: 'qdd_QUzcaB-6jkKo1Ug-1xKIAgDLFsIjJUUfWt_iHL8ti2Kl2dOnTcCypgebPm5T' +
          'T1bqHN-agGYAdK5zpX2UiQ',
      dq: 'hNRfwOSplNfhLvxLUN7a2qA3yYm-1MSz_1DWQP7srlLORlUcYPht2FZmsnEeDcAq' +
          'ynBGPQUcbG2Av_hgHz2OZw',
      qi: 'zbpJQAhinrxSbVKxBQ2EZGFUD2e3WCXbAJRYpk8HVQ5AA52OhKTicOye2hEHnrgp' +
          'FKzC8iznTsCG3FMkvwcj4Q'
    }
  },

  2048: {
    spki: Buffer.from(
      '30820122300d06092a864886f70d01010105000382010f003082010a0282010100d9' +
      '8580eb2d1772f4a476bc5404bee60d9a3c2acbbcf24a74754d9f5a6812388f9e3f26' +
      '0ad81687ddb366f8da559462b397f1c097896d0df6e6de31c04f8d47cd15600d11be' +
      '4ec4e6309e200416257fabba8bbed33ab0c165da3c9b1fcec2c4e9e52aca6359a7cf' +
      '54d5275b4486bf01a2b45f04fae20b717d01a794570728815297b2b7f22be00ef302' +
      '3813ca87b7e0be8343335cfaf0769e366cf9256cf44239458bb47ebd6b32f0168980' +
      '67009273f79d45b85b9f33f57318dfc5af981aa2964834e7f5b33012d369646a6738' +
      'b22bca55e59066f1e69f6a69f1eedecce881b7423fd44dfc7a7c989c426741d8813c' +
      '3fcdc024b53d84290a3beda3c83872cafd0203010001', 'hex'),
    pkcs8: Buffer.from(
      '308204be020100300d06092a864886f70d0101010500048204a8308204a402010002' +
      '82010100d98580eb2d1772f4a476bc5404bee60d9a3c2acbbcf24a74754d9f5a6812' +
      '388f9e3f260ad81687ddb366f8da559462b397f1c097896d0df6e6de31c04f8d47cd' +
      '15600d11be4ec4e6309e200416257fabba8bbed33ab0c165da3c9b1fcec2c4e9e52a' +
      'ca6359a7cf54d5275b4486bf01a2b45f04fae20b717d01a794570728815297b2b7f2' +
      '2be00ef3023813ca87b7e0be8343335cfaf0769e366cf9256cf44239458bb47ebd6b' +
      '32f016898067009273f79d45b85b9f33f57318dfc5af981aa2964834e7f5b33012d3' +
      '69646a6738b22bca55e59066f1e69f6a69f1eedecce881b7423fd44dfc7a7c989c42' +
      '6741d8813c3fcdc024b53d84290a3beda3c83872cafd0203010001028201005ad2a7' +
      '758aaa53d15a2a49903b3b0a0b7beecb5fae50ec4d9bfd01205a7be129f6451fb93f' +
      '6888ea44d225ede3f5c5107fcced41589c344c7731274cc8ea90a44cdc82187a81a1' +
      '2d0bf7ba1e7ab0c5920a9df6db739201ee69250d1046e0841fb5141cd546c60e87b9' +
      '48698f3f43d986fa11029f4e6ac0c41540c76b5f0dc690d445ffe2bf792e1e67996f' +
      'aba68958e5568e42ee881848f81b2b7465d76327f6d46ff184a907fc1368ace90828' +
      'e3ac2a2f248622d661e4b3d7c104de81a5013bd8ab32116444c7e272af31065f817a' +
      'bdc6981171467968334b12d21bed5d57683140707ac6223dd107067916bf5f97f87c' +
      '07578f2d7b168099c582c4f4a4e1f102818100fcdf6d12d3df7c92438ad38e9c9966' +
      'c0c0ec81150e9e1ce40cb845efa5c3d109ecf0583b8f68c7c57c53a8c9a6f99e9c43' +
      '9e0f749be053ac70bb01e17ffeafafd6d6246fda556d21e49dc03dc3cf19889af486' +
      '451267e1ac8310a846031e0562a22f58bf63f17f5d24044861e307463c8d19964daa' +
      'c956811d603c29e7bec86b02818100dc36288ccc4f0795f128e5ed0d0376ac4c3d89' +
      '08fd48df77bd1357c7033dc52d6f123ae079be902e8fe107810a9a188c60f6d4e0e8' +
      '90436206bca711e0d7a0b6f984aef9154e8a3bbab8ef0a47922ebdcea5393226f1e6' +
      '39a94d4ce5352db85716c25e3044f6abff49c519400d843878f164c5f3ab54f62056' +
      '3737d8794034370281806dddbd0c2315c48fdfdc9f5224e3d96b01e73fa62075bde3' +
      'af4b18c7a863cd9cdc5f0856c8562405bfa0b182fb9314c09bf83e8ad176c3a3f64e' +
      'a9e089b5e42b27d25e7e62841f284ca5e5727072b88b4b97d606889aadc84021aa9a' +
      'd09be88714243210e5a1754ec8693bf19babfb6e2f77e07fda2623f97103f0dfdc1a' +
      '5e05028181009571bbbb31bc406da5a817c1f41ef19ea46eee5cc76779208d945ef1' +
      '94658b36f635ecf702282d392c338f2027cdc3f320aae2756fded79be2ee8c83398f' +
      '9c661097d716fb3abddd232ef62a87bfd130c6d8a2244301cf383a8957320610ed15' +
      '4d40c32306ea507783dcdaf1f93a4e08e5e979dd8fdcacdbed26b42398c5d5a90281' +
      '81009d221bcb65a15be795dfffbab2afa85dc2a3ab65ba5f6e26fa172612d5572129' +
      'bb120015ca4446ec3fdb9ec980a661d2aad23850511898f07c148716095cd1bd60d6' +
      '31464ac89b524660bd465952d2e57d8740b7c3f3db79492b16b87a5cd1767e13526e' +
      'f66d79c691e2c7f2528b69652c29ba210a5e679d23b21a680cbf0d07', 'hex'),
    jwk: {
      kty: 'RSA',
      n: '2YWA6y0XcvSkdrxUBL7mDZo8Ksu88kp0dU2fWmgSOI-ePyYK2BaH3bNm-NpVlGKzl' +
         '_HAl4ltDfbm3jHAT41HzRVgDRG-TsTmMJ4gBBYlf6u6i77TOrDBZdo8mx_OwsTp5S' +
         'rKY1mnz1TVJ1tEhr8BorRfBPriC3F9AaeUVwcogVKXsrfyK-AO8wI4E8qHt-C-g0M' +
         'zXPrwdp42bPklbPRCOUWLtH69azLwFomAZwCSc_edRbhbnzP1cxjfxa-YGqKWSDTn' +
         '9bMwEtNpZGpnOLIrylXlkGbx5p9qafHu3szogbdCP9RN_Hp8mJxCZ0HYgTw_zcAkt' +
         'T2EKQo77aPIOHLK_Q',
      e: 'AQAB',
      d: 'WtKndYqqU9FaKkmQOzsKC3vuy1-uUOxNm_0BIFp74Sn2RR-5P2iI6kTSJe3j9cUQf' +
         '8ztQVicNEx3MSdMyOqQpEzcghh6gaEtC_e6HnqwxZIKnfbbc5IB7mklDRBG4IQftR' +
         'Qc1UbGDoe5SGmPP0PZhvoRAp9OasDEFUDHa18NxpDURf_iv3kuHmeZb6umiVjlVo5' +
         'C7ogYSPgbK3Rl12Mn9tRv8YSpB_wTaKzpCCjjrCovJIYi1mHks9fBBN6BpQE72Ksy' +
         'EWREx-JyrzEGX4F6vcaYEXFGeWgzSxLSG-1dV2gxQHB6xiI90QcGeRa_X5f4fAdXj' +
         'y17FoCZxYLE9KTh8Q',
      p: '_N9tEtPffJJDitOOnJlmwMDsgRUOnhzkDLhF76XD0Qns8Fg7j2jHxXxTqMmm-Z6cQ' +
         '54PdJvgU6xwuwHhf_6vr9bWJG_aVW0h5J3APcPPGYia9IZFEmfhrIMQqEYDHgVioi' +
         '9Yv2Pxf10kBEhh4wdGPI0Zlk2qyVaBHWA8Kee-yGs',
      q: '3DYojMxPB5XxKOXtDQN2rEw9iQj9SN93vRNXxwM9xS1vEjrgeb6QLo_hB4EKmhiMY' +
         'PbU4OiQQ2IGvKcR4NegtvmErvkVToo7urjvCkeSLr3OpTkyJvHmOalNTOU1LbhXFs' +
         'JeMET2q_9JxRlADYQ4ePFkxfOrVPYgVjc32HlANDc',
      dp: 'bd29DCMVxI_f3J9SJOPZawHnP6Ygdb3jr0sYx6hjzZzcXwhWyFYkBb-gsYL7kxTA' +
          'm_g-itF2w6P2TqngibXkKyfSXn5ihB8oTKXlcnByuItLl9YGiJqtyEAhqprQm-iH' +
          'FCQyEOWhdU7IaTvxm6v7bi934H_aJiP5cQPw39waXgU',
      dq: 'lXG7uzG8QG2lqBfB9B7xnqRu7lzHZ3kgjZRe8ZRlizb2Nez3AigtOSwzjyAnzcPz' +
          'IKridW_e15vi7oyDOY-cZhCX1xb7Or3dIy72Koe_0TDG2KIkQwHPODqJVzIGEO0V' +
          'TUDDIwbqUHeD3Nrx-TpOCOXped2P3Kzb7Sa0I5jF1ak',
      qi: 'nSIby2WhW-eV3_-6sq-oXcKjq2W6X24m-hcmEtVXISm7EgAVykRG7D_bnsmApmHS' +
          'qtI4UFEYmPB8FIcWCVzRvWDWMUZKyJtSRmC9RllS0uV9h0C3w_PbeUkrFrh6XNF2' +
          'fhNSbvZtecaR4sfyUotpZSwpuiEKXmedI7IaaAy_DQc'
    }
  },

  4096: {
    spki: Buffer.from(
      '30820222300d06092a864886f70d01010105000382020f003082020a0282020100da' +
      'aaf64cbd9cd8999bb0dd0e2c846768007f64a6f5f8687d1f4a9be25ac1b836aa916f' +
      'de14fc13f8922cbe7349bc34fb04b279eed4cc223e7a64cb6fe9e7d249359293d30e' +
      'a16d89d4afe212b7ad67671e801fda457eea4158e7a05b33f54d3604a7c02144f4a3' +
      'f2bb6fd1b4f1dd6bac0528862fd255087039ba1d83b05d74c6ca526cfbd103484b8f' +
      '3b2cde385945679fd3a013d6ad4d850044dba44f40ee41bdc9f8adb492c4ee56e8d7' +
      '6d27a5a210e62e86ea946a22e6c63fe78f10b3d06d1664369c6b841cd076cdd959e4' +
      '4bc4a9b505559d906e81ba8d7768a2ceaa73076052f0218f51f3d7436089cfd116a2' +
      'fb6cd0e820eccda7aea1740df9bb16f0b9aca0675ea2931a0f8fb79362e77586b932' +
      '40281e1b0d9884288a204e9ea2cfd4e5d2fb587443e5a4a4933b205ed9c5f295664a' +
      'db2e7f441c740a02f9e7827b1d2d493811c3d02d193cfc62bd6d1900fd97fe7cd330' +
      '179c4ea39abc11450ebc10403bbe8846a2fded9c6f291b283fcdcc5e0032ed3e57d3' +
      '735b44c26877486ae2a030a58a86028a99b526f93078480ff5e30fa440bc4a0454d5' +
      '53434957b5485e2e36c1fcbc0ecf1c529f83a8eea8911ce61b7e975d0560447e42ae' +
      '9b657b14da835c7c4e522c378b4d69b18879b12b4d0cf0004c14857981490fa0c896' +
      '725f3b3ba5f0cc0d9c86c204469ed56fe567d8ef8410b897cefee53e173a7d3190d0' +
      'd70203010001', 'hex'),
    pkcs8: Buffer.from(
      '30820944020100300d06092a864886f70d01010105000482092e3082092a02010002' +
      '82020100daaaf64cbd9cd8999bb0dd0e2c846768007f64a6f5f8687d1f4a9be25ac1' +
      'b836aa916fde14fc13f8922cbe7349bc34fb04b279eed4cc223e7a64cb6fe9e7d249' +
      '359293d30ea16d89d4afe212b7ad67671e801fda457eea4158e7a05b33f54d3604a7' +
      'c02144f4a3f2bb6fd1b4f1dd6bac0528862fd255087039ba1d83b05d74c6ca526cfb' +
      'd103484b8f3b2cde385945679fd3a013d6ad4d850044dba44f40ee41bdc9f8adb492' +
      'c4ee56e8d76d27a5a210e62e86ea946a22e6c63fe78f10b3d06d1664369c6b841cd0' +
      '76cdd959e44bc4a9b505559d906e81ba8d7768a2ceaa73076052f0218f51f3d74360' +
      '89cfd116a2fb6cd0e820eccda7aea1740df9bb16f0b9aca0675ea2931a0f8fb79362' +
      'e77586b93240281e1b0d9884288a204e9ea2cfd4e5d2fb587443e5a4a4933b205ed9' +
      'c5f295664adb2e7f441c740a02f9e7827b1d2d493811c3d02d193cfc62bd6d1900fd' +
      '97fe7cd330179c4ea39abc11450ebc10403bbe8846a2fded9c6f291b283fcdcc5e00' +
      '32ed3e57d3735b44c26877486ae2a030a58a86028a99b526f93078480ff5e30fa440' +
      'bc4a0454d553434957b5485e2e36c1fcbc0ecf1c529f83a8eea8911ce61b7e975d05' +
      '60447e42ae9b657b14da835c7c4e522c378b4d69b18879b12b4d0cf0004c14857981' +
      '490fa0c896725f3b3ba5f0cc0d9c86c204469ed56fe567d8ef8410b897cefee53e17' +
      '3a7d3190d0d702030100010282020100b973d15c185c139f8359a6c144a42e871814' +
      'f32a5ee604c849679f7983fb53de991eabbfb010726798a1760c94f69800646571e0' +
      '4a7dae754a9c7da536bdb3acff50872ab2f7d9ccd1a3319b2a4858b02e3fffc3c0b8' +
      'f8b7df4ce2c536f5ce3c080ab57a01df71c4858f3a4db9eb4e4c203bd4426ea24b7b' +
      'd299b43a61b3813caf8ee47b5532f17793cc5e2b41a304a7f3f7298669c5a53f2d91' +
      '38aecbc087d11dc353b30eb883689830f5b3cfb23c17150154cf527c0989ab8dbb37' +
      'acb4b40a30b9614f9c27f9c01b624dfa5d129d8248d2736024847465e160ea4f59f3' +
      '598761fc35486122e229292d90f3bda2f32b45888fb68cdf865d26f5247d2e5d305e' +
      'd7279c39565dcfcc486a70d7cbe6501489e0f22192216cbcb9fe75bdf052403cbaf7' +
      'be8aaa9f934b319465ae8215b1d379069990e6a6b59b5ee8020477ec2385fddf0e1e' +
      'c739d71ffb5aa713e79a36e1554411ea9e3532f3b695c1d63cbc062602c8a1e8c11e' +
      '99e7dd398c374523159922eeaf41fdd2777d7874997f43cc0942d2c8a5d4d8023e13' +
      '0fab4db7f77fe08a29d0aae3249eb06f80ac4649f194ac32ae7e50b1eb5d5966544c' +
      'dd1ed8317d8e232d60e03ca13f30558f144cb66f0f9c8b379b71e2f8ef82fcf1c5f7' +
      '7c3d27c5aa774c88c3b4a96af0ea6572cf0ba0aa8bc2bb3016725440971ed463d5b0' +
      '6a4fe87fc599850838d253436a7ce76002910282010100f4dad7c2ae2463d90104ec' +
      '0ba0565541ce24248fcd6ca6bf5bd14b75075121b32c6591d72775c3511f6f24071a' +
      '691ef95b0202ed7e8de799d5b564eadbc072b3d7e527d46b0937dc88e9ed1c4a6106' +
      '161a2f9653525fba921626b0e7ffa6c7dfd9568e382bc719f7f97a3b8e981431930d' +
      '84f9cbfb9274605851e82d6a64bb634920cb861edf64b3b38051f21955897d6099f0' +
      'e05614ce181ac5e9a49e32de67c5d39065b6cdc93317e77de5823d8bccc3f34526b9' +
      'bb30f98c6b8927ea150d2b18706c6d0f1939377f2898eee360569d72233436268c55' +
      '2a7735632385d0f041ab0847fff3f8b0a611b25c3ecb389e1fa9df7b0776d8a68453' +
      '3e70a063f4841d0282010100e49ef9f3f35e2abd573d988bc57a216104278742dbe1' +
      '0b46675c730a08e10502dc201793386fed6230ae7acf6d98bb7ddcba497f2a5227e4' +
      'a30cbc24476b34ebdfc8072606a71c9e1ad57eba5a98852c359c3d825ca3031b23b9' +
      '8d70ecf6d26b4bf5217e86d72901f4dc245d16e8323e448d99763e01a7c5ca71bbc4' +
      'bafba18042d391678545cf9b75414cfb7d2be069ab061dfe1f6f90059ea6b48fa3cd' +
      'd497070b32ea52258f4b687c6145dcf6ca2d1928dc175c747072ccc68c306fbf351c' +
      '0986ea5aa8f36c4bc563a2ad1fc261e0b84ce3aac76a810e4deae726c0c5e9ae96f0' +
      '37fcf11b61a931317309da41fd0efdd95b8d2c4420f7dbc71f2dd4442e8302820101' +
      '00e18ec7bb9b580272e1317b90aa3f5d82a5373e470a61d0a9ef173a7fb021d8fd89' +
      '2477d8cf8cf8443ec4cf578bc8d2b3ba567c03f3d51d48e549989191a61304011a24' +
      '3ad5ef43fa7055ae0ba5a9034651110d55ec482b42700d6c620b6bc42c3db6328524' +
      '2ee18941d48c10ab9fce9b3c9506d81603b01920c33332c313d05b81fe27fe816a21' +
      '06399137ebe1d29e395547fa516e7af3efd89a00c598c61b835505b3bb3f4f0acd7a' +
      '73d1d21ecc3b8081f213fdbc92e866ba2845ccf32239633dbc32e5b446f4225f8d32' +
      '74be18fd3144f7911d611d5d47255194e6205b7d37c12a7bc919223af880cce19526' +
      'f81d11e616eceacf5c7ce8e116600220921b310282010100813e223db7f21f2544c1' +
      '6c906f85f882b8ef83b6d748a4b01b549730300ecd5f6d83b2f0263298372f20240b' +
      '4980d35576c7d52ecf84fc4a73a68a61d402163bd619657928bfa61cf73c8454e34c' +
      '5fd4bb45e53be214c177c13d6f694c7cc83da20624f63b523d3b7eea48a05b87ce87' +
      '8707a99ebfb4fddc81f2c3dc967c1433c713859ac92bcb0eae3dc9404ee5d40ac885' +
      '3fc55e8e1a14233948cfff2128326ce7f6d3a2b6db081d3c5b5d3c6a43a73516f53d' +
      '3ba613bfc265e7f0a5eba9217d7d48d511b7f31beeadc1d42f251b6207ae67f22ea3' +
      'd5eb793ef787dfe8c28f5182e193dbd5c7e2f70d6664467f9188bd16f87b996fb657' +
      '88664c09037bbbf30282010024799529bd73c16e62451e9109e7b16278767e663edc' +
      '3acf49d33c0f186bd05f1d6b28beb6546a11d9c6d21be9e399fc80b52c91659c07d1' +
      '1795424e6d918a0df1aec6031ade0ff178b036be6150d763313ecc87e2208d66fb20' +
      '986c71ed3b8e1eb9c3879101567338fdd7baddcac424e376b1823c3b38bec69d8e12' +
      '602bdac7962aae2cc641678ba7b12e1a9bf8d1389bd1cc2a59e0d44b50876acb0451' +
      'b55580f749862930b7397f1cea1af4b19f715af97820f8864f637b9badc9b9d8a620' +
      '98b5069a7612b5f56a1925927610d71e5360239a5d000d05ce9c81937657f89b3187' +
      '07279de2ab6010707aad3a9113065a0bdd6dd010fbbc12786aaa8f954fc0', 'hex'),
    jwk: {
      kty: 'RSA',
      n: '2qr2TL2c2JmbsN0OLIRnaAB_ZKb1-Gh9H0qb4lrBuDaqkW_eFPwT-JIsvnNJvDT7B' +
         'LJ57tTMIj56ZMtv6efSSTWSk9MOoW2J1K_iEretZ2cegB_aRX7qQVjnoFsz9U02BK' +
         'fAIUT0o_K7b9G08d1rrAUohi_SVQhwObodg7BddMbKUmz70QNIS487LN44WUVnn9O' +
         'gE9atTYUARNukT0DuQb3J-K20ksTuVujXbSelohDmLobqlGoi5sY_548Qs9BtFmQ2' +
         'nGuEHNB2zdlZ5EvEqbUFVZ2QboG6jXdoos6qcwdgUvAhj1Hz10Ngic_RFqL7bNDoI' +
         'OzNp66hdA35uxbwuaygZ16ikxoPj7eTYud1hrkyQCgeGw2YhCiKIE6eos_U5dL7WH' +
         'RD5aSkkzsgXtnF8pVmStsuf0QcdAoC-eeCex0tSTgRw9AtGTz8Yr1tGQD9l_580zA' +
         'XnE6jmrwRRQ68EEA7vohGov3tnG8pGyg_zcxeADLtPlfTc1tEwmh3SGrioDClioYC' +
         'ipm1JvkweEgP9eMPpEC8SgRU1VNDSVe1SF4uNsH8vA7PHFKfg6juqJEc5ht-l10FY' +
         'ER-Qq6bZXsU2oNcfE5SLDeLTWmxiHmxK00M8ABMFIV5gUkPoMiWcl87O6XwzA2chs' +
         'IERp7Vb-Vn2O-EELiXzv7lPhc6fTGQ0Nc',
      e: 'AQAB',
      d: 'uXPRXBhcE5-DWabBRKQuhxgU8ype5gTISWefeYP7U96ZHqu_sBByZ5ihdgyU9pgAZ' +
         'GVx4Ep9rnVKnH2lNr2zrP9Qhyqy99nM0aMxmypIWLAuP__DwLj4t99M4sU29c48CA' +
         'q1egHfccSFjzpNuetOTCA71EJuokt70pm0OmGzgTyvjuR7VTLxd5PMXitBowSn8_c' +
         'phmnFpT8tkTiuy8CH0R3DU7MOuINomDD1s8-yPBcVAVTPUnwJiauNuzestLQKMLlh' +
         'T5wn-cAbYk36XRKdgkjSc2AkhHRl4WDqT1nzWYdh_DVIYSLiKSktkPO9ovMrRYiPt' +
         'ozfhl0m9SR9Ll0wXtcnnDlWXc_MSGpw18vmUBSJ4PIhkiFsvLn-db3wUkA8uve-iq' +
         'qfk0sxlGWughWx03kGmZDmprWbXugCBHfsI4X93w4exznXH_tapxPnmjbhVUQR6p4' +
         '1MvO2lcHWPLwGJgLIoejBHpnn3TmMN0UjFZki7q9B_dJ3fXh0mX9DzAlC0sil1NgC' +
         'PhMPq02393_giinQquMknrBvgKxGSfGUrDKuflCx611ZZlRM3R7YMX2OIy1g4DyhP' +
         'zBVjxRMtm8PnIs3m3Hi-O-C_PHF93w9J8Wqd0yIw7SpavDqZXLPC6Cqi8K7MBZyVE' +
         'CXHtRj1bBqT-h_xZmFCDjSU0NqfOdgApE',
      p: '9NrXwq4kY9kBBOwLoFZVQc4kJI_NbKa_W9FLdQdRIbMsZZHXJ3XDUR9vJAcaaR75W' +
         'wIC7X6N55nVtWTq28Bys9flJ9RrCTfciOntHEphBhYaL5ZTUl-6khYmsOf_psff2V' +
         'aOOCvHGff5ejuOmBQxkw2E-cv7knRgWFHoLWpku2NJIMuGHt9ks7OAUfIZVYl9YJn' +
         'w4FYUzhgaxemknjLeZ8XTkGW2zckzF-d95YI9i8zD80Umubsw-YxriSfqFQ0rGHBs' +
         'bQ8ZOTd_KJju42BWnXIjNDYmjFUqdzVjI4XQ8EGrCEf_8_iwphGyXD7LOJ4fqd97B' +
         '3bYpoRTPnCgY_SEHQ',
      q: '5J758_NeKr1XPZiLxXohYQQnh0Lb4QtGZ1xzCgjhBQLcIBeTOG_tYjCues9tmLt93' +
         'LpJfypSJ-SjDLwkR2s069_IByYGpxyeGtV-ulqYhSw1nD2CXKMDGyO5jXDs9tJrS_' +
         'UhfobXKQH03CRdFugyPkSNmXY-AafFynG7xLr7oYBC05FnhUXPm3VBTPt9K-BpqwY' +
         'd_h9vkAWeprSPo83UlwcLMupSJY9LaHxhRdz2yi0ZKNwXXHRwcszGjDBvvzUcCYbq' +
         'WqjzbEvFY6KtH8Jh4LhM46rHaoEOTernJsDF6a6W8Df88RthqTExcwnaQf0O_dlbj' +
         'SxEIPfbxx8t1EQugw',
      dp: '4Y7Hu5tYAnLhMXuQqj9dgqU3PkcKYdCp7xc6f7Ah2P2JJHfYz4z4RD7Ez1eLyNKz' +
          'ulZ8A_PVHUjlSZiRkaYTBAEaJDrV70P6cFWuC6WpA0ZREQ1V7EgrQnANbGILa8Qs' +
          'PbYyhSQu4YlB1IwQq5_OmzyVBtgWA7AZIMMzMsMT0FuB_if-gWohBjmRN-vh0p45' +
          'VUf6UW568-_YmgDFmMYbg1UFs7s_TwrNenPR0h7MO4CB8hP9vJLoZrooRczzIjlj' +
          'Pbwy5bRG9CJfjTJ0vhj9MUT3kR1hHV1HJVGU5iBbfTfBKnvJGSI6-IDM4ZUm-B0R' +
          '5hbs6s9cfOjhFmACIJIbMQ',
      dq: 'gT4iPbfyHyVEwWyQb4X4grjvg7bXSKSwG1SXMDAOzV9tg7LwJjKYNy8gJAtJgNNV' +
          'dsfVLs-E_Epzpoph1AIWO9YZZXkov6Yc9zyEVONMX9S7ReU74hTBd8E9b2lMfMg9' +
          'ogYk9jtSPTt-6kigW4fOh4cHqZ6_tP3cgfLD3JZ8FDPHE4WaySvLDq49yUBO5dQK' +
          'yIU_xV6OGhQjOUjP_yEoMmzn9tOittsIHTxbXTxqQ6c1FvU9O6YTv8Jl5_Cl66kh' +
          'fX1I1RG38xvurcHULyUbYgeuZ_Iuo9XreT73h9_owo9RguGT29XH4vcNZmRGf5GI' +
          'vRb4e5lvtleIZkwJA3u78w',
      qi: 'JHmVKb1zwW5iRR6RCeexYnh2fmY-3DrPSdM8Dxhr0F8dayi-tlRqEdnG0hvp45n8' +
          'gLUskWWcB9EXlUJObZGKDfGuxgMa3g_xeLA2vmFQ12MxPsyH4iCNZvsgmGxx7TuO' +
          'HrnDh5EBVnM4_de63crEJON2sYI8Ozi-xp2OEmAr2seWKq4sxkFni6exLhqb-NE4' +
          'm9HMKlng1EtQh2rLBFG1VYD3SYYpMLc5fxzqGvSxn3Fa-Xgg-IZPY3ubrcm52KYg' +
          'mLUGmnYStfVqGSWSdhDXHlNgI5pdAA0FzpyBk3ZX-JsxhwcnneKrYBBweq06kRMG' +
          'WgvdbdAQ-7wSeGqqj5VPwA'
    }
  },
};

async function testImportSpki({ name, publicUsages }, size, hash, extractable) {
  const key = await subtle.importKey(
    'spki',
    keyData[size].spki,
    { name, hash },
    extractable,
    publicUsages);

  assert.strictEqual(key.type, 'public');
  assert.strictEqual(key.extractable, extractable);
  assert.deepStrictEqual(key.usages, publicUsages);
  assert.strictEqual(key.algorithm.name, name);
  assert.strictEqual(key.algorithm.modulusLength, size);
  assert.deepStrictEqual(key.algorithm.publicExponent,
                         new Uint8Array([1, 0, 1]));
  assert.strictEqual(key.algorithm.hash.name, hash);
  assert.strictEqual(key.algorithm, key.algorithm);
  assert.strictEqual(key.usages, key.usages);

  if (extractable) {
    const spki = await subtle.exportKey('spki', key);
    assert.strictEqual(
      Buffer.from(spki).toString('hex'),
      keyData[size].spki.toString('hex'));
  } else {
    await assert.rejects(
      subtle.exportKey('spki', key), {
        message: /key is not extractable/
      });
  }
}

async function testImportPkcs8(
  { name, privateUsages },
  size,
  hash,
  extractable) {
  const key = await subtle.importKey(
    'pkcs8',
    keyData[size].pkcs8,
    { name, hash },
    extractable,
    privateUsages);

  assert.strictEqual(key.type, 'private');
  assert.strictEqual(key.extractable, extractable);
  assert.deepStrictEqual(key.usages, privateUsages);
  assert.strictEqual(key.algorithm.name, name);
  assert.strictEqual(key.algorithm.modulusLength, size);
  assert.deepStrictEqual(key.algorithm.publicExponent,
                         new Uint8Array([1, 0, 1]));
  assert.strictEqual(key.algorithm.hash.name, hash);
  assert.strictEqual(key.algorithm, key.algorithm);
  assert.strictEqual(key.usages, key.usages);

  if (extractable) {
    const pkcs8 = await subtle.exportKey('pkcs8', key);
    assert.strictEqual(
      Buffer.from(pkcs8).toString('hex'),
      keyData[size].pkcs8.toString('hex'));
  } else {
    await assert.rejects(
      subtle.exportKey('pkcs8', key), {
        message: /key is not extractable/
      });
  }

  await assert.rejects(
    subtle.importKey(
      'pkcs8',
      keyData[size].pkcs8,
      { name, hash },
      extractable,
      [/* empty usages */]),
    { name: 'SyntaxError', message: 'Usages cannot be empty when importing a private key.' });
}

async function testImportJwk(
  { name, publicUsages, privateUsages },
  size,
  hash,
  extractable) {

  const jwk = keyData[size].jwk;

  const [
    publicKey,
    privateKey,
  ] = await Promise.all([
    subtle.importKey(
      'jwk',
      {
        kty: jwk.kty,
        n: jwk.n,
        e: jwk.e,
        alg: `PS${hash.substring(4)}`
      },
      { name, hash },
      extractable,
      publicUsages),
    subtle.importKey(
      'jwk',
      { ...jwk, alg: `PS${hash.substring(4)}` },
      { name, hash },
      extractable,
      privateUsages),
  ]);

  assert.strictEqual(publicKey.type, 'public');
  assert.strictEqual(privateKey.type, 'private');
  assert.strictEqual(publicKey.extractable, extractable);
  assert.strictEqual(privateKey.extractable, extractable);
  assert.strictEqual(publicKey.algorithm.name, name);
  assert.strictEqual(privateKey.algorithm.name, name);
  assert.strictEqual(publicKey.algorithm.modulusLength, size);
  assert.strictEqual(privateKey.algorithm.modulusLength, size);
  assert.deepStrictEqual(publicKey.algorithm.publicExponent,
                         new Uint8Array([1, 0, 1]));
  assert.deepStrictEqual(publicKey.algorithm.publicExponent,
                         privateKey.algorithm.publicExponent);
  assert.strictEqual(privateKey.algorithm, privateKey.algorithm);
  assert.strictEqual(privateKey.usages, privateKey.usages);
  assert.strictEqual(publicKey.algorithm, publicKey.algorithm);
  assert.strictEqual(publicKey.usages, publicKey.usages);

  if (extractable) {
    const [
      pubJwk,
      pvtJwk,
    ] = await Promise.all([
      subtle.exportKey('jwk', publicKey),
      subtle.exportKey('jwk', privateKey),
    ]);

    assert.strictEqual(pubJwk.kty, 'RSA');
    assert.strictEqual(pvtJwk.kty, 'RSA');
    assert.strictEqual(pubJwk.n, jwk.n);
    assert.strictEqual(pvtJwk.n, jwk.n);
    assert.strictEqual(pubJwk.e, jwk.e);
    assert.strictEqual(pvtJwk.e, jwk.e);
    assert.strictEqual(pvtJwk.d, jwk.d);
    assert.strictEqual(pvtJwk.p, jwk.p);
    assert.strictEqual(pvtJwk.q, jwk.q);
    assert.strictEqual(pvtJwk.dp, jwk.dp);
    assert.strictEqual(pvtJwk.dq, jwk.dq);
    assert.strictEqual(pvtJwk.qi, jwk.qi);
    assert.strictEqual(pubJwk.d, undefined);
    assert.strictEqual(pubJwk.p, undefined);
    assert.strictEqual(pubJwk.q, undefined);
    assert.strictEqual(pubJwk.dp, undefined);
    assert.strictEqual(pubJwk.dq, undefined);
    assert.strictEqual(pubJwk.qi, undefined);
  } else {
    await assert.rejects(
      subtle.exportKey('jwk', publicKey), {
        message: /key is not extractable/
      });
    await assert.rejects(
      subtle.exportKey('jwk', privateKey), {
        message: /key is not extractable/
      });
  }

  {
    const invalidUse = name === 'RSA-OAEP' ? 'sig' : 'enc';
    await assert.rejects(
      subtle.importKey(
        'jwk',
        { kty: jwk.kty, n: jwk.n, e: jwk.e, use: invalidUse },
        { name, hash },
        extractable,
        publicUsages),
      { message: 'Invalid JWK "use" Parameter' });
    await assert.rejects(
      subtle.importKey(
        'jwk',
        { ...jwk, use: invalidUse },
        { name, hash },
        extractable,
        privateUsages),
      { message: 'Invalid JWK "use" Parameter' });
  }

  {
    let invalidAlg = name === 'RSA-OAEP' ? name : name === 'RSA-PSS' ? 'PS' : 'RS';
    switch (name) {
      case 'RSA-OAEP':
        if (hash === 'SHA-1')
          invalidAlg += '-256';
        break;
      default:
        if (hash === 'SHA-256')
          invalidAlg += '384';
        else
          invalidAlg += '256';
    }
    await assert.rejects(
      subtle.importKey(
        'jwk',
        { kty: jwk.kty, n: jwk.n, e: jwk.e, alg: invalidAlg },
        { name, hash },
        extractable,
        publicUsages),
      { message: 'JWK "alg" does not match the requested algorithm' });
    await assert.rejects(
      subtle.importKey(
        'jwk',
        { ...jwk, alg: invalidAlg },
        { name, hash },
        extractable,
        privateUsages),
      { message: 'JWK "alg" does not match the requested algorithm' });
  }

  await assert.rejects(
    subtle.importKey(
      'jwk',
      { ...jwk },
      { name, hash },
      extractable,
      [/* empty usages */]),
    { name: 'SyntaxError', message: 'Usages cannot be empty when importing a private key.' });

  await assert.rejects(
    subtle.importKey(
      'jwk',
      { kty: jwk.kty, /* missing e */ n: jwk.n },
      { name, hash },
      extractable,
      publicUsages),
    { name: 'DataError', message: 'Invalid keyData' });
}

// combinations to test
const testVectors = [
  {
    name: 'RSA-OAEP',
    privateUsages: ['decrypt', 'unwrapKey'],
    publicUsages: ['encrypt', 'wrapKey']
  },
  {
    name: 'RSA-PSS',
    privateUsages: ['sign'],
    publicUsages: ['verify']
  },
  {
    name: 'RSASSA-PKCS1-v1_5',
    privateUsages: ['sign'],
    publicUsages: ['verify']
  },
];

(async function() {
  const variations = [];
  sizes.forEach((size) => {
    hashes.forEach((hash) => {
      [true, false].forEach((extractable) => {
        testVectors.forEach((vector) => {
          variations.push(testImportSpki(vector, size, hash, extractable));
          variations.push(testImportPkcs8(vector, size, hash, extractable));
          variations.push(testImportJwk(vector, size, hash, extractable));
        });
      });
    });
  });
  await Promise.all(variations);
})().then(common.mustCall());

{
  const ecPublic = crypto.createPublicKey(
    fixtures.readKey('ec_p256_public.pem'));
  const ecPrivate = crypto.createPrivateKey(
    fixtures.readKey('ec_p256_private.pem'));

  for (const [name, [publicUsage, privateUsage]] of Object.entries({
    'RSA-PSS': ['verify', 'sign'],
    'RSASSA-PKCS1-v1_5': ['verify', 'sign'],
    'RSA-OAEP': ['encrypt', 'decrypt'],
  })) {
    assert.rejects(subtle.importKey(
      'spki',
      ecPublic.export({ format: 'der', type: 'spki' }),
      { name, hash: 'SHA-256' },
      true, [publicUsage]), { message: /Invalid key type/ }).then(common.mustCall());
    assert.rejects(subtle.importKey(
      'pkcs8',
      ecPrivate.export({ format: 'der', type: 'pkcs8' }),
      { name, hash: 'SHA-256' },
      true, [privateUsage]), { message: /Invalid key type/ }).then(common.mustCall());
  }
}
