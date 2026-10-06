#!/bin/zsh
r=$1; clip=$2; n=${clip:t:r}; d=p0/$r/apple; mkdir -p $d
a=$d/$n.m4a; rm -f $a $d/$n.dump
afconvert -f m4af -d aach -b ${r}000 $clip $a >/dev/null 2>&1
FAAD_DUMP=$d/$n.dump dec/b/frontend/faad -o $d/$n.raw -f raw $a >/dev/null 2>&1; rm -f $d/$n.raw
