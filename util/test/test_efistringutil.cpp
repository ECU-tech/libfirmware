#include <gtest/gtest.h>

#include <gerefi/efistringutil.h>

TEST(Util_String, equalsIgnoreCase) {
    ASSERT_FALSE(strEqualCaseInsensitive("a", "b"));
    ASSERT_TRUE(strEqualCaseInsensitive("a", "A"));
}
