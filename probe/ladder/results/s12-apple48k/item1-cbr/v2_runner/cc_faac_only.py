import sys
sys.path.insert(0, '/home/user/work/fb')
import compare_codecs as cc
_orig = cc.detect_encoders
cc.detect_encoders = lambda a: [e for e in _orig(a) if e.name.startswith('FAAC')]
cc.main()
