#!/bin/sh

cat testfilesizes.lua |grep "^[1-9]"|tr -dc '[0-9\n]'|awk '{print "dd if=/dev/urandom of=data/f" $1 ".csv bs=1 count=" $1}'|sh
touch data/.gen
