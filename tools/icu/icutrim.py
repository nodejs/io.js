#!/usr/bin/python
#
# Copyright (C) 2014 IBM Corporation and Others. All Rights Reserved.
#
# @author Steven R. Loomis <srl@icu-project.org>
#
# This tool slims down an ICU data (.dat) file according to a config file.
#
# See: http://bugs.icu-project.org/trac/ticket/10922
#
# Usage:
#  Use "-h" to get help options.

from __future__ import print_function

import io
import json
import optparse
import os
import re
import shutil
import sys
import subprocess

try:
    # for utf-8 on Python 2
    reload(sys)
    sys.setdefaultencoding("utf-8")
except NameError:
    pass  # Python 3 already defaults to utf-8

try:
    basestring        # Python 2
except NameError:
    basestring = str  # Python 3

endian=sys.byteorder

parser = optparse.OptionParser(usage="usage: mkdir tmp ; %prog -D ~/Downloads/icudt53l.dat -T tmp -F trim_en.json -O icudt53l.dat" )

parser.add_option("-P","--tool-path",
                    action="store",
                    dest="toolpath",
                    help="set the prefix directory for ICU tools")

parser.add_option("-D","--input-file",
                    action="store",
                    dest="datfile",
                    help="input data file (icudt__.dat)",
                    )  # required

parser.add_option("-F","--filter-file",
                    action="store",
                    dest="filterfile",
                    help="filter file (JSON format)",
                    )  # required

parser.add_option("-T","--tmp-dir",
                    action="store",
                    dest="tmpdir",
                    help="working directory.",
                    )  # required

parser.add_option("--delete-tmp",
                    action="count",
                    dest="deltmpdir",
                    help="delete working directory.",
                    default=0)

parser.add_option("-O","--outfile",
                    action="store",
                    dest="outfile",
                    help="outfile  (NOT a full path)",
                    )  # required

parser.add_option("-v","--verbose",
                    action="count",
                    default=0)

parser.add_option('-L',"--locales",
                  action="store",
                  dest="locales",
                  help="sets the 'locales.only' variable",
                  default=None)

parser.add_option('-e', '--endian', action='store', dest='endian', help='endian, big, little or host, your default is "%s".' % endian, default=endian, metavar='endianness')

(options, args) = parser.parse_args()

optVars = vars(options)

for opt in [ "datfile", "filterfile", "tmpdir", "outfile" ]:
    if optVars[opt] is None:
        print("Missing required option: %s" % opt)
        sys.exit(1)

if options.verbose>0:
    print("Options: "+str(options))

if (os.path.isdir(options.tmpdir) and options.deltmpdir):
  if options.verbose>1:
    print("Deleting tmp dir %s.." % (options.tmpdir))
  shutil.rmtree(options.tmpdir)

if not (os.path.isdir(options.tmpdir)):
    os.mkdir(options.tmpdir)
else:
    print("Please delete tmpdir %s before beginning." % options.tmpdir)
    sys.exit(1)

if options.endian not in ("big","little","host"):
    print("Unknown endianness: %s" % options.endian)
    sys.exit(1)

if options.endian == "host":
    options.endian = endian

if not os.path.isdir(options.tmpdir):
    print("Error, tmpdir not a directory: %s" % (options.tmpdir))
    sys.exit(1)

if not os.path.isfile(options.filterfile):
    print("Filterfile doesn't exist: %s" % (options.filterfile))
    sys.exit(1)

if not os.path.isfile(options.datfile):
    print("Datfile doesn't exist: %s" % (options.datfile))
    sys.exit(1)

if not options.datfile.endswith(".dat"):
    print("Datfile doesn't end with .dat: %s" % (options.datfile))
    sys.exit(1)

outfile = os.path.join(options.tmpdir, options.outfile)

if os.path.isfile(outfile):
    print("Error, output file does exist: %s" % (outfile))
    sys.exit(1)

if not options.outfile.endswith(".dat"):
    print("Outfile doesn't end with .dat: %s" % (options.outfile))
    sys.exit(1)

dataname=options.outfile[0:-4]


def runcmd(tool, cmd, do_continue=False):
  if options.toolpath:
      cmd = os.path.join(options.toolpath, tool) + " " + cmd
  else:
      cmd = tool + " " + cmd

  if options.verbose > 4:
      print("# " + cmd)

  try:
      subprocess.check_call(cmd, shell=True)
  except subprocess.CalledProcessError as e:
      print("FAILED: %s" % cmd)
      if not do_continue:
          sys.exit(e.returncode)
      return e.returncode
  return 0

## STEP 0 - read in json config
with io.open(options.filterfile, encoding='utf-8') as fi:
    config = json.load(fi)

if options.locales:
    config["variables"] = config.get("variables", {})
    config["variables"]["locales"] = config["variables"].get("locales", {})
    config["variables"]["locales"]["only"] = options.locales.split(',')

if options.verbose > 6:
    print(config)

if "comment" in config:
    print("%s: %s" % (options.filterfile, config["comment"]))

## STEP 1 - copy the data file, swapping endianness
## The first letter of endian_letter will be 'b' or 'l' for big or little
endian_letter = options.endian[0]

runcmd("icupkg", "-t%s %s %s""" % (endian_letter, options.datfile, outfile))

## STEP 2 - get listing
listfile = os.path.join(options.tmpdir,"icudata.lst")
runcmd("icupkg", "-l %s > %s""" % (outfile, listfile))

with open(listfile, 'rb') as fi:
    items = [line.strip() for line in fi.read().decode("utf-8").splitlines()]
itemset = set(items)

if options.verbose > 1:
    print("input file: %d items" % len(items))

# list of all trees
trees = {}
RES_INDX = "res_index.res"
remove = None
# remove - always remove these
if "remove" in config:
    remove = set(config["remove"])
else:
    remove = set()

# keep - always keep these
if "keep" in config:
    keep = set(config["keep"])
else:
    keep = set()

def queueForRemoval(tree):
    global remove
    if tree not in config.get("trees", {}):
        return
    mytree = trees[tree]
    if options.verbose > 0:
        print("* %s: %d items" % (tree, len(mytree["locs"])))
    # do varible substitution for this tree here
    if isinstance(config["trees"][tree], basestring):
        treeStr = config["trees"][tree]
        if options.verbose > 5:
            print(" Substituting $%s for tree %s" % (treeStr, tree))
        if treeStr not in config.get("variables", {}):
            print(" ERROR: no variable:  variables.%s for tree %s" % (treeStr, tree))
            sys.exit(1)
        config["trees"][tree] = config["variables"][treeStr]
    myconfig = config["trees"][tree]
    if options.verbose > 4:
        print(" Config: %s" % (myconfig))
    # Process this tree
    if(len(myconfig)==0 or len(mytree["locs"])==0):
        if(options.verbose>2):
            print(" No processing for %s - skipping" % (tree))
    else:
        only = None
        if "only" in myconfig:
            only = set(myconfig["only"])
            if (len(only)==0) and (mytree["treeprefix"] != ""):
                thePool = "%spool.res" % (mytree["treeprefix"])
                if (thePool in itemset):
                    if(options.verbose>0):
                        print("Removing %s because tree %s is empty." % (thePool, tree))
                    remove.add(thePool)
        else:
            print("tree %s - no ONLY")
        for l in range(len(mytree["locs"])):
            loc = mytree["locs"][l]
            if (only is not None) and not loc in only:
                # REMOVE loc
                toRemove = "%s%s%s" % (mytree["treeprefix"], loc, mytree["extension"])
                if(options.verbose>6):
                    print("Queueing for removal: %s" % toRemove)
                remove.add(toRemove)

def addTreeByType(tree, mytree):
    if(options.verbose>1):
        print("(considering %s): %s" % (tree, mytree))
    trees[tree] = mytree
    mytree["locs"]=[]
    for i in range(len(items)):
        item = items[i]
        if item.startswith(mytree["treeprefix"]) and item.endswith(mytree["extension"]):
            mytree["locs"].append(item[len(mytree["treeprefix"]):-4])
    # now, process
    queueForRemoval(tree)

addTreeByType("converters",{"treeprefix":"", "extension":".cnv"})
addTreeByType("stringprep",{"treeprefix":"", "extension":".spp"})
addTreeByType("translit",{"treeprefix":"translit/", "extension":".res"})
addTreeByType("brkfiles",{"treeprefix":"brkitr/", "extension":".brk"})
addTreeByType("brkdict",{"treeprefix":"brkitr/", "extension":"dict"})
addTreeByType("confusables",{"treeprefix":"", "extension":".cfu"})

for i in range(len(items)):
    item = items[i]
    if item.endswith(RES_INDX):
        treeprefix = item[0:item.rindex(RES_INDX)]
        tree = None
        if treeprefix == "":
            tree = "ROOT"
        else:
            tree = treeprefix[0:-1]
        if(options.verbose>6):
            print("procesing %s" % (tree))
        trees[tree] = { "extension": ".res", "treeprefix": treeprefix, "hasIndex": True }
        # read in the resource list for the tree
        treelistfile = os.path.join(options.tmpdir,"%s.lst" % tree)
        runcmd("iculslocs", "-i %s -N %s -T %s -l > %s" % (outfile, dataname, tree, treelistfile))
        with io.open(treelistfile, 'r', encoding='utf-8') as fi:
            treeitems = fi.readlines()
            trees[tree]["locs"] = [line.strip() for line in treeitems]
        if tree not in config.get("trees", {}):
            print(" Warning: filter file %s does not mention trees.%s - will be kept as-is" % (options.filterfile, tree))
        else:
            queueForRemoval(tree)

def removeList(count=0):
    # don't allow "keep" items to creep in here.
    global remove
    remove = remove - keep
    if(count > 10):
        print("Giving up - %dth attempt at removal." % count)
        sys.exit(1)
    if(options.verbose>1):
        print("%d items to remove - try #%d" % (len(remove),count))
    if(len(remove)>0):
        oldcount = len(remove)
        hackerrfile=os.path.join(options.tmpdir, "REMOVE.err")
        removefile = os.path.join(options.tmpdir, "REMOVE.lst")
        with open(removefile, 'wb') as fi:
            fi.write('\n'.join(remove).encode("utf-8") + b'\n')
        rc = runcmd("icupkg","-r %s %s 2> %s" %  (removefile,outfile,hackerrfile),True)
        if rc != 0:
            if(options.verbose>5):
                print("## Damage control, trying to parse stderr from icupkg..")
            fi = open(hackerrfile, 'rb')
            erritems = fi.readlines()
            fi.close()
            #Item zone/zh_Hant_TW.res depends on missing item zone/zh_Hant.res
            pat = re.compile(br"^Item ([^ ]+) depends on missing item ([^ ]+).*")
            for i in range(len(erritems)):
                line = erritems[i].strip()
                m = pat.match(line)
                if m:
                    toDelete = m.group(1).decode("utf-8")
                    if(options.verbose > 5):
                        print("<< %s added to delete" % toDelete)
                    remove.add(toDelete)
                else:
                    print("ERROR: could not match errline: %s" % line)
                    sys.exit(1)
            if(options.verbose > 5):
                print(" now %d items to remove" % len(remove))
            if(oldcount == len(remove)):
                print(" ERROR: could not add any mor eitems to remove. Fail.")
                sys.exit(1)
            removeList(count+1)

# fire it up
removeList(1)

# now, fixup res_index, one at a time
for tree in trees:
    # skip trees that don't have res_index
    if "hasIndex" not in trees[tree]:
        continue
    treebunddir = options.tmpdir
    if(trees[tree]["treeprefix"]):
        treebunddir = os.path.join(treebunddir, trees[tree]["treeprefix"])
    if not (os.path.isdir(treebunddir)):
        os.mkdir(treebunddir)
    treebundres = os.path.join(treebunddir,RES_INDX)
    treebundtxt = "%s.txt" % (treebundres[0:-4])
    runcmd("iculslocs", "-i %s -N %s -T %s -b %s" % (outfile, dataname, tree, treebundtxt))
    runcmd("genrb","-d %s -s %s res_index.txt" % (treebunddir, treebunddir))
    runcmd("icupkg","-s %s -a %s%s %s" % (options.tmpdir, trees[tree]["treeprefix"], RES_INDX, outfile))
