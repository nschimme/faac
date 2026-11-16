# Reference stream integrity (size + sha256 vs manifest)

| set | manifest rows | ok | bad |
|---|---|---|---|
| apple-48k | 924 | 924 | 0 |
| fdk-48k | 924 | 924 | 0 |
| xaac-48k | 539 | 539 | 0 |

apple-48k: on disk not in manifest: 0; in manifest not on disk: 0

fdk-48k: on disk not in manifest: 0; in manifest not on disk: 0

xaac-48k: on disk not in manifest: 0; in manifest not on disk: 0

No mismatches.
