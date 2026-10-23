#!/bin/bash
# Quick launcher for CharacterTest scene
# Usage: ./Scripts/run_character_test.sh [build]
exec "$(dirname "$0")/run_map.sh" CharacterTest "$@"
