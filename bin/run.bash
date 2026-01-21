#!/bin/bash

SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  HERE="$(builtin cd -P "$(dirname "$SOURCE")" >/dev/null 2>&1 && pwd)"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$HERE/$SOURCE"
done
HERE="$(builtin cd -P "$(dirname "$SOURCE")" >/dev/null 2>&1 && pwd)"

ROOT=$(builtin cd "$HERE/.." && pwd)
CONFIG=$ROOT/config
APPS=$ROOT/apps

function usage() {
    echo "Usage: run.bash <tiny|small|large|huge> [apps...]"
    echo ""
    echo "Environment variables:"
    echo "  CONFIGS  - space-separated list of targets or target@runtime configurations"
    echo "             If only target is specified, runs all available runtimes for that target"
    echo "  RUNS     - number of runs for benchmarking (default: 5)"
    echo ""
    echo "Examples:"
    echo "  CONFIGS='wasm-wasi1' run.bash small          # compare all wasm-wasi1 runtimes"
    echo "  CONFIGS='wasm-wasi1@node' run.bash small     # run only with node"
    echo "  CONFIGS='wasm-wasi1 jar' run.bash small      # compare wasm and jar targets"
    echo ""
    echo "Available configurations in config/:"
    ls "$CONFIG"/run-* 2>/dev/null | xargs -n1 basename | sed 's/^run-/  /'
    exit 1
}

RUNS=${RUNS:=5}

if [ $# = 0 ]; then
    usage
fi

# Parse size argument
size=$1
shift

case $size in
    tiny|small|large|huge)
        ;;
    *)
        echo "Error: invalid size '$size'. Must be one of: tiny, small, large, huge"
        usage
        ;;
esac

# Get apps list
if [ $# = 0 ]; then
    apps=$("$HERE/list-apps.bash")
else
    apps="$*"
fi

# Get configurations from environment or detect available ones
if [ -z "$CONFIGS" ]; then
    echo "Error: CONFIGS environment variable not set."
    echo ""
    echo "Set CONFIGS to a space-separated list of configurations to run."
    echo "Example: CONFIGS='wasm-wasi1' $0 $size        # runs all wasm-wasi1 runtimes"
    echo "Example: CONFIGS='wasm-wasi1@node' $0 $size   # runs only node runtime"
    echo ""
    usage
fi

# Expand configs: if a config has no @runtime, expand to all matching runners
expanded_configs=""
for config in $CONFIGS; do
    if [[ "$config" == *"@"* ]]; then
        # Specific runtime requested
        runner="$CONFIG/run-$config"
        if [ ! -x "$runner" ]; then
            echo "Error: runner not found or not executable: $runner"
            echo "Run ./configure to set up runners."
            exit 1
        fi
        expanded_configs="$expanded_configs $config"
    else
        # No runtime specified - find all matching runners for this target
        matches=$(ls "$CONFIG"/run-"$config"@* 2>/dev/null | xargs -n1 basename 2>/dev/null | sed 's/^run-//')
        if [ -z "$matches" ]; then
            # Try exact match (e.g., native targets like x86-64-darwin)
            if [ -x "$CONFIG/run-$config" ]; then
                expanded_configs="$expanded_configs $config"
            else
                echo "Error: no runners found for target '$config'"
                echo "Run ./configure to set up runners."
                exit 1
            fi
        else
            expanded_configs="$expanded_configs $matches"
        fi
    fi
done
CONFIGS=$expanded_configs

# Set up btime for benchmarking
BTIME_BIN="btime-$("$HERE/sense_host" | cut -d' ' -f1)"
if [ $? != 0 ]; then
    echo "Error: could not sense host platform."
    exit 1
fi

BTIME="$ROOT/btime/$BTIME_BIN"
if [ ! -x "$BTIME" ]; then
    echo "Compiling btime.c..."
    pushd "$ROOT/btime" > /dev/null
    cc -m32 -lm -O2 -o "$BTIME_BIN" btime.c
    popd > /dev/null
fi

# For each configuration, determine the target and compile if needed
for config in $CONFIGS; do
    # Extract target from config (e.g., "wasm-wasi1@node" -> "wasm-wasi1", "x86-64-darwin" -> "x86-64-darwin")
    target="${config%@*}"
    OUT="$ROOT/out/$target"

    for app in $apps; do
        app=${app#apps/}   # remove apps/ prefix
        app=${app%%/}      # remove / suffixes

        # Check if binary exists, compile if not
        if [ ! -e "$OUT/$app.$target" ] && [ ! -e "$OUT/$app.$target.wasm" ] && [ ! -e "$OUT/$app.$target.jar" ]; then
            "$HERE/compile.bash" "$target" "$app"
        fi
    done
done

# Run benchmarks for each app and configuration
for app in $apps; do
    app=${app#apps/}   # remove apps/ prefix
    app=${app%%/}      # remove / suffixes

    APPDIR="$APPS/$app"

    # Skip if no args file for this size
    if [ ! -f "$APPDIR/args-$size" ]; then
        continue
    fi

    args=$(cat "$APPDIR/args-$size")
    echo ""
    echo "=== $app ($size): $args ==="

    for config in $CONFIGS; do
        target="${config%@*}"
        OUT="$ROOT/out/$target"
        runner="$CONFIG/run-$config"

        printf "  %-24s " "$config:"
        $BTIME -i $RUNS "$runner" "$OUT" "$app.$target" $args
    done
done
