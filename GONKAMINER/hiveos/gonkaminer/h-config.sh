#!/usr/bin/env bash
mkfile_from_symlink $CUSTOM_CONFIG_FILENAME
# One KEY=VAL per line so h-run.sh can parse flight-sheet extra config.
echo "$CUSTOM_USER_CONFIG" | tr ' ' '\n' | grep -E '^[A-Za-z_][A-Za-z0-9_]*=' > "$CUSTOM_CONFIG_FILENAME" || true
