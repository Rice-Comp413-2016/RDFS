#ifndef RDFS_ZKNNCLIENT_CC
#define RDFS_ZKNNCLIENT_CC

#include "../include/zk_nn_client.h"
#include "zkwrapper.h"
#include <iostream>

#include "hdfs.pb.h"
#include "ClientNamenodeProtocol.pb.h"
#include <google/protobuf/message.h>

namespace zkclient{

	using namespace hadoop::hdfs;

	ZkNnClient::ZkNnClient(std::string zkIpAndAddress) : ZkClientCommon(zkIpAndAddress) {

	}

	/*
	 * A simple print function that will be triggered when 
	 * namenode loses a heartbeat
	 */
	void notify_delete() {
		printf("No heartbeat, no childs to retrieve\n");
	}

	/*
	 * Watcher for health child node (/health/datanode_)
	 */
	void watcher_health_child(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx) {
		std::cout << "[health child] Watcher triggered on path '" << path << "'" << std::endl;
		char health[] = "/health/datanode_";
		printf("[health child] Receive a heartbeat. A child has been added under path %s\n", path);

		struct String_vector stvector;
		struct String_vector *vector = &stvector;
		int rc = zoo_wget_children(zzh, path, watcher_health_child, nullptr, vector);
		int i = 0;
		if (vector->count == 0){
			notify_delete();
			//printf("no childs to retrieve\n");
		}
		while (i < vector->count) {
			printf("Children %s\n", vector->data[i++]);
		}
		if (vector->count) {
			deallocate_String_vector(vector);
		}
	}

	/*
	* Watcher for /health root node
	*/
	void watcher_health(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx) {

		struct String_vector stvector;
		struct String_vector *vector = &stvector;
		/* reinstall watcher */
		int rc = zoo_wget_children(zzh, path, watcher_health, nullptr, vector);
		std::cout << "[rc] health:" << rc << std::endl;
		int i;
		std::vector <std::string> children;
		for (i = 0; i < stvector.count; i++) {
			children.push_back(stvector.data[i]);
		}

		if (children.size() == 0){
			printf("no childs to retrieve\n");
		}

		for (int i = 0; i < children.size(); i++) {
			std::cout << "[In watcher_health] Attaching child to " << children[i] << std::endl;
			int rc = zoo_wget_children(zzh, ("/health/" + children[i]).c_str(), watcher_health_child, nullptr, vector);
			int k=0;
			while (k < vector->count) {
				printf("Children of %s:  %s\n", children[i].c_str(),  vector->data[k++]);
			}
		}

	}

	void ZkNnClient::register_watches() {

		/* Place a watch on the health subtree */
		std::vector <std::string> children = zk->wget_children("/health", watcher_health, nullptr);
		for (int i = 0; i < children.size(); i++) {
			std::cout << "[In register_watches] Attaching child to " << children[i] << ", " << std::endl;
			std::vector <std::string> ephem = zk->wget_children("/health/" + children[i], watcher_health_child, nullptr);
			/*
			   if (ephem.size() > 0) {
			   std::cout << "Found ephem " << ephem[0] << std::endl;
			   } else {
			   std::cout << "No ephem found for " << children[i] << std::endl;
			   }
			 */
		}
	}

	bool ZkNnClient::file_exists(const std::string& path) {
		return zk->exists(ZookeeperPath(path), 0) == 0;
	}

	void ZkNnClient::get_info(GetFileInfoRequestProto& req, GetFileInfoResponseProto& res) {
		const std::string& path = req.src();
		if (file_exists(path)) {
			// TODO: use real data.
			HdfsFileStatusProto* status = res.mutable_fs();
			FsPermissionProto* permission = status->mutable_permission();
			// Shorcut to set permission to 777.
			permission->set_perm(~0);
			// Set it to be a file with length 1, "foo" owner and group, 0
			// modification/access time, "0" path inode.
			status->set_filetype(HdfsFileStatusProto::IS_FILE);
			status->set_path(path);
			status->set_length(1);
			status->set_owner("foo");
			status->set_group("foo");
			status->set_modification_time(0);
			status->set_access_time(0);
			// Other fields are optional, skip for now.
		}
	}

	void ZkNnClient::create_file(CreateRequestProto& request, CreateResponseProto& response) {
		const std::string& path = request.src();
		if (!file_exists(path)) {
			std::vector<std::uint8_t> vec;
			zk->create(ZookeeperPath(path), vec);
			HdfsFileStatusProto* status = response.mutable_fs();
			FsPermissionProto* permission = status->mutable_permission();
			// Shorcut to set permission to 777.
			permission->set_perm(~0);
			// Set it to be a file with length 1, "foo" owner and group, 0
			// modification/access time, "0" path inode.
			status->set_filetype(HdfsFileStatusProto::IS_FILE);
			status->set_path(path);
			status->set_length(1);
			status->set_owner("foo");
			status->set_group("foo");
			status->set_modification_time(0);
			status->set_access_time(0);
			// Other fields are optional, skip for now.
		}
	}

	void ZkNnClient::get_block_locations(GetBlockLocationsRequestProto& req, GetBlockLocationsResponseProto& res) {
		const std::string& src = req.src();
		google::protobuf::uint64 offset = req.offset();
		google::protobuf::uint64 length = req.offset();
		LocatedBlocksProto* blocks = res.mutable_locations();
		// TODO: get the actual data from zookeeper.
		blocks->set_filelength(1);
		blocks->set_underconstruction(false);
		blocks->set_islastblockcomplete(true);
		for (int i = 0; i < 1; i++) {
			LocatedBlockProto* block = blocks->add_blocks();
			block->set_offset(0);
			block->set_corrupt(false);
			// Construct extended block proto.
			ExtendedBlockProto* eb = block->mutable_b();
			eb->set_poolid("0");
			eb->set_blockid(0);
			eb->set_generationstamp(1);
			// Construct security token.
			hadoop::common::TokenProto* token = block->mutable_blocktoken();
			// TODO what do these mean
			token->set_identifier("open");
			token->set_password("sesame");
			token->set_kind("foo");
			token->set_service("bar");
			// Construct data node info objects.
			DatanodeInfoProto* dn_info = block->add_locs();
			DatanodeIDProto* id = dn_info->mutable_id();
			id->set_ipaddr("localhost");
			id->set_hostname("localhost");
			id->set_datanodeuuid("1234");
			// TODO: fill in from config
			id->set_xferport(50010);
			id->set_infoport(50020);
			id->set_ipcport(50030);
		}
	}


	std::string ZkNnClient::ZookeeperPath(const std::string &hadoopPath){
		std::string zkpath = "/fileSystem";
		if (hadoopPath.at(0) != '/'){
			zkpath += "/";
		}
		zkpath += hadoopPath;
		if (zkpath.at(zkpath.length() - 1) == '/'){
			zkpath.at(zkpath.length() - 1) = '\0';
		}
		return zkpath;
	}

}

#endif