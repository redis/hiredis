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

### Sending commands (cont'd)

Together with `redisCommand`, the function `redisCommandArgv` can be used to issue commands.
It has the following prototype:

    void *redisCommandArgv(redisContext *c, int argc, const char **argv, const size_t *argvlen);

It takes the number of arguments `argc`, an array of strings `argv` and the lengths of the
arguments `argvlen`. For convenience, `argvlen` may be set to `NULL` and the function will
use `strlen(3)` on every argument to determine its length. Obviously, when any of the arguments
need to be binary safe, the entire array of lengths `argvlen` should be provided.

The return value has the same semantic as `redisCommand`.

### Pipelining

To explain how Hiredis supports pipelining in a blocking connection, there needs to be
understanding of the internal execution flow.

When any of the functions in the `redisCommand` family is called, Hiredis first formats the
command according to the Redis protocol. The formatted command is then put in the output buffer
of the context. This output buffer is dynamic, so it can hold any number of commands.
After the command is put in the output buffer, `redisGetReply` is called. This function has the
following two execution paths:

1. The input buffer is non-empty:
  * Try to parse a single reply from the input buffer and return it
  * If no reply could be parsed, continue at *2*
2. The input buffer is empty:
  * Write the **entire** output buffer to the socket
  * Read from the socket until a single reply could be parsed

The function `redisGetReply` is exported as part of the Hiredis API and can be used when a reply
is expected on the socket. To pipeline commands, the only things that needs to be done is
filling up the output buffer. For this cause, two commands can be used that are identical
to the `redisCommand` family, apart from not returning a reply:

    void redisAppendCommand(redisContext *c, const char *format, ...);
    void redisAppendCommandArgv(redisContext *c, int argc, const char **argv, const size_t *argvlen);

After calling either function one or more times, `redisGetReply` can be used to receive the
subsequent replies. The return value for this function is either `REDIS_OK` or `REDIS_ERR`, where
the latter means an error occurred while reading a reply. Just as with the other commands,
the `error` field in the context can be used to find out what the cause of this error is.

The following examples shows a simple pipeline (resulting in only a single call to `write(2)` and
a single call to `write(2)`):

    redisReply *reply;
    redisAppendCommand(context,"SET foo bar");
    redisAppendCommand(context,"GET foo");
    redisGetReply(context,&reply); // reply for SET
    freeReplyObject(reply);
    redisGetReply(context,&reply); // reply for GET
    freeReplyObject(reply);

This API can also be used to implement a blocking subscriber:

    reply = redisCommand(context,"SUBSCRIBE foo");
    freeReplyObject(reply);
    while(redisGetReply(context,&reply) == REDIS_OK) {
      // consume message
      freeReplyObject(reply);
    }

## AUTHORS

Hiredis was written by Salvatore Sanfilippo (antirez at gmail) and
Pieter Noordhuis (pcnoordhuis at gmail) and is released under the BSD license.
