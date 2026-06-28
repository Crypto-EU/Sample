#!/usr/bin/env bash
mkfile_from_symlink $CUSTOM_CONFIG_FILENAME
echo "$CUSTOM_USER_CONFIG" > $CUSTOM_CONFIG_FILENAME
