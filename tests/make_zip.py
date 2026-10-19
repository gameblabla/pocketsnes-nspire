#!/usr/bin/env python3
"""Zips files for the zipped-ROM tests: make_zip.py OUT.zip deflate|store FILE[=NAME_IN_ZIP]..."""
import sys
import zipfile

out, method = sys.argv[1], sys.argv[2]
kind = zipfile.ZIP_DEFLATED if method == "deflate" else zipfile.ZIP_STORED
with zipfile.ZipFile(out, "w", kind) as z:
    for arg in sys.argv[3:]:
        path, _, name = arg.partition("=")
        z.write(path, name or path.rsplit("/", 1)[-1])
