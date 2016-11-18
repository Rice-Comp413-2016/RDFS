#include "zkwrapper.h"
#include "zk_nn_client.h"
#include "zk_dn_client.h"

#include <gtest/gtest.h>

#include <cstring>
#include <easylogging++.h>

INITIALIZE_EASYLOGGINGPP

namespace {

	class NamenodeTest : public ::testing::Test {

	protected:
		virtual void SetUp() {
			int error_code;
			auto zk_shared = std::make_shared<ZKWrapper>("localhost:2181", error_code, "/testing");
			assert(error_code == 0); // Z_OK
			client = new zkclient::ZkNnClient(zk_shared);
			zk = new ZKWrapper("localhost:2181", error_code, "/testing");
		}

		virtual void TearDown() {
			system("sudo ~/zookeeper/bin/zkCli.sh rmr /testing");
		}

		hadoop::hdfs::CreateRequestProto getCreateRequestProto(const std::string& path){
			hadoop::hdfs::CreateRequestProto create_req;
			create_req.set_src(path);
			create_req.set_clientname("asdf");
			create_req.set_createparent(false);
			create_req.set_blocksize(1);
			create_req.set_replication(1);
			create_req.set_createflag(0);
			return create_req;
		}

		// Objects declared here can be used by all tests in the test case for Foo.
		ZKWrapper *zk;
		zkclient::ZkNnClient *client;
	};


	TEST_F(NamenodeTest, checkNamespace){
		// nuffin
	}

	TEST_F(NamenodeTest, findDataNodes){

		int error;
		zk->create("/health/localhost:2181", ZKWrapper::EMPTY_VECTOR, error);
		zk->create("/health/localhost:2181/heartbeat", ZKWrapper::EMPTY_VECTOR, error);
		zk->create("/health/localhost:2182", ZKWrapper::EMPTY_VECTOR, error);
		zk->create("/health/localhost:2182/heartbeat", ZKWrapper::EMPTY_VECTOR, error);

		zkclient::DataNodePayload data_node_payload = zkclient::DataNodePayload();
		data_node_payload.ipcPort = 1;
		data_node_payload.xferPort = 1;
		data_node_payload.disk_bytes = 1;
		data_node_payload.free_bytes = 1024;
		data_node_payload.xmits = 5;

		std::vector<uint8_t> stats_vec;
		stats_vec.resize(sizeof(zkclient::DataNodePayload));
		memcpy(&stats_vec[0], &data_node_payload, sizeof(zkclient::DataNodePayload));
		zk->create("/health/localhost:2181/stats", stats_vec, error);

		data_node_payload.xmits = 3;
		memcpy(&stats_vec[0], &data_node_payload, sizeof(zkclient::DataNodePayload));
		zk->create("/health/localhost:2182/stats", stats_vec, error);

		auto datanodes = std::vector<std::string>();
		u_int64_t block_id;
		LOG(INFO) << "Finding dn's for block " << block_id;
		util::generate_uuid(block_id);
		int rep_factor = 1;
		client->find_datanode_for_block(datanodes, block_id, rep_factor, true);

		for (auto datanode : datanodes) {
			LOG(INFO) << "Returned datanode " << datanode;
		}
		ASSERT_EQ(rep_factor, datanodes.size());
	}

	TEST_F(NamenodeTest, findDataNodesWithReplicas){
		// Check if we can find datanodes, without overlapping with ones that already contain a replica
	}

	TEST_F(NamenodeTest, basicCheckAcks){
		// Check if check_acks works as intended
		int error;
		zk->delete_node("/work_queues/wait_for_acks/block_uuid_1/dn-id-3", error);
		zk->delete_node("/work_queues/wait_for_acks/block_uuid_1/dn-id-2", error);
		zk->delete_node("/work_queues/wait_for_acks/block_uuid_1/dn-id-1", error);
		zk->delete_node("/work_queues/wait_for_acks/block_uuid_1", error);

		uint32_t rep_factor = 3;
		std::vector<std::uint8_t> data;
		data.resize(sizeof(uint32_t));
		memcpy(&data[0], &rep_factor, sizeof(uint32_t));
		zk->create("/work_queues/wait_for_acks/block_uuid_1", data, error, false);
		ASSERT_EQ(0, error);

		zk->create("/work_queues/wait_for_acks/block_uuid_1/dn-id-1", ZKWrapper::EMPTY_VECTOR, error);
		ASSERT_EQ(0, error);

		// Only one DN acknowledged, but not timed out, so should succeed
		ASSERT_EQ(true, client->check_acks());

		zk->create("/work_queues/wait_for_acks/block_uuid_1/dn-id-2", ZKWrapper::EMPTY_VECTOR, error);
		ASSERT_EQ(0, error);
		// Only two DNs acknowledged, but not timed out, so should succeed
		ASSERT_EQ(true, client->check_acks());

		zk->create("/work_queues/wait_for_acks/block_uuid_1/dn-id-3", ZKWrapper::EMPTY_VECTOR, error);
		ASSERT_EQ(0, error);
		// All three DNs acknowledged, so should succeed
		ASSERT_EQ(true, client->check_acks());


		// Since enough DNs acknowledged, the block_uuid_1 should have been removed from wait_for_acks
		auto children = std::vector<std::string>();
		zk->get_children("/work_queues/wait_for_acks", children, error);
		ASSERT_EQ(0, children.size());
	}

	TEST_F(NamenodeTest, DeleteEmptyDirNonRecursive){
		int error;
		hadoop::hdfs::MkdirsRequestProto mkdir_req;
		hadoop::hdfs::MkdirsResponseProto mkdir_resp;
		mkdir_req.set_src("dir1");
		mkdir_req.set_createparent(false);
		client->mkdir(mkdir_req, mkdir_resp);
		ASSERT_TRUE(mkdir_resp.result());

		hadoop::hdfs::DeleteRequestProto del_req;
		hadoop::hdfs::DeleteResponseProto del_resp;
		del_req.set_src("dir1");
		del_req.set_recursive(false);
		client->destroy(del_req, del_resp);
		ASSERT_FALSE(del_resp.result());
		bool exists;
		ASSERT_TRUE(zk->exists("/fileSystem/dir1", exists, error));
		ASSERT_TRUE(exists);
	}

	TEST_F(NamenodeTest, DeleteEmptyDirRecursive){
		int error;
		hadoop::hdfs::MkdirsRequestProto mkdir_req;
		hadoop::hdfs::MkdirsResponseProto mkdir_resp;
		mkdir_req.set_src("dir2");
		mkdir_req.set_createparent(false);
		client->mkdir(mkdir_req, mkdir_resp);
		ASSERT_TRUE(mkdir_resp.result());

		hadoop::hdfs::DeleteRequestProto del_req;
		hadoop::hdfs::DeleteResponseProto del_resp;
		del_req.set_src("dir2");
		del_req.set_recursive(true);
		client->destroy(del_req, del_resp);
		ASSERT_TRUE(del_resp.result());
		bool exists;
		ASSERT_TRUE(zk->exists("/fileSystem/dir2", exists, error));
		ASSERT_FALSE(exists);
	}

	TEST_F(NamenodeTest, DeleteUnclosedFile){
		int error;
		hadoop::hdfs::CreateRequestProto create_req = getCreateRequestProto("file1");
		hadoop::hdfs::CreateResponseProto create_resp;
		ASSERT_EQ(1, client->create_file(create_req, create_resp));

		hadoop::hdfs::DeleteRequestProto del_req;
		hadoop::hdfs::DeleteResponseProto del_resp;
		del_req.set_src("file1");
		del_req.set_recursive(false);
		client->destroy(del_req, del_resp);
		ASSERT_FALSE(del_resp.result());
		bool exists;
		ASSERT_TRUE(zk->exists("/fileSystem/file1", exists, error));
		ASSERT_TRUE(exists);
	}

	TEST_F(NamenodeTest, DeleteClosedFileWithBlock){
		int error;
		hadoop::hdfs::CreateRequestProto create_req = getCreateRequestProto("file2");
		hadoop::hdfs::CreateResponseProto create_resp;
		ASSERT_EQ(1, client->create_file(create_req, create_resp));
		std::uint64_t block_id = 1234;
		std::vector<std::uint8_t> block_vec(sizeof(std::uint64_t));
		memcpy(block_vec.data(), &block_id, sizeof(std::uint64_t));
		ASSERT_TRUE(zk->create("/fileSystem/file2/block-0000000000", block_vec, error));
		ASSERT_TRUE(zk->create("/block_locations/1234", ZKWrapper::EMPTY_VECTOR, error));

		// TODO: create real block_locations for this block once we start doing complete legitimately

		hadoop::hdfs::CompleteRequestProto complete_req;
		hadoop::hdfs::CompleteResponseProto complete_resp;
		complete_req.set_src("file2");
		client->complete(complete_req, complete_resp);
		ASSERT_TRUE(complete_resp.result());

		hadoop::hdfs::DeleteRequestProto del_req;
		hadoop::hdfs::DeleteResponseProto del_resp;
		del_req.set_src("file2");
		del_req.set_recursive(false);
		client->destroy(del_req, del_resp);
		ASSERT_TRUE(del_resp.result());
		bool exists;
		ASSERT_TRUE(zk->exists("/fileSystem/file2", exists, error));
		ASSERT_FALSE(exists);
	}

	TEST_F(NamenodeTest, previousBlockComplete){
		int error;
		u_int64_t block_id;
		util::generate_uuid(block_id);
		LOG(INFO) << "Previous block_id is " << block_id;
		ASSERT_EQ(false, client->previousBlockComplete(block_id));
		/* mock the directory */
		zk->create("/block_locations", ZKWrapper::EMPTY_VECTOR, error);
		zk->create("/block_locations/"+std::to_string(block_id), ZKWrapper::EMPTY_VECTOR, error);
		ASSERT_EQ(false, client->previousBlockComplete(block_id));
		/* mock the child directory */
		zk->create("/block_locations/"+std::to_string(block_id)+"/child1", ZKWrapper::EMPTY_VECTOR, error);
		ASSERT_EQ(true, client->previousBlockComplete(block_id));
	}

	TEST_F(NamenodeTest, testRenameFile){
		int error_code;
		zk->create("/fileSystem/old_name", zk->get_byte_vector("File data"), error_code, false);
		ASSERT_EQ(0, error_code);

		std::string new_path;
		zk->create_sequential("/fileSystem/old_name/block-", zk->get_byte_vector("Block uuid"), new_path, false, error_code);
		ASSERT_EQ(0, error_code);
		ASSERT_EQ("/fileSystem/old_name/block-0000000000", new_path);

		ASSERT_TRUE(client->rename_file("/old_name", "/new_name"));

		auto new_file_data = std::vector<std::uint8_t>();
		zk->get("/fileSystem/new_name", new_file_data, error_code);
		ASSERT_EQ(0, error_code);
		ASSERT_EQ("File data", std::string(new_file_data.begin(), new_file_data.end()));
		LOG(INFO) << "New: " << std::string(new_file_data.begin(), new_file_data.end());

		auto new_block_data = std::vector<std::uint8_t>();
		zk->get("/fileSystem/new_name/block-0000000000", new_block_data, error_code);
		ASSERT_EQ(0, error_code);
		ASSERT_EQ("Block uuid", std::string(new_block_data.begin(), new_block_data.end()));
		LOG(INFO) << "New: " << std::string(new_block_data.begin(), new_block_data.end());

		// Ensure that old_name was delete
		bool exist;
		zk->exists("/fileSystem/old_name", exist, error_code);
		ASSERT_EQ(false, exist);
	}

}

int main(int argc, char **argv) {
	// Start up zookeeper
	system("sudo ~/zookeeper/bin/zkServer.sh stop");
	system("sudo ~/zookeeper/bin/zkServer.sh start");

	// Initialize and run the tests
	::testing::InitGoogleTest(&argc, argv);
	int res = RUN_ALL_TESTS();
	// NOTE: You'll need to scroll up a bit to see the test results

	// Remove test files and shutdown zookeeper
	system("sudo ~/zookeeper/bin/zkServer.sh stop");
	return res;
}
