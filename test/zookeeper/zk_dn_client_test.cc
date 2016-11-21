#include "zk_dn_client_test.h"
#include "zk_dn_client.h"
#include "zk_client_common.h"
#include "zkwrapper.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define ELPP_THREAD_SAFE

class thisZKDNClientTest : ZKDNClientTest {
};

int main(int argc, char **argv) {
	system("sudo ~/zookeeper/bin/zkServer.sh start");
	system("sudo ~/zookeeper/bin/zkCli.sh rmr /testing");
	::testing::InitGoogleTest(&argc, argv);
	auto ret = RUN_ALL_TESTS();
	system("sudo ~/zookeeper/bin/zkCli.sh rmr /testing");
	system("sudo ~/zookeeper/bin/zkServer.sh stop");
	return ret;
}

