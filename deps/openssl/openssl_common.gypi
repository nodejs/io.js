{
  'include_dirs': [
    'openssl/',
    'openssl/include/',
    'openssl/crypto/',
    'openssl/crypto/include/',
    'openssl/crypto/modes/',
    'openssl/crypto/ec/curve448',
    'openssl/crypto/ec/curve448/arch_32',
    'openssl/providers/common/include',
    'openssl/providers/implementations/include',
    'config/',
  ],
  # build options specific to OS
  'conditions': [
    [ 'OS in ("aix", "os400")', {
      # AIX is missing /usr/include/endian.h
      'defines': [
        '__LITTLE_ENDIAN=1234',
        '__BIG_ENDIAN=4321',
        '__BYTE_ORDER=__BIG_ENDIAN',
        '__FLOAT_WORD_ORDER=__BIG_ENDIAN',
        'OPENSSLDIR="/etc/ssl"',
        'ENGINESDIR="/dev/null"',
      ],
    }, 'OS=="win"', {
      'defines': [
        ## default of Win. See INSTALL in openssl repo.
        'OPENSSLDIR="C:\\\\Program\\ Files\\\\Common\\ Files\\\\SSL"',
        'ENGINESDIR="NUL"',
        'OPENSSL_SYS_WIN32', 'WIN32_LEAN_AND_MEAN', 'L_ENDIAN',
        '_CRT_SECURE_NO_DEPRECATE', 'UNICODE', '_UNICODE',
      ],
      'cflags': [
        '-W3', '-wd4090', '-Gs0', '-GF', '-Gy', '-nologo','/O2',
      ],
      'msvs_disabled_warnings': [4090],
      'link_settings': {
        'libraries': [
          '-lws2_32.lib',
          '-lgdi32.lib',
          '-ladvapi32.lib',
          '-lcrypt32.lib',
          '-luser32.lib',
        ],
      },
    }, 'OS=="mac"', {
      'xcode_settings': {
        'WARNING_CFLAGS': ['-Wno-missing-field-initializers']
      },
      'defines': [
        'OPENSSLDIR="/System/Library/OpenSSL/"',
        'ENGINESDIR="/dev/null"',
      ],
    }, 'OS=="solaris"', {
      'defines': [
        'OPENSSLDIR="/etc/ssl"',
        'ENGINESDIR="/dev/null"',
        '__EXTENSIONS__'
      ],
    }, {
      # linux and others
      'cflags': ['-Wno-missing-field-initializers',],
      'defines': [
        'OPENSSLDIR="/etc/ssl"',
        'ENGINESDIR="/dev/null"',
        'TERMIOS',
      ],
      'conditions': [
        [ 'clang==0', {
          'cflags': ['-Wno-old-style-declaration',],
        }],
      ],
    }],
  ]
}
