#!/bin/sh
set -eu
cd "$(dirname "$0")"
revision=90d65ba059d91078a34b9c26c8772ee14b556a13
if [ ! -d .deps/lufa/.git ]; then
    mkdir -p .deps
    git clone --no-checkout https://github.com/abcminiuser/lufa.git .deps/lufa
fi
git -C .deps/lufa checkout --detach "$revision"
