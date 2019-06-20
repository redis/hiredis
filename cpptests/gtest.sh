#!/bin/sh -ue

REDIS_SERVER=${REDIS_SERVER:-redis-server}
REDIS_PORT=${REDIS_PORT:-6379}
#SOCK_FILE=${SOCK_FILE:-/tmp/redis.sock}

tmpdir=$(mktemp -d)
PID_FILE=${tmpdir}/hiredis-test-redis.pid
SOCK_FILE=${tmpdir}/hiredis-test-redis.sock


cleanup() {
  set +e
  kill $(cat ${PID_FILE})
  rm -rf ${tmpdir}
}
trap cleanup INT TERM EXIT

${REDIS_SERVER} - <<EOF
daemonize yes
pidfile ${PID_FILE}
port ${REDIS_PORT}
bind 127.0.0.1
unixsocket ${SOCK_FILE}
unixsocketperm 700
EOF

${TEST_PREFIX:-} ./cpptests/hiredis-gtest