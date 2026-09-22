#!/bin/bash

# Get the relative directory of the script to the current working directory
SCRIPT_DIR="$(dirname "$0")"

# Collect all .c files excluding pl_*.c
FILES=$(find "$SCRIPT_DIR" -maxdepth 1 -name '*.c' ! -name 'pl_*.c' ! -name 'ad_test.c')

# Replace newlines with spaces in the FILES variable
FILES=$(echo "$FILES" | tr '\n' ' ')

# Detect the OS (MSYS/MinGW or Linux)
if [ "$(uname | grep -i 'mingw')" ]; then
    # OS is MSYS/MinGW (Windows)
    FILES="$FILES $SCRIPT_DIR/pl_win32.c"
else
    # Assuming Linux if not MSYS/MinGW
    FILES="$FILES $SCRIPT_DIR/pl_linux.c"
fi

# Output the result in a single line
echo "$FILES"