#!/bin/bash
# Quick launcher for WowWorld (full game with terrain + server connection)
# Usage: ./Scripts/run_world.sh [build]
exec "$(dirname "$0")/run_map.sh" WowWorld "$@"
