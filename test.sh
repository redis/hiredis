#!/bin/sh -ue

REDIS_SERVER=${REDIS_SERVER:-redis-server}
REDIS_PORT=${REDIS_PORT:-56379}

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
EOF

${TEST_PREFIX:-} ./hiredis-test -h 127.0.0.1 -p ${REDIS_PORT} -s ${SOCK_FILE}
