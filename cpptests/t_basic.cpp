#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstdarg>
#include <cstdio>

#include "hiredis.h"

class FormatterTest : public ::testing::Test {
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
    ASSERT_EQ(expected, formatCommand("SET foo bar"))
        << "No interpolation";

    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    ASSERT_EQ(expected, formatCommand("SET %s %s", "foo", "bar"))
        << "interpolation";

    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n";
    ASSERT_EQ(expected, formatCommand("SET %s %s", "foo", ""))
        << "empty string";

    expected = "*3\r\n$3\r\nSET\r\n$0\r\n\r\n$3\r\nfoo\r\n";
    ASSERT_EQ(expected, formatCommand("SET %s %s","","foo"))
        << "an empty string in between proper interpolations";

    // NULL terminator requires special care
    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nb\0r\r\n";
    std::string result = formatCommand("SET %b %b","foo",(size_t)3,"b\0r",(size_t)3);
    bool cmp = memcmp(expected, result.data(), 4+4+(3+2)+4+(3+2)+4+(3+2));
    ASSERT_TRUE(cmp == 0) << "%%b string interpolation";

    expected = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n";
    ASSERT_EQ(expected, formatCommand("SET %b %b","foo",(size_t)3,"",(size_t)0))
        << "%%b and an empty string";

    expected =  "*3\r\n$3\r\nSET\r\n$1\r\n%\r\n$1\r\n%\r\n";
    ASSERT_EQ(expected, formatCommand("SET %% %%"))
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
    ASSERT_TRUE(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2)) << "passing argc/argv without lengths";
    free(cmd);

    len = redisFormatCommandArgv(&cmd,argc,argv,lens);
    ASSERT_TRUE(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$7\r\nfoo\0xxx\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(7+2)+4+(3+2)) << "passing argc/argv with lengths";
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

