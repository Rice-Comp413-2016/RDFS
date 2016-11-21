#include "zookeeper/zk_dn_client_test.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
using ::testing::AtLeast;

TEST(ThisClass, isSetUpRight) {
	ASSERT_EQ(6, 6);
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv);
    return RUN_ALL_TESTS();
}
