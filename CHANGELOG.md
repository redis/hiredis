### 0.12.0 - January 22, 2015

* Add optional KeepAlive support

* Try again on EINTR errors

* Add libuv adapter

* Add IPv6 support

* Remove possiblity of multiple close on same fd

* Add ability to bind source address on connect

* Add redisConnectFd() and redisFreeKeepFd()

* Fix getaddrinfo() memory leak

* Free string if it is unused (fixes memory leak)

* Improve redisAppendCommandArgv performance 2.5x

* Add support for SO_REUSEADDR

* Fix redisvFormatCommand format parsing

* Add GLib 2.0 adapter

* Refactor reading code into read.c

* Fix errno error buffers to not clobber errors

* Generate pkgconf during build

* Silence _BSD_SOURCE warnings

* Improve digit counting for multibulk creation


### 0.11.0

* Increase the maximum multi-bulk reply depth to 7.

* Increase the read buffer size from 2k to 16k.

* Use poll(2) instead of select(2) to support large fds (>= 1024).

### 0.10.1

* Makefile overhaul. Important to check out if you override one or more
  variables using environment variables or via arguments to the "make" tool.

* Issue #45: Fix potential memory leak for a multi bulk reply with 0 elements
  being created by the default reply object functions.

* Issue #43: Don't crash in an asynchronous context when Redis returns an error
  reply after the connection has been made (this happens when the maximum
  number of connections is reached).

### 0.10.0

* See commit log.

