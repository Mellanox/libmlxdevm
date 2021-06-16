#!/bin/bash
name=libmlxdevm
bd=$(dirname $0)
wdir=$(dirname $bd)
cd $wdir

set -e
VER=$($bd/get_ver.sh)
TARBALL="$name-$VER.tar.gz"
if [ ! -e $TARBALL ] ; then
  rm -rf $name-$VER.tar.gz
  ./autogen.sh
  ./configure
  make dist
  tar -xvzf $name-$VER.tar.gz
  rm -f $name-$VER.tar.gz
  tar -cvzf $name-$VER.tar.gz $name-$VER --exclude=libmlxdevm
  rm -rf $name-$VER
fi

TOPDIR=$(rpmbuild -E "%_topdir")
if [ ! -n ${TOPDIR} ]; then
  mkdir -p ${TOPDIR}/{SOURCES,RPMS,SRPMS,SPECS,BUILD,BUILDROOT}
else
  mkdir -p $HOME/rpmbuild/{SOURCES,RPMS,SRPMS,SPECS,BUILD,BUILDROOT}
fi

if [ -z "$ghprbPullId" ]; then
    _rev="${BUILD_NUMBER:-1}"
else
    _rev="pr$ghprbPullId"
fi


echo "start rpm build..."
rpmbuild -ta $name-$VER.tar.gz
