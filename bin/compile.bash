#!/bin/bash

if [ $# = 0 ]; then
	echo "Usage: compile.bash <target[@config]> [apps]"
	echo ""
	echo "Examples:"
	echo "  compile.bash jar              # compile for jar target"
	echo "  compile.bash jar@o2           # compile for jar with config/V3C_OPTS@o2 options"
	exit 1
fi

SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  HERE=$(builtin cd -P $(dirname "$SOURCE") >/dev/null 2>&1 && builtin pwd)
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$HERE/$SOURCE"
done
HERE=$(builtin cd -P $(dirname "$SOURCE") >/dev/null 2>&1 && builtin pwd)

ROOT=$(builtin cd "$HERE/.." && pwd)
CONFIG_DIR=$ROOT/config

VIRGIL_LOC=${VIRGIL_LOC:=$(cd $HERE/.. && pwd)}

if [ -z "$V3C" ]; then
    if [ -x "$CONFIG_DIR/v3c" ]; then
	V3C=$(builtin cd $CONFIG_DIR && pwd)/v3c
    else
	V3C=$(which v3c)
    fi
fi

echo "V3C=\"$V3C\""
echo "V3C_OPTS=\"$V3C_OPTS\""
echo "VIRGIL_LOC=\"$VIRGIL_LOC\""

cd $HERE

TMP=/tmp/$USER/virgil-bench/
mkdir -p $TMP

# Parse target[@config]
full_target="$1"
shift

# Extract base target and compilation config
if [[ "$full_target" == *"@"* ]]; then
    base_target="${full_target%@*}"
    compile_config="${full_target#*@}"
else
    base_target="$full_target"
    compile_config=""
fi

# Read compilation config options from config/V3C_OPTS@<config>
config_opts=""
if [ -n "$compile_config" ]; then
    config_file="$CONFIG_DIR/V3C_OPTS@$compile_config"
    if [ -f "$config_file" ]; then
	config_opts=$(cat "$config_file")
	echo "CONFIG_OPTS=\"$config_opts\" (from $config_file)"
    else
	echo "Warning: config file not found: $config_file"
    fi
fi

OUT=${OUT:=$ROOT/out/$full_target/}
mkdir -p $OUT
OUT=$(builtin cd $OUT && pwd)

echo "OUT=\"$OUT\""

if [ $# = 0 ]; then
    apps=$(./list-apps.bash)
else
    apps="$*"
    shift
fi

function do_compile() {
    p=$1
    opts="${V3C_OPTS[@]} $config_opts"
    PROG=$p.$full_target
    EXE=$OUT/$PROG
    ERROR_MSG=""

    cd $HERE/../apps/$p

    if [ -f TARGETS ]; then
	grep -q $base_target TARGETS > /dev/null
	if [ $? != 0 ]; then
	    ERROR_MSG=": skipping unsupported target: $base_target"
	    return 0
	fi
    fi

    # Add files from DEPS and DEPS-target
    files="*.v3"
    if [ -f "DEPS" ]; then
	files="$files $(cat DEPS)"
    fi
    if [ -f "DEPS-$base_target" ]; then
	files="$files $(cat DEPS-$base_target)"
    fi
    # Add options from V3C_OPTS and V3C_OPTS-target
    if [ -f "V3C_OPTS" ]; then
	opts="$opts $(cat V3C_OPTS)"
    fi
    if [ -f "V3C_OPTS-$base_target" ]; then
	opts="$opts $(cat V3C_OPTS-$base_target)"
    fi

    if [ ! -z "$opts" ]; then
	echo "  $opts"
    fi

    if [ "$base_target" = "v3i" ]; then
	# v3i is a special target that runs the V3C interpreter
	echo "#!/bin/bash" > $EXE
	echo "exec v3i $files \"$@\"" >> $EXE
	chmod 755 $EXE
	return 0
    elif [ "$base_target" = "v3i-ra" ]; then
	# v3i is a special target that runs the V3C interpreter (with -ra)
	echo "#!/bin/bash" > $EXE
	echo "exec v3i -ra $files \"$@\"" >> $EXE
	chmod 755 $EXE
	return 0
    else
	# compile to the given target architecture
	v3c-$base_target -output=$OUT -program-name=$PROG $opts $files
	return $?
    fi
}

ERROR_MSG=""

for x in $apps; do
    app=${x#apps/} # remove app/ prefix
    app=${app%%/}    # remove / suffixes
    if [ -z "$V3C" ]; then
	printf "##+compiling -target=%s %s\n" $full_target $app
    else
	printf "##+compiling V3C=%s -target=%s %s\n" "$V3C" $full_target $app
    fi
    do_compile $app

    if [ $? != 0 ]; then
	printf "##-fail%s\n" "$ERROR_MSG"
    else
	printf "##-ok%s\n" "$ERROR_MSG"
    fi
done
