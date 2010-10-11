HIREDIS OVERVIEW
----------------

Hiredis is a minimalistic C client library for the Redis Database.

It is minimalistic because it just adds minimal support for the protocol, but
at the same time it uses an high level printf-alike API in order to make it
much higher level than otherwise suggested by its minimal code base and the
lack of explicit bindings for every Redis command.

Hiredis only supports the new Redis protocol, so you can use it with any
Redis version >= 1.2.0.

HIREDIS API
-----------

Hiredis exports only three function calls:

    redisReply *redisConnect(int *fd, char *ip, int port);
    redisReply *redisCommand(int fd, char *format, ...);
    void freeReplyObject(redisReply *r);

The first function is used in order to create a connection to the Redis server:

    redisReply *reply;
    int fd;

    reply = redisConnect(&fd,"127.0.0.1",6379);

to test for connection errors all it is needed to do is checking if reply
is not NULL:

    if (reply != NULL) {
        printf("Connection error: %s\n", reply->reply);
        freeReplyObject(reply);
        exit(1);
    }

When a reply object returns an error, the reply->type is set to the value
`REDIS_REPLY_ERROR`, and reply->reply points to a C string with the description
of the error.

In the above example we don't check for reply->type as `redisConnect()` can
only return `NULL` or a reply object that is actually an error.

As you can see `redisConnect()` will just set (by reference) the `fd` variable
to the file descriptor of the open socket connected to our Redis server.

Calls to `redisCommand()` will require this file descriptor as first argument.

SENDING COMMANDS
----------------

Commands are sent using a printf-alike format. In the simplest form it is
like that:

    reply = redisCommand("SET foo bar");

But you can use "%s" and "%b" format specifiers to create commands in a
printf-alike fashion:

    reply = redisComand("SET foo %s", somevalue);

If your arguments are binary safe, you can use "%b" that receives the pointer
to the buffer and a size_t integer with the length of the buffer.

    reply = redisCommand("SET %s %b", "foo", somevalue, somevalue_length);

Internally Hiredis will split the command in different arguments and will
convert it to the actual protocol used to communicate with Redis.
Every space will separate arguments, so you can use interpolation.
The following example is valid:

    reply = redisCommand("SET key:%s %s", myid, value);

USING REPLIES
-------------

`redisCommand()` returns a reply object. In order to use this object you
need to test the reply->type field, that can be one of the following types:

* `REDIS_REPLY_ERROR`:
    The command returned an error string, that can be read accessing to
    the reply->reply field.

* `REDIS_REPLY_STRING`:
    The command returned a string, that can be read accessing to the
    reply->reply field. The string is always null-terminated, but when you
    need to work with binary-safe strings you can obtain the exact length
    of the reply with: `sdslen(reply->reply)`.

* `REDIS_REPLY_ARRAY`:
    The command returned an array of reply->elements elements.
    Every element is a redisReply object, stored at redis->element[..index..]
    Redis may reply with nested arrays but this is fully supported.

* `REDIS_REPLY_INTEGER`:
    The command replies with an integer. It's possible to access this integer
    using the reply->integer field that is of type "long long".

* `REDIS_REPLY_NIL`:
    The command replies with a NIL special object. There is no data to access.

FREEING REPLIES
---------------

Replies should be freed using the `freeReplyObject()` function.
Note that this function will take care of freeing sub-replies objects
contained in arrays and nested arrays, so there is no need for the user to
free the sub replies (it is actually harmful and will corrupt the memory).

AUHTOR
------

Hiredis was written by Salvatore Sanfilippo (antirez at gmail) and is
released under the BSD license.
