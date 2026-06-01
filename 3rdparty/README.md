# Third Party Dependencies

A major goal of this project is to be very judicious about which third party dependencies we include. Limiting performance overhead, complexity, and bloat is very important.

## yyjson

Took yyjson.c and yyjson.h from master commit 815783186bc3a0754f5c090b896ce2f544e9fe29

## simdutf

Checked out v0.9.0 and created an "amalgomated" single-file header+source with:

```
./amalgamate.py --with-utf8 --with-utf16 --with-base64
```
