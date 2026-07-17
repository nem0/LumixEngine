#!/usr/bin/env bash
# Automatically installs genie from the official repository

SCRIPT_DIR=$(cd $(dirname "${BASH_SOURCE[0]}") && pwd)
wget -P scripts -N https://github.com/bkaradzic/bx/raw/master/tools/bin/linux/genie
chmod +x scripts/genie
