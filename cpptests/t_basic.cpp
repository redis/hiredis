#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <cstdarg>
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
}