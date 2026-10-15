cd /tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad/run
AUDDIR=/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad/fb/data/external/audio_44k1 TAG=44k1 RUNGSTR=40,48,56,64 ../venv/bin/python ladder.py fix15,fix9,fix11 > run_44.log 2>&1
AUDDIR=/tmp/claude-0/-home-user-faac/bc9c8615-9277-5565-96c9-f1ff39d121b8/scratchpad/fb/data/external/audio_32k TAG=32k RUNGSTR=32,40,48,56 ../venv/bin/python ladder.py fix15,fix9,fix11 > run_32.log 2>&1
