#!/bin/zsh
tag=$1; r=$2; clip=$3; n=${clip:t:r}; d=arm/$tag/$r; mkdir -p $d; a=$d/$n.aac; rm -f $a $d/$n.dump
wt/b/frontend/faac -a -b $r -o $a $clip >/dev/null 2>&1
FAAD_DUMP=$d/$n.dump dec/b/frontend/faad -o $d/$n.raw -f raw $a >/dev/null 2>&1; rm -f $d/$n.raw
