#!/bin/sh
# Third-party assets the engine ships with but the repo does not store.
set -e
cd "$(dirname "$0")"
mkdir -p assets/fonts
# Noto Sans JP (SIL Open Font License 1.1) - message text, Japanese and Latin
curl -sSfL -o assets/fonts/NotoSansJP.ttf "https://raw.githubusercontent.com/google/fonts/main/ofl/notosansjp/NotoSansJP%5Bwght%5D.ttf"
curl -sSfL -o assets/fonts/OFL.txt "https://raw.githubusercontent.com/google/fonts/main/ofl/notosansjp/OFL.txt"
echo "fonts ready"
