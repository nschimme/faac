"""Load FAAC_MS_DUMP records keyed by (frame, band index g*max_sfb... resolved later)."""
import collections
def load_ms(path):
    d=collections.defaultdict(dict)
    for line in open(path):
        f,g,sfb,sh,el,er,elr,w,u=line.split();d[int(f)][(int(g),int(sfb))]=(int(sh),float(el),float(er),float(elr),int(w),int(u))
    return d
