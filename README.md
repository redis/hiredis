# HIREDIS

Hiredis is a minimalistic C client library for the [Redis](http://redis.io/) database.

It is minimalistic because it just adds minimal support for the protocol, but
at the same time it uses an high level printf-alike API in order to make it
much higher level than otherwise suggested by its minimal code base and the
lack of explicit bindings for every Redis command.

Apart from supporting sending commands and receiving replies, it comes with
a reply parser that is decoupled from the I/O layer. It
is a stream parser designed for easy reusability, which can for instance be used
in higher level language bindings for efficient reply parsing.

Hiredis only supports the binary-safe Redis protocol, so you can use it with any
Redis version >= 1.2.0.

The library comes with multiple APIs. There is the
*synchronous API*, the *asynchronous API* and the *reply parsing API*.

## SYNCHRONOUS API

To consume the synchronous API, there are only a few function calls that need to be introduced:

    redisContext *redisConnect(const char *ip, int port);
    void *redisCommand(redisContext *c, const char *format, ...);
    void freeReplyObject(void *reply);

### Connecting

The function `redisConnect` is used to create a so-called `redisContext`. The context is where
Hiredis holds state for a connection. The `redisContext` struct has an `error` field that is
non-NULL when the connection is in an error state. It contains a string with a textual
representation of the error. After trying to connect to Redis using `redisConnect` you should
check the `error` field to see if establishing the connection was successful:

    redisContext *c = redisConnect("127.0.0.1", 6379);
    if (c->error != NULL) {
      printf("Error: %s\n", c->error);
      // handle error
    }

### Sending commands

There are several ways to issue commands to Redis. The first that will be introduced is
`redisCommand`. This function takes a format similar to printf. In the simplest form,
it is used like this:

    reply = redisCommand(context, "SET foo bar");

The specifier `%s` interpolates a string in the command, and uses `strlen` to
determine the length of the string:

    reply = redisCommand(context, "SET foo %s", value);

When you need to pass binary safe strings in a command, the `%b` specifier can be
used. Together with a pointer to the string, it requires a `size_t` length argument
of the string:

    reply = redisCommand(context, "SET foo %b", value, valuelen);

Internally, Hiredis splits the command in different arguments and will
convert it to the protocol used to communicate with Redis.
One or more spaces separates arguments, so you can use the specifiers
anywhere in an argument:

    reply = redisCommand("SET key:%s %s", myid, value);

### Using replies

The return value of `redisCommand` holds a reply when the command was
successfully executed. When the return value is `NULL`, the `error` field
in the context can be used to find out what was the cause of failure.
Once an error is returned the context cannot be reused and you should set up
a new connection.

The standard replies that `redisCommand` are of the type `redisReply`. The
`type` field in the `redisReply` should be used to test what kind of reply
was received:

* `REDIS_REPLY_STATUS`:
    The command replied with a status reply. The status string can be accessed using `reply->str`.
    The length of this string can be accessed using `reply->len`.

* `REDIS_REPLY_ERROR`:
    The command replied with an error. The error string can be accessed identical to `REDIS_REPLY_STATUS`.

* `REDIS_REPLY_INTEGER`:
    The command replied with an integer. The integer value can be accessed using the
    `reply->integer` field of type `long long`.

* `REDIS_REPLY_NIL`:
    The command replied with a **nil** object. There is no data to access.

* `REDIS_REPLY_STRING`:
    A bulk (string) reply. The value of the reply can be accessed using `reply->str`.
    The length of this string can be accessed using `reply->len`.

* `REDIS_REPLY_ARRAY`:
    A multi bulk reply. The number of elements in the multi bulk reply is stored in
    `reply->elements`. Every element in the multi bulk reply is a `redisReply` object as well
    and can be accessed via `reply->elements[..index..]`.
    Redis may reply with nested arrays but this is fully supported.

Replies should be freed using the `freeReplyObject()` function.
Note that this function will take care of freeing sub-replies objects
contained in arrays and nested arrays, so there is no need for the user to
free the sub replies (it is actually harmful and will corrupt the memory).

## AUTHORS

Hiredis was written by Salvatore Sanfilippo (antirez at gmail) and
Pieter Noordhuis (pcnoordhuis at gmail) and is released under the BSD license.
