/* IMPORTANT
 * This snapshot file is auto-generated, but designed for humans.
 * It should be checked into source control and tracked carefully.
 * Re-generate by setting TAP_SNAPSHOT=1 and running tests.
 * Make sure to inspect the output below.  Do not ignore changes!
 */
'use strict'
exports[`test/lib/utils/error-message.js TAP 404 cleans sensitive info from package id > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "",
      "'http://evil:***@npmjs.org/not-found' is not in this registry.",
    ],
    Array [
      "404",
      "This package name is not valid, because",
      "",
    ],
    Array [
      "404",
      " 1. name can only contain URL-friendly characters",
    ],
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "Note that you can also install from a",
    ],
    Array [
      "404",
      "tarball, folder, http url, or git url.",
    ],
  ],
  "summary": Array [
    Array [
      "404",
      "not found",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP 404 name with error > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "",
      "'node_modules' is not in this registry.",
    ],
    Array [
      "404",
      "This package name is not valid, because",
      "",
    ],
    Array [
      "404",
      " 1. node_modules is a blacklisted name",
    ],
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "Note that you can also install from a",
    ],
    Array [
      "404",
      "tarball, folder, http url, or git url.",
    ],
  ],
  "summary": Array [
    Array [
      "404",
      "not found",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP 404 name with warning > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "",
      "'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' is not in this registry.",
    ],
    Array [
      "404",
      "This package name is not valid, because",
      "",
    ],
    Array [
      "404",
      " 1. name can no longer contain more than 214 characters",
    ],
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "Note that you can also install from a",
    ],
    Array [
      "404",
      "tarball, folder, http url, or git url.",
    ],
  ],
  "summary": Array [
    Array [
      "404",
      "not found",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP 404 no package id > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "404",
      "not found",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP 404 you should publish it > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "",
      "'yolo' is not in this registry.",
    ],
    Array [
      "404",
      "",
    ],
    Array [
      "404",
      "Note that you can also install from a",
    ],
    Array [
      "404",
      "tarball, folder, http url, or git url.",
    ],
  ],
  "summary": Array [
    Array [
      "404",
      "not found",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP args are cleaned > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "signal",
      "SIGYOLO",
    ],
    Array [
      "command",
      "some command",
      "a",
      "r",
      "g",
      "s",
      "https://evil:***@npmjs.org",
    ],
    Array [
      "",
      "stdout",
    ],
    Array [
      "",
      "stderr",
    ],
  ],
  "summary": Array [
    Array [
      "",
      "cmd err",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP bad engine without config loaded > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "notsup",
      String(
        Not compatible with your version of node/npm: some@package
        Required: undefined
        Actual:   {"npm":"123.456.789-npm","node":"123.456.789-node"}
      ),
    ],
  ],
  "summary": Array [
    Array [
      "engine",
      "foo",
    ],
    Array [
      "engine",
      "Not compatible with your version of node/npm: some@package",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP bad platform array os/arch > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "notsup",
      String(
        Valid os:   !yours,mine
        Actual os:  posix
        Valid cpu:  x867,x5309
        Actual cpu: x64
      ),
    ],
  ],
  "summary": Array [
    Array [
      "notsup",
      "Unsupported platform for lodash@1.0.0: wanted {/"os/":/"!yours,mine/",/"cpu/":/"x867,x5309/"} (current: {/"os/":/"posix/",/"cpu/":/"x64/"})",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP bad platform omits keys with no required value > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "notsup",
      String(
        Valid os:  !yours,mine
        Actual os: posix
      ),
    ],
  ],
  "summary": Array [
    Array [
      "notsup",
      "Unsupported platform for lodash@1.0.0: wanted {/"os/":/"!yours,mine/"} (current: {/"os/":/"posix/"})",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP bad platform string os/arch > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "notsup",
      String(
        Valid os:   !yours
        Actual os:  posix
        Valid cpu:  x420
        Actual cpu: x64
      ),
    ],
  ],
  "summary": Array [
    Array [
      "notsup",
      "Unsupported platform for lodash@1.0.0: wanted {/"os/":/"!yours/",/"cpu/":/"x420/"} (current: {/"os/":/"posix/",/"cpu/":/"x64/"})",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP default message > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      "error object",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP default message > must match snapshot 2`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      "error string",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP default message > must match snapshot 3`] = `
Object {
  "detail": Array [
    Array [
      "signal",
      "SIGYOLO",
    ],
    Array [
      "command",
      "some command",
      "a",
      "r",
      "g",
      "s",
    ],
    Array [
      "",
      "stdout",
    ],
    Array [
      "",
      "stderr",
    ],
  ],
  "summary": Array [
    Array [
      "",
      "cmd err",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":false,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It is likely you do not have the permissions to access this file as the current user
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "/not/cache/dir/dest",
        "path": "/not/cache/dir/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":false,"cacheDest":false} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":false,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It is likely you do not have the permissions to access this file as the current user
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "{CWD}/cache/dest",
        "path": "/not/cache/dir/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":false,"cacheDest":true} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":true,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It is likely you do not have the permissions to access this file as the current user
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "/not/cache/dir/dest",
        "path": "{CWD}/cache/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":true,"cacheDest":false} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":true,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It is likely you do not have the permissions to access this file as the current user
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "{CWD}/cache/dest",
        "path": "{CWD}/cache/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":false,"cachePath":true,"cacheDest":true} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":false,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It is likely you do not have the permissions to access this file as the current user
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "/not/cache/dir/dest",
        "path": "/not/cache/dir/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":false,"cacheDest":false} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
]
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":false,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      String(
        
        Your cache folder contains root-owned files, due to a bug in
        previous versions of npm which has since been addressed.
        
        To permanently fix this problem, please run:
          sudo chown -R 867:5309 "{CWD}/cache"
      ),
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":false,"cacheDest":true} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
  "dummy stack trace",
]
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":true,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      String(
        
        Your cache folder contains root-owned files, due to a bug in
        previous versions of npm which has since been addressed.
        
        To permanently fix this problem, please run:
          sudo chown -R 867:5309 "{CWD}/cache"
      ),
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":true,"cacheDest":false} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
  "dummy stack trace",
]
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":true,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      String(
        
        Your cache folder contains root-owned files, due to a bug in
        previous versions of npm which has since been addressed.
        
        To permanently fix this problem, please run:
          sudo chown -R 867:5309 "{CWD}/cache"
      ),
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":false,"loaded":true,"cachePath":true,"cacheDest":true} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
  "dummy stack trace",
]
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":false,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "/not/cache/dir/dest",
        "path": "/not/cache/dir/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":false,"cacheDest":false} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":false,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "{CWD}/cache/dest",
        "path": "/not/cache/dir/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":false,"cacheDest":true} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":true,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "/not/cache/dir/dest",
        "path": "{CWD}/cache/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":true,"cacheDest":false} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":true,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "{CWD}/cache/dest",
        "path": "{CWD}/cache/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":false,"cachePath":true,"cacheDest":true} > must match snapshot 2`] = `
Array []
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":false,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "/not/cache/dir/dest",
        "path": "/not/cache/dir/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":false,"cacheDest":false} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
]
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":false,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "{CWD}/cache/dest",
        "path": "/not/cache/dir/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":false,"cacheDest":true} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
]
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":true,"cacheDest":false} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "/not/cache/dir/dest",
        "path": "{CWD}/cache/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":true,"cacheDest":false} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
]
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":true,"cacheDest":true} > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        The operation was rejected by your operating system.
        It's possible that the file was already in use (by a text editor or antivirus),
        or that you lack permissions to access it.
        
        If you believe this might be a permissions issue, please double-check the
        permissions of the file and its containing directories, or try running
        the command again as root/Administrator.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: whoopsie {
        "code": "EACCES",
        "dest": "{CWD}/cache/dest",
        "path": "{CWD}/cache/path",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eacces/eperm {"windows":true,"loaded":true,"cachePath":true,"cacheDest":true} > must match snapshot 2`] = `
Array [
  "title npm",
  "argv /"--fetch-retries/" /"0/" /"--cache/" /"{CWD}/cache/" /"--loglevel/" /"silly/" /"--color/" /"false/"",
  "logfile logs-max:10 dir:{CWD}/cache/_logs/{DATE}-",
  "logfile {CWD}/cache/_logs/{DATE}-debug-0.log",
]
`

exports[`test/lib/utils/error-message.js TAP enoent without a file > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "enoent",
      "This is related to npm not being able to find a file./n",
    ],
  ],
  "summary": Array [
    Array [
      "enoent",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP enolock without a command > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      "Try creating one first with: npm i --package-lock-only",
    ],
    Array [
      "",
      "Original error: foo",
    ],
  ],
  "summary": Array [
    Array [
      "",
      "This command requires an existing lockfile.",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 401, no auth headers > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      "nope",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 401, no message > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      Object {
        "code": "E401",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 one-time pass challenge code > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        You can provide a one-time password by passing --otp=<code> to the command you ran.
        If you already provided a one-time password then it is likely that you either typoed
        it, or it timed out. Please try again.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      "This operation requires a one-time password from your authenticator.",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 one-time pass challenge message > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        You can provide a one-time password by passing --otp=<code> to the command you ran.
        If you already provided a one-time password then it is likely that you either typoed
        it, or it timed out. Please try again.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      "This operation requires a one-time password from your authenticator.",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 www-authenticate challenges Basic realm=by, charset="UTF-8", challenge="your friends" > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        If you were trying to login, change your password, create an
        authentication token or enable two-factor authentication then
        that means you likely typed your password in incorrectly.
        Please try again, or recover your password at:
          https://www.npmjs.com/forgot
        
        If you were doing some other operation then your saved credentials are
        probably out of date. To correct this please try logging in again with:
          npm login
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      "Incorrect or missing password.",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 www-authenticate challenges Bearer realm=do, charset="UTF-8", challenge="yourself" > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        To correct this please try logging in again with:
          npm login
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      "Unable to authenticate, your authentication token seems to be invalid.",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 www-authenticate challenges PickACardAnyCard realm=friday, charset="UTF-8" > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      "challenge!",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP eotp/e401 www-authenticate challenges WashYourHands, charset="UTF-8" > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "",
      "challenge!",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP explain ERESOLVE errors > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      "",
    ],
    Array [
      "",
      "explanation",
    ],
  ],
  "files": Array [
    Array [
      "eresolve-report.txt",
      "report",
    ],
  ],
  "summary": Array [
    Array [
      "ERESOLVE",
      "could not resolve",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "audit",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 10`] = `
Object {
  "detail": Array [
    Array [
      "network",
      String(
        This is a problem related to network connectivity.
        In most cases you are behind a proxy or have bad network settings.
        
        If you are behind a proxy, please make sure that the
        'proxy' config is set properly.  See: 'npm help config'
      ),
    ],
  ],
  "summary": Array [
    Array [
      "network",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 11`] = `
Object {
  "detail": Array [
    Array [
      "network",
      String(
        This is a problem related to network connectivity.
        In most cases you are behind a proxy or have bad network settings.
        
        If you are behind a proxy, please make sure that the
        'proxy' config is set properly.  See: 'npm help config'
      ),
    ],
  ],
  "summary": Array [
    Array [
      "network",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 12`] = `
Object {
  "detail": Array [
    Array [
      "network",
      String(
        This is a problem related to network connectivity.
        In most cases you are behind a proxy or have bad network settings.
        
        If you are behind a proxy, please make sure that the
        'proxy' config is set properly.  See: 'npm help config'
      ),
    ],
  ],
  "summary": Array [
    Array [
      "network",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 13`] = `
Object {
  "detail": Array [
    Array [
      "notsup",
      String(
        Not compatible with your version of node/npm: some@package
        Required: undefined
        Actual:   {"npm":"123.456.789-npm","node":"123.456.789-node"}
      ),
    ],
  ],
  "summary": Array [
    Array [
      "engine",
      "foo",
    ],
    Array [
      "engine",
      "Not compatible with your version of node/npm: some@package",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 14`] = `
Object {
  "detail": Array [
    Array [
      "nospc",
      String(
        There appears to be insufficient space on your system to finish.
        Clear up some disk space and try again.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "nospc",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 15`] = `
Object {
  "detail": Array [
    Array [
      "rofs",
      String(
        Often virtualized file systems, or other file systems
        that don't support symlinks, give this error.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "rofs",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 16`] = `
Object {
  "detail": Array [
    Array [
      "enoent",
      String(
        This is related to npm not being able to find a file.
        
        Check if the file '/some/file' is present.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "enoent",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 17`] = `
Object {
  "detail": Array [
    Array [
      "typeerror",
      String(
        This is an error with npm itself. Please report this error at:
          https://github.com/npm/cli/issues
      ),
    ],
  ],
  "summary": Array [
    Array [
      "typeerror",
      "dummy stack trace",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 18`] = `
Object {
  "detail": Array [
    Array [
      "typeerror",
      String(
        This is an error with npm itself. Please report this error at:
          https://github.com/npm/cli/issues
      ),
    ],
  ],
  "summary": Array [
    Array [
      "typeerror",
      "dummy stack trace",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 19`] = `
Object {
  "detail": Array [
    Array [
      "typeerror",
      String(
        This is an error with npm itself. Please report this error at:
          https://github.com/npm/cli/issues
      ),
    ],
  ],
  "summary": Array [
    Array [
      "typeerror",
      "dummy stack trace",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 2`] = `
Object {
  "detail": Array [
    Array [
      "audit",
      "Try creating one first with: npm i --package-lock-only",
    ],
    Array [
      "audit",
      "Original error: foo",
    ],
  ],
  "summary": Array [
    Array [
      "audit",
      "This command requires an existing lockfile.",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 20`] = `
Object {
  "detail": Array [
    Array [
      "typeerror",
      String(
        This is an error with npm itself. Please report this error at:
          https://github.com/npm/cli/issues
      ),
    ],
  ],
  "summary": Array [
    Array [
      "typeerror",
      "dummy stack trace",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 21`] = `
Object {
  "detail": Array [
    Array [
      "notarget",
      String(
        In most cases you or one of your dependencies are requesting
        a package version that doesn't exist.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "notarget",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 22`] = `
Object {
  "detail": Array [
    Array [
      "403",
      String(
        In most cases, you or one of your dependencies are requesting
        a package version that is forbidden by your security policy, or
        on a server you do not have access to.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "403",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 23`] = `
Object {
  "detail": Array [
    Array [
      "network",
      String(
        This is a problem related to network connectivity.
        In most cases you are behind a proxy or have bad network settings.

        If you are behind a proxy, please make sure that the
        'proxy' config is set properly.  See: 'npm help config'
      ),
    ],
  ],
  "summary": Array [
    Array [
      "network",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 3`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        If you are behind a proxy, please make sure that the
        'proxy' config is set properly.  See: 'npm help config'
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      Error: foo {
        "code": "ECONNREFUSED",
        "file": "/some/file",
        "path": "/some/path",
        "pkgid": "some@package",
      },
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 4`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        
        Failed using git.
        Please check if you have git installed and in your PATH.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 5`] = `
Object {
  "detail": Array [
    Array [
      "publish fail",
      "Update the 'version' field in package.json and try again.",
    ],
    Array [
      "publish fail",
      "",
    ],
    Array [
      "publish fail",
      "To automatically increment version numbers, see:",
    ],
    Array [
      "publish fail",
      "  npm help version",
    ],
  ],
  "summary": Array [
    Array [
      "publish fail",
      "Cannot publish over existing version.",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 6`] = `
Object {
  "detail": Array [
    Array [
      "git",
      String(
        Refusing to remove it. Update manually,
        or move it out of the way first.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "git",
      "foo",
    ],
    Array [
      "git",
      "  /some/path",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 7`] = `
Object {
  "detail": Array [
    Array [
      "",
      "Remove the existing file and try again, or run npm",
    ],
    Array [
      "",
      "with --force to overwrite files recklessly.",
    ],
  ],
  "summary": Array [
    Array [
      "",
      "foo",
    ],
    Array [
      "",
      "File exists: /some/path",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 8`] = `
Object {
  "detail": Array [
    Array [
      "need auth",
      "You need to authorize this machine using \`npm adduser\`",
    ],
  ],
  "summary": Array [
    Array [
      "need auth",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP just simple messages > must match snapshot 9`] = `
Object {
  "detail": Array [
    Array [
      "network",
      String(
        This is a problem related to network connectivity.
        In most cases you are behind a proxy or have bad network settings.
        
        If you are behind a proxy, please make sure that the
        'proxy' config is set properly.  See: 'npm help config'
      ),
    ],
  ],
  "summary": Array [
    Array [
      "network",
      "foo",
    ],
  ],
}
`

exports[`test/lib/utils/error-message.js TAP replace message/stack sensistive info > must match snapshot 1`] = `
Object {
  "detail": Array [],
  "summary": Array [
    Array [
      "audit",
      "Error at registry: https://user:***@registry.npmjs.org/",
    ],
  ],
}
`

exports[`v TAP json parse json somewhere else > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "JSON.parse",
      String(
        Failed to parse JSON data.
        Note: package.json must be actual JSON, not just JavaScript.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "JSON.parse",
      "not json",
    ],
  ],
}
`

exports[`v TAP json parse just regular bad json in package.json > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "JSON.parse",
      String(
        Failed to parse JSON data.
        Note: package.json must be actual JSON, not just JavaScript.
      ),
    ],
  ],
  "summary": Array [
    Array [
      "JSON.parse",
      "not json",
    ],
  ],
}
`

exports[`v TAP json parse merge conflict in package.json > must match snapshot 1`] = `
Object {
  "detail": Array [
    Array [
      "",
      String(
        Merge conflict detected in your package.json.
        
        Please resolve the package.json conflict and retry.
      ),
    ],
  ],
  "summary": Array [],
}
`
