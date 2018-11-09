#!/usr/bin/env bash
set -xeo pipefail

# for jenkins, assumes cwd == $WORKSPACE
export XODEE_SRC=${XODEE_SRC-`pwd`}
export XODEE_EXTERNAL=${XODEE_SRC}/external

PACKAGE=hiredis

BUCKET=aws-uc-us-west-1-packages
BUCKET_URL=s3://$BUCKET/tarballs/$PACKAGE

ensure_dependency()
{
  jenkins_job=$1
  project=$2
  alias=$3

  set +e
  [ -z "$alias" ] && alias=$project
  set -e
  rm -f $alias
  ln -vsf ../../$jenkins_job/workspace/$project $alias
}

build_assets()
{
  pushd $PACKAGE
  DESTDIR=tmp make install
  tar -C tmp -czvf $PACKAGE/package.tgz .
  popd
}

deploy_assets()
{
  pushd $PACKAGE
  current_hash=$(git log --pretty=format:'%H' -n 1)
  aws s3 cp package.tgz $BUCKET_URL/$current_hash-$BUILD_NUMBER.tgz
  popd
}

build_assets
deploy_assets

