#!/bin/sh
rm -rf autom4te.cache
#
# hack to force backward compatibility with the libtool
rm -f m4/libtool.m4 m4/lt*
autoreconf -v --force --install || exit 1
rm -rf autom4te.cache
#
# make sure configure does not complain if time is way off
find . -exec touch \{\} \;

