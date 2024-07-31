GYP can Generate Your Projects.
===================================

Documents are available at [`./docs`](./docs).

__gyp-next__ is [released](https://github.com/nodejs/gyp-next/releases) to the [__Python Packaging Index__](https://pypi.org/project/gyp-next) (PyPI) and can be installed with the command:
* `python3 -m pip install gyp-next`

When used as a command line utility, __gyp-next__ can also be installed with [pipx](https://pypa.github.io/pipx):
* `pipx install gyp-next`
```
Installing to a new venv 'gyp-next'
  installed package gyp-next 0.13.0, installed using Python 3.10.6
  These apps are now globally available
    - gyp
done! ✨ 🌟 ✨
```

Or to run __gyp-next__ directly without installing it:
* `pipx run gyp-next --help`
```
NOTE: running app 'gyp' from 'gyp-next'
usage: usage: gyp [options ...] [build_file ...]

options:
  -h, --help            show this help message and exit
  --build CONFIGS       configuration for build after project generation
  --check               check format of gyp files
  [ ... ]
```
