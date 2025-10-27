#!/usr/bin/env bash

set -e

# install the necessary dependencies
sudo apt-get update
sudo apt-get install -y \
    linux-libc-dev \
    build-essential \
    libdrm-dev

_ARCH=$(arch)

if [ "$_ARCH" = "x86_64" ]; then
  # x86-64 need to be generic for old machines
  export CFLAGS="-Wall -Wextra -std=gnu99 -O2 -D_GNU_SOURCE -march=x86-64 -mtune=generic -mno-sse4.1 -mno-sse4.2 -mno-avx -mno-avx2 -mno-fma -mno-f16c"

  make embedded
else
  make embedded
fi
