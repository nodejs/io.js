const isWindowsShell = (process.platform === 'win32') &&
  !/^MINGW(32|64)$/.test(process.env.MSYSTEM) && process.env.TERM !== 'cygwin'

exports.isWindowsShell = isWindowsShell
