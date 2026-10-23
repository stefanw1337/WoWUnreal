#!/bin/bash
# Quick launcher for MobTest scene (creature gallery)
# Usage: ./Scripts/run_mob_test.sh [build]
exec "$(dirname "$0")/run_map.sh" MobTest "$@"
