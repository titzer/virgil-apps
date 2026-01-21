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
OUT_ROOT=$ROOT/out

function usage() {
    echo "Usage: run.bash <test|tiny|small|large|huge> [apps...]"
    echo ""
    echo "Sizes:"
    echo "  test   - run with args-test and compare output to output-test"
    echo "  tiny, small, large, huge - benchmark with btime"
    echo ""
    echo "Environment variables:"
    echo "  CONFIGS  - space-separated list of targets or target@runtime configurations"
    echo "             If only target is specified, runs all available runtimes for that target"
    echo "  RUNS     - number of runs for benchmarking (default: 5)"
    echo ""
    echo "Examples:"
    echo "  CONFIGS='wasm-wasi1' run.bash test           # test all wasm-wasi1 runtimes"
    echo "  CONFIGS='wasm-wasi1' run.bash small          # benchmark all wasm-wasi1 runtimes"
    echo "  CONFIGS='wasm-wasi1@node' run.bash small     # run only with node"
    echo ""
    echo "Compilation configurations are detected from out/ directories."
    echo "E.g., out/wasm-wasi1/ and out/wasm-wasi1@o2/ are both used."
    echo ""
    echo "Available runtime configurations in config/:"
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
    test|tiny|small|large|huge)
        ;;
    *)
        echo "Error: invalid size '$size'. Must be one of: test, tiny, small, large, huge"
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

# Set up btime for benchmarking (not needed for test mode)
if [ "$size" != "test" ]; then
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
fi

TMP=/tmp/$USER/virgil-run
mkdir -p $TMP

# Find all compilation configurations for a given base target
# Returns space-separated list of full target names (e.g., "wasm-wasi1 wasm-wasi1@o2")
function find_compile_configs() {
    local base_target=$1
    local configs=""

    # Look for directories matching base_target or base_target@*
    for dir in "$OUT_ROOT/$base_target" "$OUT_ROOT/$base_target"@*; do
        if [ -d "$dir" ]; then
            configs="$configs $(basename "$dir")"
        fi
    done
    echo $configs
}

# Run tests or benchmarks for each app and configuration
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
        # Extract base target from runtime config (e.g., "wasm-wasi1@node" -> "wasm-wasi1")
        base_target="${config%@*}"
        runtime="${config#*@}"

        # If no @ in config, runtime is same as base_target (native target)
        if [ "$base_target" = "$config" ]; then
            runtime=""
        fi

        runner="$CONFIG/run-$config"

        # Find all compilation configs for this base target
        compile_configs=$(find_compile_configs "$base_target")

        if [ -z "$compile_configs" ]; then
            printf "  %-30s (no compiled binaries)\n" "$config:"
            continue
        fi

        for compile_config in $compile_configs; do
            OUT="$OUT_ROOT/$compile_config"
            binary="$app.$compile_config"

            # Check if binary exists
            if [ ! -e "$OUT/$binary" ] && [ ! -e "$OUT/$binary.wasm" ] && [ ! -e "$OUT/$binary.jar" ]; then
                printf "  %-30s (skip: not compiled)\n" "$compile_config@$runtime:"
                continue
            fi

            # Format the display name
            if [ -n "$runtime" ]; then
                display_name="$compile_config@$runtime"
            else
                display_name="$compile_config"
            fi

            if [ "$size" = "test" ]; then
                # Test mode: run once and compare output
                expected="$APPDIR/output-test"
                if [ ! -f "$expected" ]; then
                    printf "  %-30s (skip: no output-test)\n" "$display_name:"
                    continue
                fi

                actual="$TMP/$app.$compile_config.out"
                "$runner" "$OUT" "$binary" $args > "$actual" 2>&1

                if diff -q "$expected" "$actual" > /dev/null 2>&1; then
                    printf "  %-30s ok\n" "$display_name:"
                else
                    printf "  %-30s FAIL\n" "$display_name:"
                fi
            else
                # Benchmark mode
                printf "  %-30s " "$display_name:"
                $BTIME -i $RUNS "$runner" "$OUT" "$binary" $args
            fi
        done
    done
done
