#!/usr/bin/env python
# Moved some utilities here from ../../configure

from __future__ import print_function
import hashlib
import sys
import zipfile
import tarfile
import contextlib
try:
    from urllib.request import FancyURLopener, URLopener
except ImportError:
    from urllib import FancyURLopener, URLopener

def formatSize(amt):
    """Format a size as a string in MB"""
    return "%.1f" % (amt / 1024000.)

def spin(c):
    """print out an ASCII 'spinner' based on the value of counter 'c'"""
    spin = ".:|'"
    return (spin[c % len(spin)])

class ConfigOpener(FancyURLopener):
    """fancy opener used by retrievefile. Set a UA"""
    # append to existing version (UA)
    version = '%s node.js/configure' % URLopener.version

def reporthook(count, size, total):
    """internal hook used by retrievefile"""
    sys.stdout.write(' Fetch: %c %sMB total, %sMB downloaded   \r' %
                     (spin(count),
                      formatSize(total),
                      formatSize(count*size)))

def retrievefile(url, targetfile):
    """fetch file 'url' as 'targetfile'. Return targetfile or throw."""
    try:
        sys.stdout.write(' <%s>\nConnecting...\r' % url)
        sys.stdout.flush()
        ConfigOpener().retrieve(url, targetfile, reporthook=reporthook)
        print('')  # clear the line
        return targetfile
    except IOError as err:
        print(' ** IOError %s\n' % err)
        return None
    except:
        print(' ** Error occurred while downloading\n <%s>' % url)
        raise

def findHash(dict):
    """Find an available hash type."""
    # choose from one of these
    availAlgos = hashlib.algorithms_guaranteed
    for hashAlgo in availAlgos:
      if hashAlgo in dict:
        return (dict[hashAlgo], hashAlgo, availAlgos)
    # error
    return (None, None, availAlgos)

def checkHash(targetfile, hashAlgo):
    """Check a file using hashAlgo. Return the hex digest."""
    digest = hashlib.new(hashAlgo)
    with open(targetfile, 'rb') as f:
      chunk = f.read(1024)
      while len(chunk) > 0:
        digest.update(chunk)
        chunk = f.read(1024)
    return digest.hexdigest()

def unpack(packedfile, parent_path):
    """Unpacks packedfile into parent_path. Assumes .zip. Returns parent_path"""
    packedsuffix = packedfile.lower().split('.')[-1]  # .zip, .tgz etc
    if zipfile.is_zipfile(packedfile):
      print(' Extracting zip file: %s' % packedfile)
      with contextlib.closing(zipfile.ZipFile(packedfile, 'r')) as icuzip:
          icuzip.extractall(parent_path)
          return parent_path
    elif tarfile.is_tarfile(packedfile):
      # this will support gz, bz2, xz, etc. See tarfile.open()
      print(' Extracting tar file: %s' % packedfile)
      with contextlib.closing(tarfile.TarFile.open(packedfile, 'r')) as icuzip:
          icuzip.extractall(parent_path)
          return parent_path
    else:
        raise Exception('Error: Don\'t know how to unpack %s with extension %s' % (packedfile, packedsuffix))

# List of possible "--download=" types.
download_types = set(['icu'])

# Default options for --download.
download_default = "none"

def help():
  """This function calculates the '--help' text for '--download'."""
  return """Select which packages may be auto-downloaded.
valid values are: none, all, %s. (default is "%s").""" % (", ".join(download_types), download_default)

def set2dict(keys, value=None):
  """Convert some keys (iterable) to a dict."""
  return dict((key, value) for (key) in keys)

def parse(opt):
  """This function parses the options to --download and returns a set such as { icu: true }, etc. """
  if not opt:
    opt = download_default

  theOpts = set(opt.split(','))

  if 'all' in theOpts:
    # all on
    return set2dict(download_types, True)
  elif 'none' in theOpts:
    # all off
    return set2dict(download_types, False)

  # OK. Now, process each of the opts.
  theRet = set2dict(download_types, False)
  for anOpt in opt.split(','):
    if not anOpt or anOpt == "":
      # ignore stray commas, etc.
      continue
    elif anOpt == 'all':
      # all on
      theRet = dict((key, True) for (key) in download_types)
    else:
      # turn this one on
      if anOpt in download_types:
        theRet[anOpt] = True
      else:
        # future proof: ignore unknown types
        print('Warning: ignoring unknown --download= type "%s"' % anOpt)
  # all done
  return theRet

def candownload(auto_downloads, package):
  if not (package in auto_downloads.keys()):
    raise Exception('Internal error: "%s" is not in the --downloads list. Check nodedownload.py' % package)
  if auto_downloads[package]:
    return True
  else:
    print("""Warning: Not downloading package "%s". You could pass "--download=all"
    (Windows: "download-all") to try auto-downloading it.""" % package)
    return False
