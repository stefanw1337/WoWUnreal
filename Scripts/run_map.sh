#!/bin/bash
# Launch any WowUnreal map
# Usage: ./Scripts/run_map.sh <MapName> [build]
#   ./Scripts/run_map.sh CharacterTest        - run character test scene
#   ./Scripts/run_map.sh CharacterTest build  - build then run
#   ./Scripts/run_map.sh TerrainTest          - run terrain test
#   ./Scripts/run_map.sh WowWorld             - run full game (with login)
#
# Available maps: CharacterTest, TerrainTest, AnimationTest, MobTest,
#                 WmoTest, UITest, NetworkTest, StreamingTest, WowWorld

set -e

MAP_NAME="${1:-CharacterTest}"
BUILD_FLAG="$2"

PROJECT_DIR="/Users/clancey/Projects/WowUnreal"
UE_DIR="/Users/Shared/Epic Games/UE_5.7"
WOW_DATA="/Users/clancey/Downloads/World of Warcraft 3.3.5a/Data"
LOG_DIR="$HOME/Library/Logs/WowUnreal"

# Validate map exists
if [ ! -f "$PROJECT_DIR/Content/Maps/$MAP_NAME.umap" ]; then
    echo "Map '$MAP_NAME' not found. Available maps:"
    ls "$PROJECT_DIR/Content/Maps/"*.umap 2>/dev/null | xargs -I{} basename {} .umap
    exit 1
fi

# Kill any existing instance
pkill -f "UnrealEditor.*WowUnreal.*-game" 2>/dev/null || true
sleep 1

# Build if requested
if [ "$BUILD_FLAG" = "build" ]; then
    echo "Building..."
    "$UE_DIR/Engine/Build/BatchFiles/Mac/Build.sh" WowUnrealEditor Mac Development \
        -project="$PROJECT_DIR/WowUnreal.uproject" 2>&1 | tail -5
    echo ""
fi

find_log() {
    ls -t "$LOG_DIR"/WowUnreal*.log 2>/dev/null | head -1
}

SCREENSHOT_DIR="$PROJECT_DIR/Saved/Screenshots"
mkdir -p "$SCREENSHOT_DIR"
SCREENSHOT_PATH="$SCREENSHOT_DIR/${MAP_NAME}_verify.png"
rm -f "$SCREENSHOT_PATH"

# Build command-line args based on map type
EXTRA_ARGS=""
case "$MAP_NAME" in
    WowWorld)
        EXTRA_ARGS="-autologin -startpos"
        ;;
    NetworkTest)
        EXTRA_ARGS="-autologin"
        ;;
    *)
        # Test scenes don't need login
        ;;
esac

echo "Launching map: $MAP_NAME"
"$UE_DIR/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
    "$PROJECT_DIR/WowUnreal.uproject" \
    "/Game/Maps/$MAP_NAME" \
    -game -log -windowed -resx=1280 -resy=720 \
    "-wowdata=$WOW_DATA" \
    $EXTRA_ARGS \
    "-autoscreenshot=$SCREENSHOT_PATH" \
    -autoscreenshotdelay=15 \
    -ExecCmds="r.VolumetricFog 0,r.Fog 0" \
    2>/dev/null &

GAME_PID=$!
echo "Game PID: $GAME_PID"
echo "Waiting for scene to load..."
sleep 20

LOG_FILE=$(find_log)
if [ -z "$LOG_FILE" ]; then
    echo "No log file found!"
    exit 1
fi
echo "Log: $LOG_FILE"

echo ""
echo "=== Scene Setup ==="
grep -i "Setting up.*scene\|CharacterTest\|Spawning creature\|Spawning character\|SpawnM2\|LogWowTestMode\|LogWowCharacter" "$LOG_FILE" 2>/dev/null | tail -15

echo ""
echo "=== Errors ==="
grep -i "ensure.*failed\|fatal\|crash\|error.*wow\|Failed to compile" "$LOG_FILE" 2>/dev/null | grep -v "LogConfig\|LogInit\|LogAudio\|LogSlate\|CrashGUID\|CrashReport\|LogStreaming\|r.GPU\|LogHLSL\|LogRHI\|LogWowXml" | head -10

echo ""
echo "=== Screenshot ==="
if [ -f "$SCREENSHOT_PATH" ]; then
    SIZE=$(stat -f%z "$SCREENSHOT_PATH" 2>/dev/null || echo "0")
    echo "Screenshot saved: $SCREENSHOT_PATH ($SIZE bytes)"
else
    echo "Screenshot not yet saved (may appear after more frames render)"
fi

echo ""
echo "Game running. Press Ctrl+C to stop."
wait $GAME_PID 2>/dev/null
