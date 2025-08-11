#!/bin/bash

HERE=$(builtin cd $(dirname ${BASH_SOURCE[0]}) >/dev/null && builtin pwd)
echo $(cd $HERE/../apps; ls */*.v3 | sort | cut -d/ -f1 | uniq)
