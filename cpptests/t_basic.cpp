#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <climits>          // LLONG_MAX

#include "hiredis.h"
#include "c_tests.c"

class FormatterTest : public ::testing::Test {
};

class ReplyReaderTest : public ::testing::Test {
protected:
    redisReader *reader;
    void *reply = nullptr;;
    int ret;
    int i;    
    
    virtual void SetUp() {
        reader = redisReaderCreate();
    }
    virtual void TearDown() {
        if(reply == nullptr) { freeReplyObject(reply); }
        redisReaderFree(reader);
    }
};

class OtherTest : public ::testing::Test {
};

static std::string formatCommand(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = NULL;
    size_t n = redisvFormatCommand(&s, fmt, ap);
    va_end(ap);
    std::string xs(s, n);
    free(s);
    return xs;
}

#define INTEGER_WIDTH_TEST(fmt, type) do {                                                \
    type value = 123;                                                                     \
    len = redisFormatCommand(&cmd,"key:%08" fmt " str:%s", value, "hello");               \
    ASSERT_TRUE(strncmp(cmd,"*2\r\n$12\r\nkey:00000123\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2)) << "(" << #type << ")";                                \
    free(cmd);                                                                            \
} while(0)

#define FLOAT_WIDTH_TEST(type) do {                                                       \
    type value = 123.0;                                                                   \
    len = redisFormatCommand(&cmd,"key:%08.3f str:%s", value, "hello");                   \
    ASSERT_TRUE(strncmp(cmd,"*2\r\n$12\r\nkey:0123.000\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2)) << "(" << #type << ")";                                \
    free(cmd);                                                                            \
} while(0)


TEST_F(FormatterTest, testFormatCommands) {
    auto expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    ASSERT_STREQ(expected, formatCommand("SET foo bar").c_str())
        << "No interpolation";

    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    ASSERT_STREQ(expected, formatCommand("SET %s %s", "foo", "bar").c_str())
        << "interpolation";

    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n";
    ASSERT_STREQ(expected, formatCommand("SET %s %s", "foo", "").c_str())
        << "empty string";

    expected = "*3\r\n$3\r\nSET\r\n$0\r\n\r\n$3\r\nfoo\r\n";
    ASSERT_STREQ(expected, formatCommand("SET %s %s","","foo").c_str())
        << "an empty string in between proper interpolations";

    // NULL terminator requires special care
    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nb\0r\r\n";
    std::string result = formatCommand("SET %b %b","foo",(size_t)3,"b\0r",(size_t)3);
    bool cmp = memcmp(expected, result.data(), 4+4+(3+2)+4+(3+2)+4+(3+2));
    ASSERT_TRUE(cmp == 0) << "%%b string interpolation";

    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n";
    ASSERT_STREQ(expected, formatCommand("SET %b %b","foo",(size_t)3,"",(size_t)0).c_str())
        << "%%b and an empty string";

    expected =  "*3\r\n$3\r\nSET\r\n$1\r\n%\r\n$1\r\n%\r\n";
    ASSERT_STREQ(expected, formatCommand("SET %% %%").c_str())
        << "literal %%";
}

TEST_F(FormatterTest, testTypeWidth) {
    char *cmd;
    int len;    

    INTEGER_WIDTH_TEST("d", int);
    INTEGER_WIDTH_TEST("hhd", char);
    INTEGER_WIDTH_TEST("hd", short);
    INTEGER_WIDTH_TEST("ld", long);
    INTEGER_WIDTH_TEST("lld", long long);
    INTEGER_WIDTH_TEST("u", unsigned int);
    INTEGER_WIDTH_TEST("hhu", unsigned char);
    INTEGER_WIDTH_TEST("hu", unsigned short);
    INTEGER_WIDTH_TEST("lu", unsigned long);
    INTEGER_WIDTH_TEST("llu", unsigned long long);
    FLOAT_WIDTH_TEST(float);
    FLOAT_WIDTH_TEST(double);

    len = redisFormatCommand(&cmd,"key:%08p %b",(void*)1234,"foo",(size_t)3);
    ASSERT_TRUE(len == -1) << "invalid printf format"; 
}

TEST_F(FormatterTest, testArgs) {
    const char *argv[3];
    argv[0] = "SET";
    argv[1] = "foo\0xxx";
    argv[2] = "bar";
    size_t lens[3] = { 3, 7, 3 };
    int argc = 3;

    char *cmd;
    int len;
//    sds sds_cmd;

    len = redisFormatCommandArgv(&cmd,argc,argv,NULL);
    ASSERT_STREQ(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");
    ASSERT_TRUE(len == 4+4+(3+2)+4+(3+2)+4+(3+2)) << "passing argc/argv without lengths";
    free(cmd);

    len = redisFormatCommandArgv(&cmd,argc,argv,lens);
    ASSERT_STREQ(cmd, "*3\r\n$3\r\nSET\r\n$7\r\nfoo\0xxx\r\n$3\r\nbar\r\n");
    ASSERT_TRUE(len == 4+4+(3+2)+4+(7+2)+4+(3+2)) << "passing argc/argv with lengths";
    free(cmd);
/*
    sds_cmd = sdsempty();
    len = redisFormatSdsCommandArgv(&sds_cmd,argc,argv,NULL);
    ASSERT_TRUE(strncmp(sds_cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2)) << "passing argc/argv with lengths";
    sdsfree(sds_cmd);

    sds_cmd = sdsempty();
    len = redisFormatSdsCommandArgv(&sds_cmd,argc,argv,lens);
    ASSERT_TRUE(strncmp(sds_cmd,"*3\r\n$3\r\nSET\r\n$7\r\nfoo\0xxx\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(7+2)+4+(3+2)) << "sds by passing argc/argv with length";
    sdsfree(sds_cmd);    */
}

TEST_F(ReplyReaderTest, testErrorHandling) {
    redisReaderFeed(reader, "@foo\r\n", 6);
    ret = redisReaderGetReply(reader,NULL);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STRCASEEQ(reader->errstr,"Protocol error, got \"@\" as reply type byte");
}

TEST_F(ReplyReaderTest, testMemoryCleanup) {
    redisReaderFeed(reader, "*2\r\n", 4);
    redisReaderFeed(reader, "$5\r\nhello\r\n", 11);
    redisReaderFeed(reader, "@foo\r\n", 6);
    ret = redisReaderGetReply(reader,NULL);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STRCASEEQ(reader->errstr,"Protocol error, got \"@\" as reply type byte");
}

TEST_F(ReplyReaderTest, testMultiBulkGreater7) {
    for (i = 0; i < 9; i++) {
        redisReaderFeed(reader, "*1\r\n", 4);
    }

    ret = redisReaderGetReply(reader,NULL);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STRCASEEQ(reader->errstr, "No support for nested multi bulk replies with depth > 7");
}

TEST_F(ReplyReaderTest, testCorrectParseLLONG_MAX) {
    reader = redisReaderCreate();
    redisReaderFeed(reader, ":9223372036854775807\r\n", 22);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_OK &&
            ((redisReply*)reply)->type == REDIS_REPLY_INTEGER &&
            ((redisReply*)reply)->integer == LLONG_MAX);
}

TEST_F(ReplyReaderTest, testErrorGreaterLLONG_MAX) {
    redisReaderFeed(reader, ":9223372036854775808\r\n", 22);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STREQ(reader->errstr,"Bad integer value");
}

TEST_F(ReplyReaderTest, testCorrectParseLLONG_MIN) {
    redisReaderFeed(reader, ":-9223372036854775808\r\n", 23);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_OK &&
            ((redisReply*)reply)->type == REDIS_REPLY_INTEGER &&
            ((redisReply*)reply)->integer == LLONG_MIN);
}

TEST_F(ReplyReaderTest, testErrorSmallerLLONG_MIN) {
    redisReaderFeed(reader, ":-9223372036854775809\r\n", 23);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STREQ(reader->errstr,"Bad integer value");
}

TEST_F(ReplyReaderTest, testErrorArraySmallerThan1) {
    redisReaderFeed(reader, "*-2\r\n+asdf\r\n", 12);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STREQ(reader->errstr,"Multi-bulk length out of range");
}

TEST_F(ReplyReaderTest, testErrorBulkSmallerThan1) {
    redisReaderFeed(reader, "$-2\r\nasdf\r\n", 11);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STREQ(reader->errstr,"Bulk string length out of range");
 
}

TEST_F(ReplyReaderTest, testErrorArrayGreaterINT_MAX) {
    redisReaderFeed(reader, "*9223372036854775807\r\n+asdf\r\n", 29);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STREQ(reader->errstr,"Multi-bulk length out of range");
}

#if LLONG_MAX > SIZE_MAX
TEST_F(ReplyReaderTest, testErrorBulkSmallerThan1) {
    redisReaderFeed(reader, "$9223372036854775807\r\nasdf\r\n", 28);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_ERR);
    ASSERT_STREQ(reader->errstr,"Bulk string length out of range");
}
#endif

TEST_F(ReplyReaderTest, testNullReplyFunc) {
    reader->fn = NULL;
    redisReaderFeed(reader, "+OK\r\n", 5);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_OK && reply == (void*)REDIS_REPLY_STATUS);
}

TEST_F(ReplyReaderTest, testSingleNewlineTwoCalls) {
    reader->fn = NULL;
    redisReaderFeed(reader, "+OK\r", 4);
    ret = redisReaderGetReply(reader,&reply);
    assert(ret == REDIS_OK && reply == NULL);
    redisReaderFeed(reader, "\n", 1);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_OK && reply == (void*)REDIS_REPLY_STATUS);
}

TEST_F(ReplyReaderTest, testNoResetAfterProtocolError) {
    reader->fn = NULL;
    redisReaderFeed(reader, "x", 1);
    ret = redisReaderGetReply(reader,&reply);
    assert(ret == REDIS_ERR); //
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_ERR && reply == NULL);
}

/* Regression test for issue #45 on GitHub. */
TEST_F(ReplyReaderTest, testNoEmptyAlloc4EmptyBulk) {
    redisReaderFeed(reader, "*0\r\n", 4);
    ret = redisReaderGetReply(reader,&reply);
    ASSERT_TRUE(ret == REDIS_OK &&
        ((redisReply*)reply)->type == REDIS_REPLY_ARRAY &&
        ((redisReply*)reply)->elements == 0);
}

TEST_F(OtherTest, testDoubleFree) {
    redisContext *redisCtx = NULL;
    void *reply = NULL;

    redisFree(redisCtx);
    ASSERT_TRUE(redisCtx == NULL);

    freeReplyObject(reply);
    ASSERT_TRUE(reply == NULL);
}

TEST_F(OtherTest, testCTests) {
    int res = runHiredisCTests();   
    ASSERT_FALSE(res);
}