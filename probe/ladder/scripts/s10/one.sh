#!/bin/zsh
# usage: one.sh enc rate clip
enc=$1; r=$2; clip=$3; n=${clip:t:r}
d=p0/$r/$enc; mkdir -p $d
a=$d/$n.aac; rm -f $a $d/$n.dump
if [[ $enc == faac ]]; then wt/b/frontend/faac -a -b $r -o $a $clip >/dev/null 2>&1; else fdkaac -p 5 -b $r -f 2 -o $a $clip >/dev/null 2>&1; fi
FAAD_DUMP=$d/$n.dump dec/b/frontend/faad -o $d/$n.raw -f raw $a >/dev/null 2>&1; rm -f $d/$n.raw
