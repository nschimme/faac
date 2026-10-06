#!/bin/zsh
r=$1; clip=$2; n=${clip:t:r}; d=c20/$r; mkdir -p $d; a=$d/$n.aac; rm -f $a
w20/b/frontend/faac -a -b $r -o $a $clip >/dev/null 2>&1
