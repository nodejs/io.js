# Hacking

## Getting the sources

Git is required to hack on anything, you can set up a git clone of GYP
as follows:

```
mkdir foo
cd foo
git clone git@github.com:nodejs/gyp-next.git
cd gyp
```

(this will clone gyp underneath it into `foo/gyp`.
`foo` can be any directory name you want. Once you've done that,
you can use the repo like anything other Git repo.

## Testing your change

GYP has a suite of tests which you can run with the provided test driver
to make sure your changes aren't breaking anything important.

You run the test driver with e.g.

``` sh
$ python -m pip install --upgrade pip setuptools
$ pip install --editable ".[dev]"
$ python -m pytest
```

See [Testing](Testing.md) for more details on the test framework.

Note that it can be handy to look at the project files output by the tests
to diagnose problems. The easiest way to do that is by kindly asking the
test driver to leave the temporary directories it creates in-place.
This is done by setting the environment variable "PRESERVE", e.g.

```
set PRESERVE=all     # On Windows
export PRESERVE=all  # On saner platforms.
```

## Reviewing your change

All changes to GYP must be code reviewed before submission.
