#!/bin/bash

if [ $# = 0 ]; then
	echo "Usage: compile.bash <target> [apps]"
	exit 1
fi

SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  HERE="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$HERE/$SOURCE"
done
HERE="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"

VIRGIL_LOC=${VIRGIL_LOC:=$(cd $HERE/.. && pwd)}

echo "V3C=\"$V3C\""
echo "V3C_OPTS=\"$V3C_OPTS\""
echo "VIRGIL_LOC=\"$VIRGIL_LOC\""

cd $HERE

TMP=/tmp/$USER/virgil-bench/
mkdir -p $TMP

target="$1"
shift

OUT=${OUT:=$HERE/../out/$target/}
mkdir -p $OUT
OUT=$(cd $OUT && pwd)

echo "OUT=\"$OUT\""

if [ $# = 0 ]; then
    apps=$(./list-apps.bash)
else
    apps="$*"
    shift
fi

function do_compile() {
    p=$1
    opts="${V3C_OPTS[@]}"
    PROG=$p.$target
    EXE=$OUT/$PROG
    ERROR_MSG=""

    cd $HERE/../apps/$p

    if [ -f TARGETS ]; then
	grep -q $target TARGETS > /dev/null
	if [ $? != 0 ]; then
	    ERROR_MSG=": skipping unsupported target: $target"
	    return 0
	fi
    fi

    # Add files from DEPS and DEPS-target
    files="*.v3"
    if [ -f "DEPS" ]; then
	files="$files $(cat DEPS)"
    fi
    if [ -f "DEPS-$target" ]; then
	files="$files $(cat DEPS-$target)"
    fi
    # Add options from V3C_OPTS and V3C_OPTS-target
    if [ -f "V3C_OPTS" ]; then
	opts="$opts $(cat V3C_OPTS)"
    fi
    if [ -f "V3C_OPTS-$target" ]; then
	opts="$opts $(cat V3C_OPTS-$target)"
    fi

    if [ ! -z "$opts" ]; then
	echo "  $opts"
    fi
    
    if [ "$target" = "v3i" ]; then
	# v3i is a special target that runs the V3C interpreter
	echo "#!/bin/bash" > $EXE
	echo "exec v3i $files \"$@\"" >> $EXE
	chmod 755 $EXE
	return 0
    elif [ "$target" = "v3i-ra" ]; then
	# v3i is a special target that runs the V3C interpreter (with -ra)
	echo "#!/bin/bash" > $EXE
	echo "exec v3i -ra $files \"$@\"" >> $EXE
	chmod 755 $EXE
	return 0
    else
	# compile to the given target architecture
	v3c-$target -output=$OUT -program-name=$PROG $opts $files
	return $?
    fi
}

ERROR_MSG=""

for x in $apps; do
    app=${x#apps/} # remove app/ prefix
    app=${app%%/}    # remove / suffixes
    if [ -z "$V3C" ]; then
	printf "##+compiling -target=%s %s\n" $target $app
    else
	printf "##+compiling V3C=%s -target=%s %s\n" "$V3C" $target $app
    fi
    do_compile $app

    if [ $? != 0 ]; then
	printf "##-fail%s\n" "$ERROR_MSG"
    else
	printf "##-ok%s\n" "$ERROR_MSG"
    fi
done
