#!/bin/bash

# Stops on first error, echo on
set -e
# set -x

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/build-common.sh"
ensure_uv_build_env "$0" "$@"

: "${SRCDIR:=$(dirname "$(pwd)/$0")/..}"

: ${FLAVOR:="tx16s;tx16smk3"}
: ${COMMON_OPTIONS:="-DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_RULE_MESSAGES=OFF -Wno-dev -DCMAKE_MESSAGE_LOG_LEVEL=WARNING"}

# wipe build directory clean
rm -rf build && mkdir -p build && cd build

target_names=$(echo "$FLAVOR" | tr '[:upper:]' '[:lower:]' | tr ';' '\n')

for target_name in $target_names
do
    BUILD_OPTIONS=${COMMON_OPTIONS}
    BUILD_OPTIONS+=" $EXTRA_OPTIONS "

    echo "Processing ${target_name}"

    if ! get_target_build_options "$target_name"; then
        echo "Error: Failed to find a match for target '$target_name'"
        exit 1
    fi

    cmake ${BUILD_OPTIONS} "${SRCDIR}"
    cmake --build . --target arm-none-eabi-configure
    cmake --build arm-none-eabi --target hardware_defs

    rm -f CMakeCache.txt arm-none-eabi/CMakeCache.txt
done
