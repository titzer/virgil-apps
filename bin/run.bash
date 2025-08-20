#!/bin/bash

SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SOURCE" ]; do
  HERE="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"
  SOURCE="$(readlink "$SOURCE")"
  [[ $SOURCE != /* ]] && SOURCE="$HERE/$SOURCE"
done
HERE="$( cd -P "$( dirname "$SOURCE" )" >/dev/null 2>&1 && pwd )"

function usage() {
    echo "Usage: run.bash [aeneas...] <tiny|small|large|huge> [target [benchmarks]]"
		echo "       target is required when combining with aeneas"
    exit 1
}

RUNS=${RUNS:=5}

cd $HERE

APPS=../apps/

TMP=/tmp/$USER/virgil-bench
mkdir -p $TMP


if [ $# = 0 ]; then
	usage
fi

# size=$1
# shift
AENEAS_BINARY=()
marks=( {a..z} )
while [ $# != 0 ]
do 
	case $1 in
		tiny|small|large|huge)
			size=$1
			shift
			break
			;;
		*)
			AENEAS_BINARY+=( $1 )
			shift
			;;
	esac
done

if [ $# = 0 ]; then
	if [ -z $AENEAS_BINARY ]; then
		usage
	fi
	target="v3i"
else
	target="$1"
	shift
fi

if [ $# = 0 ]; then
	benchmarks=$(./list-benchmarks.bash)
else
	benchmarks="$*"
	shift
fi

mkdir -p $TMP

BTIME_BIN="btime-$(./sense_host | cut -d' ' -f1)"
if [ $? != 0 ]; then
	echo Could not sense host platform.
	exit 1
fi

BTIME="../btime/$BTIME_BIN"
if [ ! -x $BTIME ]; then
    echo Compiling btime.c...
    pushd ../btime
    cc -m32 -lm -O2 -o $BTIME_BIN btime.c
    popd
fi

#./compile.bash $target $benchmarks

for p in $benchmarks; do
	if [ ! -z $AENEAS_BINARY ]; then
		i=0
		for aeneas in "${AENEAS_BINARY[@]}"; do
			PROG=$TMP/${marks[$i]}/$p-$target
			if [ ! -x $PROG ]; then
				./compile.bash $aeneas ${marks[$i]} $target $p
			fi
			i=($i+1)
		done
	else
		# Check that binaries exist in $TMP
		PROG=$TMP/$p-$target
		if [ ! -x $PROG ]; then
			./compile.bash $target $p
		fi
	fi
done

for b in $benchmarks; do
    B=$APPS/$b
	if [ ! -f "$B/args-$size" ]; then
		continue
	fi

	if [ ! -z $AENEAS_BINARY ]; then
		args=$(cat $B/args-$size)
		echo "$b ($size): $args"

		i=0
		for aeneas in ${AENEAS_BINARY[@]}; do
			PROG=$HERE/out/$b-$target
			echo -n "$aeneas: "
			$BTIME -i $RUNS $PROG $args
			i=($i+1)
		done
	else
		PROG=$TMP/$b-$target
		args=$(cat $B/args-$size)
		
		echo "$b ($size): $PROG $args"
		$BTIME $RUNS $PROG $args
	fi
done
