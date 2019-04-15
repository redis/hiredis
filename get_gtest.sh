#!/bin/sh
# Run this from the toplevel directory of the source code tree
GTEST_URL_BASE=https://s3-eu-central-1.amazonaws.com/redislabs-dev-public-deps
GTEST_URL_BASE=https://github.com/google/googletest/archive/
GTEST_FILENAME=release-1.8.0.tar.gz
GTEST_TOPDIR=googletest-release-1.8.0
DESTDIR=contrib

if [ -d $DESTDIR/gtest ]; then
    exit 0
fi

curdir=$PWD
tarball=/tmp/${GTEST_FILENAME}
url=${GTEST_URL_BASE}/${GTEST_FILENAME}
if [ ! -e $tarball ]; then
    wget -O $tarball $url
fi

tar -C $DESTDIR -xf $tarball
rm $DESTDIR/gtest
cd $DESTDIR
ln -s $GTEST_TOPDIR gtest
