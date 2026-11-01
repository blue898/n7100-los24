#!/usr/bin/env python3
"""Move hardcoded data directories in blobs to vendor-owned ones (same length, so the
strings are patched in place): vendor code may not use /data/system or /data/misc
(Treble SELinux).

usage: patch-paths.py <file> <old>=<new> ...
"""
import sys


def main():
    path = sys.argv[1]
    b = open(path, "rb").read()
    for arg in sys.argv[2:]:
        old, new = (s.encode() for s in arg.split("="))
        if len(old) != len(new):
            sys.exit("%s: %s and %s differ in length" % (path, old, new))
        n = b.count(old)
        if not n:
            sys.exit("%s: %s not found" % (path, old))
        b = b.replace(old, new)
        print("%s: %s -> %s (%d)" % (path, old.decode(), new.decode(), n))
    open(path, "wb").write(b)


if __name__ == "__main__":
    main()
