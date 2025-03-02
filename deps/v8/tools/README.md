# TOOLS

This directory contains debugging and investigation tools for V8.

The contents are regularly mirrored to <http://v8.dev/tools>.

## Local Development

For local development you have to start a local webserver under <http://localhost:8000>:
```
  cd tools/;
  npm install;
  npm run start-webserver;
```

## Local Symbol Server

The system-analyzer can symbolize profiles for local binaries by running a
local symbol server
```
  cd tools/;
  ./system-analyzer/local-server.sh;
```
Note that the local symbol server will run `nm` and `objdump` and has access to
your files.