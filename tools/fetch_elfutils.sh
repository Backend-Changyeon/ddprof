#!/bin/bash

# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

set -euo pipefail
IFS=$'\n\t'

usage() {
    echo "Usage :"
    echo "$0 <version> <md5> <path> <c-compiler> <extra-c-flags>"
    echo ""
    echo "The extra c flags should be a single arg (hence quoted)"
    echo ""
    echo "Example"
    echo "  $0 0.183 6f58aa1b9af1a5681b1cbf63e0da2d67 ./vendor gcc \"-O0 -g\""
}

if [ "$#" -lt 4 ] || [ "$#" -ge 6 ]; then
    usage
    exit 1
fi

VER_ELF=$1
SHA512_ELF=$2
TARGET_EXTRACT=$3
C_COMPILER=${4}
EXTRA_CFLAGS=""
# C flags are optional
if [ "$#" -ge 5 ]; then
  EXTRA_CFLAGS=${5}
fi

mkdir -p "${TARGET_EXTRACT}"
cd "${TARGET_EXTRACT}"

TAR_ELF="elfutils-${VER_ELF}.tar.bz2"
URL_ELF="https://sourceware.org/elfutils/ftp/${VER_ELF}/${TAR_ELF}"

already_present=0
if [ -e "${TAR_ELF}" ]; then
    already_present=1
else
    curl -LO "${URL_ELF}"
fi

echo "Checking elfutils sha512"
if ! echo "${SHA512_ELF} ${TAR_ELF}" | sha512sum --check --strict --status; then
    echo "Error validating elfutils SHA512"
    echo "Please clear $TARGET_EXTRACT before restarting"
    exit 1
fi

[ $already_present -eq 1 ] && exit 0

echo "Extracting elfutils"
mkdir src
cd src
tar --no-same-owner --strip-components 1 -xf "../${TAR_ELF}"

# The flags below are hardcoded to work around clang compatibility issues in
# elfutils 186; these are irrelevant for GCC.  Note that this won't propagate
# envvars back up to the calling build system.
# TODO is there a better way to infer compiler family?
if [[ "$(basename "${C_COMPILER}")" == clang* ]]; then
  export CFLAGS="-Wno-xor-used-as-pow -Wno-gnu-variable-sized-type-not-at-end -Wno-unused-but-set-parameter"
fi
export CFLAGS="${CFLAGS-""} ${EXTRA_CFLAGS}"

echo "Compiling elfutils using ${C_COMPILER} / flags=$CFLAGS"

./configure CC="${C_COMPILER}" --without-bzlib --without-zstd --disable-debuginfod --disable-libdebuginfod --disable-symbol-versioning --prefix "${TARGET_EXTRACT}"
make "-j$(nproc)" install
