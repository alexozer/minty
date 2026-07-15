#!/usr/bin/env bash

dd if=/dev/zero of=poop.bin bs=1 count=161
cat data/cool-botw-bgs/rgb-test.jpg >> poop.bin
base64 -i poop.bin -w 80 -o poop.base64
