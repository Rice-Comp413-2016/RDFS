#ifndef RDFS_ZKNNCLIENT_CC
#define RDFS_ZKNNCLIENT_CC

#include "../include/zk_nn_client.h"
#include "zkwrapper.h"
#include <iostream>
#include <sstream>
#include <ctime>
#include <chrono>
#include <sys/time.h>
#include "zk_lock.h"

#include "hdfs.pb.h"
#include "ClientNamenodeProtocol.pb.h"
#include <google/protobuf/message.h>
#include <ConfigReader.h>
#include <easylogging++.h>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/algorithm/string.hpp>
#include "zk_dn_client.h"

namespace zkclient{

	using namespace hadoop::hdfs;

	const std::string ZkNnClient::CLASS_NAME = ": **ZkNnClient** : ";

	ZkNnClient::ZkNnClient(std::string zkIpAndAddress) : ZkClientCommon(zkIpAndAddress) {
		mkdir_helper( "/", false);
	}

	ZkNnClient::ZkNnClient(std::shared_ptr <ZKWrapper> zk_in) : ZkClientCommon(zk_in) {
		mkdir_helper( "/", false);
	}

	/*
	 * A simple print function that will be triggered when 
	 * namenode loses a heartbeat
	 */
	void notify_delete() {
		printf("No heartbeat, no childs to retrieve\n");
	}

	void ZkNnClient::register_watches() {

		int error_code;
		// TODO: Do we have to free the returned children?
		std::vector <std::string> children = std::vector <std::string>();

		/* Place a watch on the health subtree */


		if (!(zk->wget_children(HEALTH, children, zk->watcher_health_factory(ZkClientCommon::HEALTH_BACKSLASH), nullptr, error_code))) {
				// TODO: Handle error
				LOG(ERROR) << CLASS_NAME << "[In register_watchers], wget failed " << error_code;
			}

		for (int i = 0; i < children.size(); i++) {
			LOG(INFO) << CLASS_NAME << "[In register_watches] Attaching child to " << children[i] << ", ";
			std::vector <std::string> ephem = std::vector <std::string>();
			if(!(zk->wget_children(HEALTH_BACKSLASH + children[i], ephem, zk->watcher_health_child, nullptr, error_code))) {
				// TODO: Handle error
				LOG(ERROR) << CLASS_NAME << "[In register_watchers], wget failed " << error_code;
			}
			/*
			   if (ephem.size() > 0) {
			   LOG(INFO) << CLASS_NAME << "Found ephem " << ephem[0];
			   } else {
			   LOG(INFO) << CLASS_NAME << "No ephem found for " << children[i];
			   }
			 */
		}
	}

	bool ZkNnClient::file_exists(const std::string& path) {
		bool exists;
		int error_code;
		if (zk->exists(ZookeeperPath(path), exists, error_code)) {
			return exists;
		} else {
			// TODO: Handle error
		}
	}



	// --------------------------- PROTOCOL CALLS ---------------------------------------

	void ZkNnClient::read_file_znode(FileZNode& znode_data, const std::string& path) {
		int error_code;
		std::vector<std::uint8_t> data(sizeof(znode_data));
		if (!zk->get(ZookeeperPath(path), data, error_code)) {
			LOG(INFO) << "helllo its bad news " << path;
			// TODO handle error
		}
		std::uint8_t *buffer = &data[0];
		memcpy(&znode_data, buffer, sizeof(znode_data));
	}

	void ZkNnClient::file_znode_struct_to_vec(FileZNode* znode_data, std::vector<std::uint8_t>& data) {
		memcpy(&data[0], znode_data, sizeof(*znode_data));
	}

	void ZkNnClient::add_block(AddBlockRequestProto& req, AddBlockResponseProto& res) {

		// Build a new block for the response
		auto block = res.mutable_block();

		// TODO: Make sure we are appending / not appending ZKPath at every step....
		const std::string file_path = req.src();

		LOG(INFO) << CLASS_NAME << "Attempting to add block to existing file " << file_path;

		FileZNode znode_data;
		if (!file_exists(file_path)) {
			LOG(ERROR) << CLASS_NAME << "Requested file " << file_path << " does not exist";
			return;
		}
		read_file_znode(znode_data, file_path); // TODO: What if the file does not exist
		if (znode_data.filetype != IS_FILE) { // Assert that the znode we want to modify is a file
			LOG(ERROR) << CLASS_NAME << "Requested file " << file_path << " is not a file";
			return;
		}

		uint32_t replication_factor = znode_data.replication;
		uint64_t block_size = znode_data.blocksize;
		assert(block_size > 0);

		u_int64_t block_id;
		auto data_nodes = std::vector<std::string>();

		add_block(file_path, block_id, data_nodes, replication_factor);

		block->set_offset(0); // TODO: Set this
		block->set_corrupt(false);

		ExtendedBlockProto* eb = block->mutable_b();
		eb->set_poolid("0");
		eb->set_blockid(block_id);
		eb->set_generationstamp(1);
		eb->set_numbytes(block_size);

		for (auto data_node :data_nodes) {

			int error_code;

			std::vector<std::string> split_address;
			boost::split(split_address, data_node, boost::is_any_of(":"));
			assert(split_address.size() == 2);

			auto data = std::vector<std::uint8_t>();
			if (zk->get(HEALTH_BACKSLASH + data_node + STATS, data, error_code)) {
				LOG(ERROR) << CLASS_NAME << "Getting data node stats failed with " << error_code;
			}

			zkclient::DataNodePayload * payload = (zkclient::DataNodePayload *) (&data[0]);

			DatanodeInfoProto* dn_info = block->add_locs();
			DatanodeIDProto* id = dn_info->mutable_id();
			id->set_ipaddr(split_address[0]);
			id->set_hostname("localhost"); // TODO: Fill out with the proper value
			id->set_datanodeuuid("1234");
			id->set_xferport(payload->xferPort);
			id->set_infoport(50020);
			id->set_ipcport(payload->ipcPort);
		}

		// Construct security token.
		hadoop::common::TokenProto* token = block->mutable_blocktoken();
		// TODO what do these mean
		token->set_identifier("open");
		token->set_password("sesame");
		token->set_kind("foo");
		token->set_service("bar");
	}

	void ZkNnClient::get_info(GetFileInfoRequestProto& req, GetFileInfoResponseProto& res) {
		const std::string& path = req.src();
		
		if (file_exists(path)) {
			LOG(INFO) << CLASS_NAME << "File exists";
			// read the node into the file node struct
			FileZNode znode_data;
			read_file_znode(znode_data, path);		
	
			// set the file status in the get file info response res
			HdfsFileStatusProto* status = res.mutable_fs();

			set_file_info(status, path, znode_data);	
			LOG(INFO) << CLASS_NAME << "Got info for file ";
			return;
		}
		LOG(INFO) << CLASS_NAME << "No file to get info for";
	}

	/**
	 * Create a node in zookeeper corresponding to a file 
	 */
	int ZkNnClient::create_file_znode(const std::string& path, FileZNode* znode_data) {
		int error_code;
		if (!file_exists(path)) {	
			LOG(INFO)<< "Creating file znode at " << path; 
			{
				LOG(INFO) << CLASS_NAME << znode_data->replication;
				LOG(INFO) << CLASS_NAME << znode_data->owner;
				LOG(INFO) << CLASS_NAME << "size of znode is " << sizeof(*znode_data);
			}
			// serialize struct to byte vector 
			std::vector<std::uint8_t> data(sizeof(*znode_data));
			file_znode_struct_to_vec(znode_data, data);	
			// crate the node in zookeeper 
			if (!zk->create(ZookeeperPath(path), data, error_code)) {
				LOG(ERROR) << CLASS_NAME << "Create failed" << error_code;
				return 0;
				// TODO : handle error
			}
			return 1; 
		}
		return 0;
	}

	void ZkNnClient::delete_node_wrapper(std::string& path, DeleteResponseProto& response) {
		int error_code;
		if (!zk->delete_node(ZookeeperPath(path), error_code)) {
			response.set_result(false);
			LOG(ERROR) << CLASS_NAME << "Error deleting node at " << path << " because of error = " << error_code;
			return;
		}
		LOG(INFO) << CLASS_NAME << "Successfully deletes znode";
	}


	/**
	 * Go down directories recursively. If a child is a file, then put its deletion on a queue.
	 * Files delete themselves, but directories are deleted by their parent (so root can't be deleted) 
	 */	
	void ZkNnClient::destroy(DeleteRequestProto& request, DeleteResponseProto& response) {

		int error_code;
		const std::string& path = request.src();
		bool recursive = request.recursive();
		response.set_result(true);
		if (!file_exists(path)) {
			return response.set_result(false);
		}
		FileZNode znode_data;
		read_file_znode(znode_data, path);

		// we have a directory
		if (recursive) {
			std::vector<std::string> children;
			if (!zk->get_children(ZookeeperPath(path), children, error_code)) {
				LOG(INFO) << CLASS_NAME << "Could not get children for " << path << " because of error = " << error_code;
				response.set_result(false);
				// zlock.unlock();
				return;
			}
			// delete the kids
			for (auto src : children) {
				DeleteRequestProto request_child;
				DeleteResponseProto response_child;
				request_child.set_src(path + "/" + src);
				request_child.set_recursive(true);
				destroy(request_child, response_child);
				if (response_child.result() == false) { // propogate failures updwards
					response.set_result(false);
				}
			}
		}
		else if (znode_data.filetype == IS_DIR || znode_data.filetype == IS_DIR1) {
			//make sure that the directory is empty if we are not recursively deleting it
			std::vector<std::string> children;
			if (!zk->get_children(ZookeeperPath(path), children, error_code) || children.size() > 0) {
				LOG(INFO) << CLASS_NAME << "Could not get children for " << path << " because of error = " << error_code
					<< " or the directory has children and the delete wasnt recursive";
				response.set_result(false);
				// zlock.unlock();
				return;
			}
		}
		std::string copy = path;
		// delete then dude then
		delete_node_wrapper(copy, response);
		if (znode_data.filetype == IS_FILE) {
			// TODO delete his blocks
		}
	}

	/**
	 * Create a file in zookeeper 
	 */ 
	int ZkNnClient::create_file(CreateRequestProto& request, CreateResponseProto& response) {
		LOG(INFO) << CLASS_NAME << "Gonna try and create a file on zookeeper";
		const std::string& path = request.src();
		const std::string& owner = request.clientname();
		bool create_parent = request.createparent();
		std::uint64_t blocksize = request.blocksize();
		std::uint32_t replication = request.replication();
		std::uint32_t createflag = request.createflag();

		if (file_exists(path)) {
			// TODO solve this issue of overwriting files
			LOG(ERROR) << CLASS_NAME << "File already exists";
			return 0;
		}

		// If we need to create directories, do so 
		if (create_parent) {
			std::string directory_paths = ""; 
			auto split_path = split(path, '/');
			LOG(INFO) << CLASS_NAME << split_path.size();
			for (int i = 0; i < split_path.size() - 1; i++) {
				directory_paths += split_path[i];
			}
			// try and make all the parents
			if (!mkdir_helper(directory_paths, true))
				return 0; 
		}

		// Now create the actual file which will hold blocks 	
		FileZNode znode_data;
		znode_data.length = 0;
		znode_data.under_construction = UNDER_CONSTRUCTION;
		// http://stackoverflow.com/questions/19555121/how-to-get-current-timestamp-in-milliseconds-since-1970-just-the-way-java-gets
		struct timeval tp;
		gettimeofday(&tp, NULL);
		uint64_t mslong = (uint64_t) tp.tv_sec * 1000L + tp.tv_usec / 1000; //get current timestamp in milliseconds
		znode_data.access_time = mslong;
		znode_data.modification_time = mslong;
		strcpy(znode_data.owner, owner.c_str());
		strcpy(znode_data.group, owner.c_str());
		znode_data.replication = replication;
		znode_data.blocksize = blocksize;
		znode_data.filetype = 2;

		// if we failed, then do not set any status
		if (!create_file_znode(path, &znode_data))
			return 0;

		HdfsFileStatusProto* status = response.mutable_fs();
		set_file_info(status, path, znode_data);

		return 1;
	}

	void ZkNnClient::complete(CompleteRequestProto& req, CompleteResponseProto& res) {

		// TODO: Completion makes a few guarantees that we should handle

		int error_code;
		// change the under construction bit
		const std::string& src = req.src();
		FileZNode znode_data;
		read_file_znode(znode_data, src);
		znode_data.under_construction = FILE_COMPLETE;
		std::vector<std::uint8_t> data(sizeof(znode_data));
		file_znode_struct_to_vec(&znode_data, data);
		if (!zk->set(ZookeeperPath(src), data, error_code)) {
			// TODO : handle erro
		}
		res.set_result(true);		
	}

	/**
	 * Rename a file in the zookeeper filesystem
	 */
	void ZkNnClient::rename(RenameRequestProto& req, RenameResponseProto& res) {
		if(!rename_file(req.src(), req.dst())) {
			res.set_result(false);
		}
		res.set_result(true);
	}

	// ------- make a directory

	/**
	 * Set the default information for a directory znode 
	 */ 
	void ZkNnClient::set_mkdir_znode(FileZNode* znode_data) {
		znode_data->length = 0;
		struct timeval tp;
		gettimeofday(&tp, NULL);
		// http://stackoverflow.com/questions/19555121/how-to-get-current-timestamp-in-milliseconds-since-1970-just-the-way-java-gets
		uint64_t mslong = (uint64_t) tp.tv_sec * 1000L + tp.tv_usec / 1000; //get current timestamp in milliseconds
		znode_data->access_time = mslong; // TODO what are these
		znode_data->modification_time = mslong;
		znode_data->blocksize = 0;
		znode_data->replication = 0;
		znode_data->filetype = IS_DIR;
	}

	/**
	 * Make a directory in zookeeper
	 */ 
	void ZkNnClient::mkdir(MkdirsRequestProto& request, MkdirsResponseProto& response) {	
		const std::string& path = request.src();
		bool create_parent = request.createparent();
		if (!mkdir_helper(path, create_parent)) {
			response.set_result(false);
		}
		response.set_result(true); 
	}

	/**
	 * Helper for creating a direcotyr znode. Iterates over the parents and crates them
	 * if necessary. 
	 */ 
	bool ZkNnClient::mkdir_helper(const std::string& path, bool create_parent) {
		if (create_parent) {
			auto split_path = split(path, '/');
			bool not_exist = false;
			std::string unroll;
			std::string p_path = "";
			for (int i = 0; i < split_path.size(); i++) {
				p_path += split_path[i] + "/";
				if (!file_exists(p_path)) {
					// keep track of the path where we start creating directories
					if (not_exist == false) {
						unroll = p_path; 
					}
					not_exist = true;
					FileZNode znode_data;
					set_mkdir_znode(&znode_data);	
					int error;
					if ((error = create_file_znode(path, &znode_data))) {
						// TODO unroll the created directories						
						return false; 
					}
				}
			}
		} 
		else {
			FileZNode znode_data;
			set_mkdir_znode(&znode_data);
			return create_file_znode(path, &znode_data);	
		}
		return true; 
	}
	
	void ZkNnClient::get_block_locations(GetBlockLocationsRequestProto& req, GetBlockLocationsResponseProto& res) {

		int error_code;

		const std::string &src = req.src();
		const std::string zk_path = ZookeeperPath(src);

		google::protobuf::uint64 offset = req.offset();
		google::protobuf::uint64 length = req.length();

		LocatedBlocksProto* blocks = res.mutable_locations();

		FileZNode znode_data;
		read_file_znode(znode_data, src);

		blocks->set_underconstruction(false);
		blocks->set_islastblockcomplete(true);
		blocks->set_filelength(znode_data.length);

		uint64_t block_size = znode_data.blocksize;

		LOG(INFO) << CLASS_NAME << "Block size of " << zk_path << " is " << block_size;

		auto sorted_blocks = std::vector<std::string>();

		// TODO: Make more efficient
		if(!zk->get_children(zk_path, sorted_blocks, error_code)) {
			LOG(ERROR) << CLASS_NAME << "Failed getting children of " << zk_path << " with error: " << error_code;
		}
		uint64_t size = 0;
		for (auto sorted_block : sorted_blocks) {
			LOG(INFO) << CLASS_NAME << "Considering block " << sorted_block;
			if (size > offset + length) {
				// at this point the start of the block is at a higher offset than the segment we want
				LOG(INFO) << CLASS_NAME << "Breaking at block " << sorted_block;
				break;
			}
			if (size + block_size >= offset) {
				auto data = std::vector<uint8_t>();
				if (!zk->get(zk_path + "/" + sorted_block, data, error_code)) {
					LOG(ERROR) << CLASS_NAME << "Failed to get " << zk_path << "/" << sorted_block << " info: " << error_code;
					return; // TODO: Signal error
				}
				uint64_t block_id = *(uint64_t *)(&data[0]);
				LOG(INFO) << CLASS_NAME << "Found block " << block_id << " for " << zk_path;

				// TODO: This block of code should be moved to a function, repeated with add_block
				LocatedBlockProto* located_block = blocks->add_blocks();
				located_block->set_corrupt(0);
				located_block->set_offset(size); // TODO: This offset may be incorrect

				hadoop::common::TokenProto* token = located_block->mutable_blocktoken();
				// TODO what do these mean
				token->set_identifier("open");
				token->set_password("sesame");
				token->set_kind("foo");
				token->set_service("bar");

				ExtendedBlockProto* block_proto = located_block->mutable_b();

				block_proto->set_poolid("0");
				block_proto->set_blockid(block_id);
				block_proto->set_generationstamp(1); // TODO: Do we have to modify this?
				block_proto->set_numbytes(block_size);

				auto data_nodes = std::vector<std::string>();

				LOG(INFO) << CLASS_NAME << "Getting datanode locations for block: " << "/block_locations/" + std::to_string(block_id);

				if (!zk->get_children("/block_locations/" + std::to_string(block_id), data_nodes, error_code)) {
					LOG(ERROR) << CLASS_NAME << "Failed getting datanode locations for block: " << "/block_locations/" + std::to_string(block_id) << " with error: " << error_code;
				}

				// TODO: fix me this is wrong.
				// data_nodes.push_back("127.0.0.1:50020");
				LOG(INFO) << CLASS_NAME << "Found block locations " << data_nodes.size();
				for (auto data_node :data_nodes) {

					LOG(INFO) << CLASS_NAME << "Reading data node " << data_node;
					int error_code;

					std::vector<std::string> split_address;
					boost::split(split_address, data_node, boost::is_any_of(":"));
					assert(split_address.size() == 2);

					auto data = std::vector<std::uint8_t>();
					if (zk->get(HEALTH_BACKSLASH + data_node + STATS, data, error_code)) {
						LOG(ERROR) << CLASS_NAME << "Getting data node stats failed with " << error_code;
					}

					zkclient::DataNodePayload * payload = (zkclient::DataNodePayload *) (&data[0]);

					DatanodeInfoProto* dn_info = located_block->add_locs();
					DatanodeIDProto* id = dn_info->mutable_id();
					id->set_ipaddr(split_address[0]);
					id->set_hostname("localhost"); // TODO: Fill out with the proper value
					id->set_datanodeuuid("1234");
					id->set_xferport(payload->xferPort);
					id->set_infoport(50020);
					id->set_ipcport(payload->ipcPort);
				}
				// Then store this block's information
			}
			size += block_size;
		}
	}


	// ---------------------------------------- HELPERS ----------------------------------------

	std::string ZkNnClient::ZookeeperPath(const std::string &hadoopPath){
		std::string zkpath = NAMESPACE_PATH;
		if (hadoopPath.size() == 0) {
			LOG(INFO) << "real real bad news";
		}
		if (hadoopPath.at(0) != '/'){
			zkpath += "/";
		}
		zkpath += hadoopPath;
		if (zkpath.at(zkpath.length() - 1) == '/'){
			zkpath.at(zkpath.length() - 1) = '\0';
		}
		return zkpath;
	}

	void ZkNnClient::set_file_info(HdfsFileStatusProto* status, const std::string& path, FileZNode& znode_data) {
		HdfsFileStatusProto_FileType filetype;
		// get the filetype, since we do not want to serialize an enum
		switch(znode_data.filetype) {
			case(0):
				filetype = HdfsFileStatusProto::IS_DIR;
				break;
			case(1):
				filetype = HdfsFileStatusProto::IS_DIR;
				break;
			case(2):
				filetype = HdfsFileStatusProto::IS_FILE;
				break;
			default:
				break;

		}
		
		FsPermissionProto* permission = status->mutable_permission();
		// Shorcut to set permission to 777.
		permission->set_perm(~0);
		// Set it to be a file with length 1, "foo" owner and group, 0
		// modification/access time, "0" path inode.
		status->set_filetype(filetype);
		status->set_path(path);
		status->set_length(znode_data.length);
		status->set_blocksize(znode_data.blocksize);
		
		std::string owner(znode_data.owner);
		std::string group(znode_data.group);
		status->set_owner(owner);
		status->set_group(group);
		
		status->set_modification_time(znode_data.modification_time);
		status->set_access_time(znode_data.access_time);
		LOG(INFO) << CLASS_NAME << "Successfully set the file info ";
	}

	/**
	 * Split the str by the delimiter and return a vector of the split words
	*/
		std::vector<std::string> ZkNnClient::split(const std::string &str, char delim) {
		std::stringstream ss;
		ss.str(str);
		std::string item;
		std::vector<std::string> elems;
		while (getline(ss, item, delim)) {
			elems.push_back(item);
		}
		return elems;
	}

	bool ZkNnClient::add_block(const std::string& file_path, u_int64_t& block_id, std::vector<std::string> & data_nodes, uint32_t replicationFactor) {

		if (!file_exists(file_path)) {
			LOG(ERROR) << CLASS_NAME << "Cannot add block to non-existent file" << file_path;
			return false;
		}

		FileZNode znode_data;
		read_file_znode(znode_data, file_path);
		if (znode_data.under_construction) { // TODO: This is a faulty check
			LOG(WARNING) << "Last block for " << file_path << " still under construction";
		}
		// TODO: Check the replication factor

		std::string block_id_str;

		generate_block_UUID(block_id);
		block_id_str = std::to_string(block_id);
		LOG(INFO) << CLASS_NAME << "Generated block id " << block_id_str;

		if (!find_datanode_for_block(data_nodes, block_id, replicationFactor, true)) {
			return false;
		}

		// Add another block size to the file length and update zookeeper.
		znode_data.length += znode_data.blocksize;
		int error_code;
		std::vector<std::uint8_t> new_data(sizeof(znode_data));
		file_znode_struct_to_vec(&znode_data, new_data);
		if (!zk->set(ZookeeperPath(file_path), new_data, error_code)) {
			LOG(ERROR) << "Set failed" << error_code;
			return 0;
			// TODO : handle error
		}

        // Generate the massive multi-op for creating the block

		std::vector<std::uint8_t> data;
		data.resize(sizeof(u_int64_t));
		memcpy(&data[0], &block_id, sizeof(u_int64_t));

		LOG(INFO) << CLASS_NAME << "Generating block for " << ZookeeperPath(file_path);

		// ZooKeeper multi-op to
		auto seq_file_block_op = zk->build_create_op(ZookeeperPath(file_path + "/block_"), data, ZOO_SEQUENCE);
		auto ack_op = zk->build_create_op("/work_queues/wait_for_acks/" + block_id_str, ZKWrapper::EMPTY_VECTOR);
		auto block_location_op = zk->build_create_op("/block_locations/" + block_id_str, ZKWrapper::EMPTY_VECTOR);

		std::vector<std::shared_ptr<ZooOp>> ops = {seq_file_block_op, ack_op, block_location_op};

		auto results = std::vector <zoo_op_result>();
		int err;
		// TODO: Perhaps we have to perform a more fine grained analysis of the results
		if (!zk->execute_multi(ops, results, err)) {
			LOG(ERROR) << CLASS_NAME << "Failed to write the addBlock multiop, ZK state was not changed";
			ZKWrapper::print_error(err);
			return false;
		}
		return true;
	}

	bool ZkNnClient::generate_block_UUID(u_int64_t& blockId) {
		// TODO: As of now we will just generate an incremented long
		auto uuid = boost::uuids::random_generator()();
		memcpy((&blockId), &uuid, sizeof(u_int64_t) / sizeof(u_char));
		return true;
	}

	bool ZkNnClient::find_datanode_for_block(std::vector<std::string>& datanodes, const u_int64_t blockId, uint32_t replication_factor, bool newBlock) {
		// TODO: Actually perform this action
		// TODO: Perhaps we should keep a cached list of nodes

		std::vector<std::string> live_data_nodes = std::vector <std::string>();
		int error_code;

		if (zk->get_children(HEALTH, live_data_nodes, error_code)) {
			/* for each child, check if the ephemeral node exists */
			for(auto datanode : live_data_nodes) {
				bool isAlive;
				if (!zk->exists(HEALTH_BACKSLASH + datanode + HEALTH, isAlive, error_code)) {
					LOG(ERROR) << CLASS_NAME << "Failed to check if datanode: " + datanode << " is alive: " << error_code;
				}
				if (isAlive) {
					// TODO: use more advanced child selection heruistic
					if (newBlock) {
						datanodes.push_back(datanode);
					} else {
						// TODO: Check if the datanode currently contains a replica
					}
				}
				if (datanodes.size() == replication_factor) {
					LOG(INFO) << CLASS_NAME << "Found " << replication_factor << " datanodes";
					break;
				}
			}
		} else {
			LOG(ERROR) << CLASS_NAME << "Failed to get list of datanodes at " + HEALTH + " " << error_code;
			return false;
		}

		// TODO: Read strategy from config, but as of now select the first few blocks that are valid

		if (datanodes.size() > replication_factor) {
			LOG(ERROR) << CLASS_NAME << "Failed to find at least " << replication_factor << " datanodes at " + HEALTH;
			return false;
		}
		return true;
	}

	/**
	 * Updates /fileSystem in ZK to reflect a file rename
	 * @param src The path to the source file (not znode) within the filesystem
	 * @param dst The path to the renamed destination file (not znode) within the filesystem
	 * @return Boolean indicating success or failure of the rename
	 */
	bool ZkNnClient::rename_file(std::string src, std::string dst) {
		LOG(INFO) << "Renaming '"  << src << "' to '" << dst << "'";

		int error_code = 0;
		auto data = std::vector<std::uint8_t>();

		std::string src_znode = "/fileSystem" + src;
		std::string dst_znode = "/fileSystem" + dst;

		// TODO: if one of these fails, should we try to undo? Use a multiop here?

		// Get the payload from the old filesystem znode for the src
		zk->get(src_znode, data, error_code);
		if (error_code != ZOK) {
			LOG(ERROR) << "Failed to get data from '" << src_znode << "' when renaming.";
			return false;
		}

		// Create a new znode in the filesystem for the dst
		zk->create(dst_znode, data, error_code, false);
		if (error_code != ZOK) {
			LOG(ERROR) << "Failed to create new znode for '" << dst_znode << "' when renaming.";
			return false;
		}

		// Copy over the data from the children of the src_znode into new children of the dst_znode
		auto children = std::vector<std::string>();
		zk->get_children(src_znode, children, error_code);
		if (error_code != ZOK) {
			LOG(ERROR) << "Failed to get children of znode '" << src_znode << "' when renaming.";
			return false;
		}

		for (auto child : children) {
			// Get child's data
			auto child_data = std::vector<std::uint8_t>();
			zk->get(src_znode + "/" + child, child_data, error_code);
			if (error_code != ZOK) {
				LOG(ERROR) << "Failed to get data from '" << child << "' when renaming.";
				return false;
			}

			// Create new child of dst_znode with this data
			zk->create(dst_znode + "/" + child, child_data, error_code, false);
			if (error_code != ZOK) {
				LOG(ERROR) << "Failed to create dst_znode child for '" << dst_znode << "' when renaming.";
				return false;
			}

			// Delete src_znode's child
			zk->delete_node(src_znode + "/" + child, error_code);
			if (error_code != ZOK) {
				LOG(ERROR) << "Failed to delete src_znode child for '" << src_znode << "' when renaming.";
				return false;
			}

		}

		// Remove the old znode for the src
		zk->delete_node(src_znode, error_code);
		if (error_code != ZOK) {
			LOG(ERROR) << "Failed to delete old znode '" << src_znode << "' when renaming.";
			return false;
		}

		LOG(INFO) << "Successfully renamed '"  << src << "' to '" << dst << "'";
		return true;
	}
}

#endif
