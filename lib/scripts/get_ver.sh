#!/bin/bash
wd=$(dirname $0)
awk '/AC_INIT/ {v=$2; gsub("[\\[\\],]*","", v); print v}' "$wd/../configure.ac"
