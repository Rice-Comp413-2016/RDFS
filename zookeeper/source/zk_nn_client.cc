#ifndef RDFS_ZKNNCLIENT_CC
#define RDFS_ZKNNCLIENT_CC

#include "zk_nn_client.h"
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
        int err;
        std::vector<std::string> children = std::vector<std::string>();
        if (!(zk->wget_children(HEALTH, children, watcher_health, this, err))) {
            // TODO: Handle error
            LOG(ERROR) << CLASS_NAME << "[In register_watchers], wget failed " << err;
        }

        for (int i = 0; i < children.size(); i++) {
            LOG(INFO) << CLASS_NAME << "[In register_watches] Attaching child to " << children[i] << ", ";
            std::vector<std::string> ephem = std::vector<std::string>();
            if (!(zk->wget_children(HEALTH_BACKSLASH + children[i], ephem, watcher_health_child, this, err))) {
                // TODO: Handle error
                LOG(ERROR) << CLASS_NAME << "[In register_watchers], wget failed " << err;
            }
        }
    }

    /**
     * Static
     */
    void ZkNnClient::watcher_health(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx) {
        LOG(INFO) << "Health watcher triggered on " << path;

        ZkNnClient *cli = (ZkNnClient *)watcherCtx;
        auto zk = cli->zk;

        int err;
        std::vector<std::string> children = std::vector<std::string>();
        if (!(zk->wget_children(HEALTH, children, ZkNnClient::watcher_health, watcherCtx, err))) {
            // TODO: Handle error
            LOG(ERROR) << CLASS_NAME << "[In register_watchers], wget failed " << err;
        }

        for (int i = 0; i < children.size(); i++) {
            LOG(INFO) << CLASS_NAME << "[In register_watches] Attaching child to " << children[i] << ", ";
            std::vector<std::string> ephem = std::vector<std::string>();
            if (!(zk->wget_children(HEALTH_BACKSLASH + children[i], ephem, ZkNnClient::watcher_health_child, watcherCtx, err))) {
                // TODO: Handle error
                LOG(ERROR) << CLASS_NAME << "[In register_watchers], wget failed " << err;
            }
        }
    }

    /**
     * Static
     */
    void ZkNnClient::watcher_health_child(zhandle_t *zzh, int type, int state, const char *raw_path, void *watcherCtx) {

        LOG(INFO) << "Health child water triggered on " << raw_path;

        ZkNnClient *cli = (ZkNnClient *)watcherCtx;
        auto zk = cli->zk;
        auto path = zk->removeZKRoot(std::string(raw_path));

        std::vector<std::string> children;

        LOG(INFO) << CLASS_NAME << "[health child] Watcher triggered on path '" << path;
        int err, rc;
        bool ret;
        std::vector<std::string> split_path;
        boost::split(split_path, path, boost::is_any_of("/"));
        auto dn_id = split_path[split_path.size() - 1];
        const std::string &repl_q_path = zkclient::ZkClientCommon::REPLICATE_QUEUES + dn_id;
        const std::string &delete_q_path = zkclient::ZkClientCommon::DELETE_QUEUES + dn_id;
        const std::string &block_path = util::concat_path(path, zkclient::ZkClientCommon::BLOCKS);
        std::vector<std::uint8_t> bytes;
        std::vector<std::string> to_replicate;
        /* Lock the znode */
        ZKLock race_lock(*zk.get(), std::string(path));
        if (race_lock.lock()) {
            // Lock failed somehow
            LOG(ERROR) << CLASS_NAME << " An error occurred while trying to lock " << path;
            return;
        }

        ret = zk->get_children(path, children, err);

        if (!ret) {
            LOG(ERROR) << CLASS_NAME << " Failed to get children";
            goto unlock;
        }

        /* Check for heartbeat */
        for (int i = 0; i < children.size(); i++) {
            if (children[i] == "heartbeat") {
                /* Heartbeat exists! Don't do anything */
                LOG(INFO) << " Heartbeat found while deleting";
                goto unlock;
            }
        }
        children.clear();

        /* Get all replication queue items for this datanode, save it */
        if (!zk->get_children(repl_q_path.c_str(), children, err)) {
            LOG(ERROR) << CLASS_NAME << " Failed to get dead datanode's replication queue";
            goto unlock;
        }
        for (auto child : children) {
            to_replicate.push_back(child);
        }
        children.clear();

        /* Delete all work queues for this datanode */
        if (!zk->recursive_delete(repl_q_path, err)){
            LOG(ERROR) << CLASS_NAME << " Failed to delete dead datanode's replication queue";
            goto unlock;
        }
        if (!zk->recursive_delete(delete_q_path, err)){
            LOG(ERROR) << CLASS_NAME << " Failed to delete dead datanode's delete queue";
            goto unlock;
        }

        /* Put every block from /blocks on replication queue as well as any saved items from replication queue */

        if (!zk->get_children(block_path.c_str(), children, err)) {
            LOG(ERROR) << CLASS_NAME << " Failed to get children for blocks";
            goto unlock;
        }

        /* Push blocks this datanode has onto replication to-queue list */
        for (auto child : children) {
            to_replicate.push_back(child);
        }
        children.clear();

        /* Delete this datanode. */
        if (!zk->recursive_delete(std::string(path), err)){
            LOG(ERROR) << "Failed to clean up dead datanode.";
        }

        /* Push all blocks needing to be replicated onto the queue */
        if (!cli->replicate_blocks(to_replicate, err)){
            LOG(ERROR) << "Failed to push all items on to replication queues!";
            goto unlock;
        }
        unlock:
        /* Unlock the znode */
        if (race_lock.unlock()) {
            LOG(ERROR) << CLASS_NAME << " An error occurred while trying to unlock " << path;
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

	bool ZkNnClient::get_block_size(const u_int64_t &block_id, uint64_t &blocksize) {
		int error_code;
		std::string block_path = BLOCK_LOCATIONS + std::to_string(block_id);

		BlockZNode block_data;
		std::vector<std::uint8_t> data(sizeof(block_data));
		if (!zk->get(block_path, data, error_code)) {
			LOG(ERROR) << "We could not read the block at " << block_path;
			return false;
		}
		std::uint8_t *buffer = &data[0];
		memcpy(&block_data, &data[0], sizeof(block_data));

		blocksize = block_data.block_size;
		LOG(INFO) << "Block size of: " << block_path << " is " << blocksize;
		return true;
	}

    // --------------------------- PROTOCOL CALLS ---------------------------------------

    void ZkNnClient::read_file_znode(FileZNode& znode_data, const std::string& path) {
        int error_code;
        std::vector<std::uint8_t> data(sizeof(znode_data));
        if (!zk->get(ZookeeperPath(path), data, error_code)) {
            LOG(ERROR) << "We could not read the file znode at " << path;
            return; // don't bother reading the data
        }
        std::uint8_t *buffer = &data[0];
        memcpy(&znode_data, buffer, sizeof(znode_data));
    }

    void ZkNnClient::file_znode_struct_to_vec(FileZNode* znode_data, std::vector<std::uint8_t>& data) {
        memcpy(&data[0], znode_data, sizeof(*znode_data));
    }


    bool ZkNnClient::previousBlockComplete(uint64_t prev_id) {
        if (prev_id == 0) { // first block, so just say hell yea
            return true;
        }
    	int error_code;
    	/* this value will eventually be read from config file */
    	int MIN_REPLICATION = 1;
    	std::vector<std::string> children;
    	std::string block_id_str = std::to_string(prev_id);
    	if (zk->get_children(BLOCK_LOCATIONS + block_id_str, children, error_code)) {
            if(children.size() >= MIN_REPLICATION) {
				return true;
			} else {
				LOG(INFO) << "Had to sync: previous block failed";
				// If we failed initially attempt to sync the changes, then check again
				zk->flush(zk->prepend_zk_root(BLOCK_LOCATIONS + block_id_str), true);
				if (zk->get_children(BLOCK_LOCATIONS + block_id_str, children, error_code)) {
					return children.size() >= MIN_REPLICATION;
				}
			}
    	}
    	return false;
    }

    bool ZkNnClient::add_block(AddBlockRequestProto& req, AddBlockResponseProto& res) {

    	// make sure previous addBlock operation has completed
    	auto prev_id = req.previous().blockid();
    	if (!previousBlockComplete(prev_id)){
    	    LOG(ERROR) << "Previous Add Block Operation has not finished";
    	    return false;	 
    	}

        // Build a new block for the response
        auto block = res.mutable_block();

        // TODO: Make sure we are appending / not appending ZKPath at every step....
        const std::string file_path = req.src();

        LOG(INFO) << CLASS_NAME << "Attempting to add block to existing file " << file_path;

        FileZNode znode_data;
        if (!file_exists(file_path)) {
            LOG(ERROR) << CLASS_NAME << "Requested file " << file_path << " does not exist";
            return false;
        }
        read_file_znode(znode_data, file_path);
        if (znode_data.filetype != IS_FILE) { // Assert that the znode we want to modify is a file
            LOG(ERROR) << CLASS_NAME << "Requested file " << file_path << " is not a file";
            return false;
        }

        uint32_t replication_factor = znode_data.replication;
        uint64_t block_size = znode_data.blocksize;
        assert(block_size > 0);

        std::uint64_t block_id;
        auto data_nodes = std::vector<std::string>();

        add_block(file_path, block_id, data_nodes, replication_factor);

        block->set_offset(0); // TODO: Set this
        block->set_corrupt(false);

        buildExtendedBlockProto(block->mutable_b(), block_id, block_size);

        for (auto data_node : data_nodes) {
            buildDatanodeInfoProto(block->add_locs(), data_node);
        }

        // Construct security token.
        buildTokenProto(block->mutable_blocktoken());
    	return true;
	}

    /**
     * Since the names were a bit strange and it was a pain to go back and figure out 
     * where these were again, I'm writing what the proto fields are here.
     *
     * message has:
     * required ExtendedBlockProto b = 1;
     * required string src = 2;
     * required string holder = 3;
     * optional uint64 fileId = 4 [default = 0];  // default to GRANDFATHER_INODE_ID 
     *
     * ExtendedBlockProto has:
     * required string poolId = 1;   // Block pool id - gloablly unique across clusters
     * required uint64 blockId = 2;  // the local id within a pool
     * required uint64 generationStamp = 3;
     * optional uint64 numBytes = 4 [default = 0];  // len does not belong in ebid 
    */
    bool ZkNnClient::abandon_block(AbandonBlockRequestProto& req, AbandonBlockResponseProto& res) {
        const std::string& file_path = req.src();
        const std::string& holder = req.holder(); // I believe this is the lease holder?
        LOG(INFO) << CLASS_NAME << "Attempting to abandon block";

        //uint64 generationStamp (as well as the optional uint64 numBytes)
        const ExtendedBlockProto& block = req.b();
        const std::string poolId = block.poolid();
        const uint64_t blockId = block.blockid();
        const uint64_t generation_stamp = block.generationstamp();
        LOG(INFO) << CLASS_NAME << "Requested to abandon block: " << blockId;
        LOG(INFO) << CLASS_NAME << "This block is in pool " << poolId;
        LOG(INFO) << CLASS_NAME << "Its generation stamp is " << generation_stamp;

        const std::string block_id_str = std::to_string(blockId);
        LOG(INFO) << CLASS_NAME << "...Also, converted blockid to a string: " << block_id_str;

        LOG(INFO) << CLASS_NAME << "Checking file exists: " << file_path;

        //Double check the file exists first
        FileZNode znode_data;
        if (!file_exists(file_path)) {
            LOG(ERROR) << CLASS_NAME << "Requested file " << file_path << " does not exist";
            return false;
        }
        read_file_znode(znode_data, file_path);
        if (znode_data.filetype != IS_FILE) { // Assert that the znode we want to modify is a file
            LOG(ERROR) << CLASS_NAME << "Requested file " << file_path << " is not a file";
            return false;
        }
        LOG(INFO) << CLASS_NAME << "File exists. Building multi op to abandon block";

        // Find the last block
        int error_code;
        std::vector<std::string> sorted_fs_znodes;
        if(!zk->get_children(ZookeeperPath(file_path), sorted_fs_znodes, error_code)) {
            LOG(ERROR) << CLASS_NAME << "Failed getting children of " << ZookeeperPath(file_path) << " with error: " << error_code;
        }
        std::sort(sorted_fs_znodes.begin(), sorted_fs_znodes.end());

        // Build the multi op - it's a reverse of the one's in add_block.
        // Note that it was due to the ZOO_SEQUENCE flag that this first
        // znode's path has the 9 digit number on the end.
        auto undo_seq_file_block_op = zk->build_delete_op(ZookeeperPath(file_path + "/" + sorted_fs_znodes.back()));
        auto undo_ack_op = zk->build_delete_op("/work_queues/wait_for_acks/" + block_id_str);

        std::vector<std::string> datanodes;
        if (!find_all_datanodes_with_block(block_id_str, datanodes, error_code)) {
            LOG(ERROR) << CLASS_NAME << "Could not find datandoes with the block";
        }

        std::vector<std::shared_ptr<ZooOp>> ops;
        ops.push_back(undo_seq_file_block_op);
        ops.push_back(undo_ack_op);

        std::vector<uint8_t> block_vec(sizeof(uint64_t));
        memcpy(&block_vec[0], &blockId, sizeof(uint64_t));

        // push delete commands onto ops
        for (auto& dn : datanodes){
            auto delete_queue = util::concat_path(DELETE_QUEUES, dn);
            auto delete_item = util::concat_path(delete_queue, "block-");
            ops.push_back(zk->build_create_op(delete_item, block_vec, ZOO_SEQUENCE));
            blockDeleted(blockId, dn);
        }

        auto results = std::vector <zoo_op_result>();
        int err;

        LOG(INFO) << CLASS_NAME << "Built multi op. Executing multi op to abandon block... " << file_path;
        if (!zk->execute_multi(ops, results, err)) {
            LOG(ERROR) << CLASS_NAME << "Failed to write the abandon_block multiop, ZK state was not changed";
            ZKWrapper::print_error(err);
            for (int i = 0; i < results.size(); i++) {
                LOG(ERROR) << "\t MULTIOP #" << i << " ERROR CODE: " << results[i].err;
            }
            return false;
        }
    	return true;
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
    	return;
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

    bool ZkNnClient::blockDeleted(uint64_t uuid, std::string id) {
        int error_code;
        bool exists;

        LOG(INFO) << "DataNode deleted a block with UUID " << std::to_string(uuid);

        auto ops = std::vector<std::shared_ptr<ZooOp>>();

        // Delete block locations
        if (zk->exists(BLOCK_LOCATIONS + std::to_string(uuid) + "/" + id, exists, error_code)) {
            if (exists) {
                ops.push_back(zk->build_delete_op(BLOCK_LOCATIONS + std::to_string(uuid) + "/" + id));
                std::vector <std::string> children = std::vector <std::string>();
                if(!zk->get_children(BLOCK_LOCATIONS + std::to_string(uuid), children, error_code)){
                    LOG(ERROR) << "getting children failed";
                }
                // If deleting last child of block locations, delete block locations at this block uuid
                if (children.size() == 1) {
                    ops.push_back(zk->build_delete_op(BLOCK_LOCATIONS + std::to_string(uuid)));
                }
            }
        }

        // Delete blocks
        if (zk->exists(HEALTH_BACKSLASH + id + BLOCKS + "/" + std::to_string(uuid), exists, error_code)) {
            if (exists) {
                ops.push_back(zk->build_delete_op(HEALTH_BACKSLASH + id + BLOCKS + "/" + std::to_string(uuid)));
            }
        }

        std::vector<zoo_op_result> results = std::vector<zoo_op_result>();
        if (!zk->execute_multi(ops, results, error_code)) {
            LOG(ERROR) << "Failed multiop when deleting block" << std::to_string(uuid);
            for (int i = 0; i < results.size(); i++) {
                LOG(ERROR) << "\t MULTIOP #" << i << " ERROR CODE: " << results[i].err;
            }
            return false;
        }
        return true;
    }

    bool ZkNnClient::destroy_helper(const std::string& path, std::vector<std::shared_ptr<ZooOp>>& ops){
        LOG(INFO) << "Destroying " << path;
        if (!file_exists(path)){
            LOG(ERROR) << path << " does not exist";
            return false;
        }
        int error_code;
        FileZNode znode_data;
        read_file_znode(znode_data, path);
        std::vector<std::string> children;
        if (!zk->get_children(ZookeeperPath(path), children, error_code)) {
            LOG(FATAL) << "Failed to get children for " << path;
            return false;
        }
        if (znode_data.filetype == IS_DIR){
            for (auto& child : children){
                auto child_path = util::concat_path(path, child);
                if (!destroy_helper(child_path, ops)){
                    return false;
                }
            }
        }
        else if (znode_data.filetype == IS_FILE){
            if (znode_data.under_construction == UNDER_CONSTRUCTION){
                LOG(ERROR) << path << " is under construction, so it cannot be deleted.";
                return false;
            }
            for (auto& child : children){
                auto child_path = util::concat_path(path, child);
                child_path = ZookeeperPath(child_path);
                ops.push_back(zk->build_delete_op(child_path));
                std::vector<std::uint8_t> block_vec;
                std::uint64_t block;
                if (!zk->get(child_path, block_vec, error_code, sizeof(block))){
                    return false;
                }
                block = *reinterpret_cast<std::uint64_t *>(block_vec.data());
                std::vector<std::string> datanodes;

                if (!zk->get_children(util::concat_path(BLOCK_LOCATIONS, std::to_string(block)), datanodes, error_code)) {
                    LOG(ERROR) << CLASS_NAME << "Failed getting datanode locations for block: " << block << " with error: " << error_code;
                    return false;
                }
                // push delete commands onto ops
                for (auto& dn : datanodes){
                    auto delete_queue = util::concat_path(DELETE_QUEUES, dn);
                    auto delete_item = util::concat_path(delete_queue, "block-");
                    ops.push_back(zk->build_create_op(delete_item, block_vec, ZOO_SEQUENCE));
                    blockDeleted(block, dn);
                }
            }
        }
        ops.push_back(zk->build_delete_op(ZookeeperPath(path)));
        return true;
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
            LOG(ERROR) << CLASS_NAME << "Cannot delete " << path << " because it doesn't exist.";
            response.set_result(false);
            return;
        }
        FileZNode znode_data;
        read_file_znode(znode_data, path);

        if (znode_data.filetype == IS_FILE && znode_data.under_construction == UNDER_CONSTRUCTION){
            LOG(ERROR) << CLASS_NAME << "Cannot delete " << path << " because it is under construction.";
            response.set_result(false);
            return;
        }
        if (znode_data.filetype == IS_DIR && !recursive){
            LOG(ERROR) << CLASS_NAME << "Cannot delete " << path << " because it is a directory. Use recursive = true.";
            response.set_result(false);
            return;
        }
        std::vector<std::shared_ptr<ZooOp>> ops;
        if (!destroy_helper(path, ops)){
            response.set_result(false);
            return;
        }
        std::vector<zoo_op_result> results;
        if (!zk->execute_multi(ops, results, error_code)) {
            LOG(ERROR) << CLASS_NAME << "Failed to execute multi op to delete " << path;
            response.set_result(false);
        }
    }

    /**
     * Create a file in zookeeper
     */
    bool ZkNnClient::create_file(CreateRequestProto& request, CreateResponseProto& response) {
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
            return false;
        }

        // If we need to create directories, do so
        if (create_parent) {
            std::string directory_paths = "";
            std::vector<std::string> split_path;
            boost::split(split_path, path, boost::is_any_of("/"));
            LOG(INFO) << CLASS_NAME << split_path.size();
            for (int i = 1; i < split_path.size() - 1; i++) {
                directory_paths += ("/"  + split_path[i]);
            }
            // try and make all the parents
            if (!mkdir_helper(directory_paths, true))
                return false;
        }

        // Now create the actual file which will hold blocks
        FileZNode znode_data;
        znode_data.length = 0;
        znode_data.under_construction = UNDER_CONSTRUCTION;
        uint64_t mslong = current_time_ms();
        znode_data.access_time = mslong;
        znode_data.modification_time = mslong;
        strcpy(znode_data.owner, owner.c_str());
        strcpy(znode_data.group, owner.c_str());
        znode_data.replication = replication;
        znode_data.blocksize = blocksize;
        znode_data.filetype = IS_FILE;

        // if we failed, then do not set any status
        if (!create_file_znode(path, &znode_data))
            return false;


        HdfsFileStatusProto* status = response.mutable_fs();
        set_file_info(status, path, znode_data);

        return true;
    }

    void ZkNnClient::complete(CompleteRequestProto& req, CompleteResponseProto& res) {

        // TODO: Completion makes a few guarantees that we should handle

        int error_code;
        // change the under construction bit
        const std::string& src = req.src();
        FileZNode znode_data;
        read_file_znode(znode_data, src);
        znode_data.under_construction = FILE_COMPLETE;
		// set the file length
		uint64_t file_length = 0;
		auto file_blocks = std::vector<std::string>();
		if (!zk->get_children(ZookeeperPath(src), file_blocks, error_code)) {
			LOG(ERROR) << CLASS_NAME << "Failed getting children of " << ZookeeperPath(src) << " with error: " << error_code;
			res.set_result(false);
			return;
		}
		if (file_blocks.size() == 0) {
			LOG(ERROR) << "No blocks found for file " << ZookeeperPath(src);
			//res.set_result(false);
			res.set_result(true);
			return;
		}
		// TODO: This loop could be two multi-ops instead
		for (auto file_block : file_blocks) {
			auto data = std::vector<std::uint8_t>();
			if (!zk->get(ZookeeperPath(src) + "/" + file_block, data, error_code, sizeof(uint64_t))) {
				LOG(ERROR) << CLASS_NAME << "Failed to get " << ZookeeperPath(src) << "/" << file_block << " with error: " << error_code;
				res.set_result(false);
				return;
			}
			uint64_t block_uuid = *(uint64_t *)(&data[0]);
			auto block_data = std::vector<std::uint8_t>();
			if (!zk->get(BLOCK_LOCATIONS + std::to_string(block_uuid), block_data, error_code, sizeof(uint64_t))) {
				LOG(ERROR) << CLASS_NAME << "Failed to get " << BLOCK_LOCATIONS << std::to_string(block_uuid) << " with error: " << error_code;
				res.set_result(false);
				return;
			}
			uint64_t length = *(uint64_t *)(&block_data[0]);
			file_length += length;
		}
		znode_data.length = file_length;
        std::vector<std::uint8_t> data(sizeof(znode_data));
        file_znode_struct_to_vec(&znode_data, data);
        if (!zk->set(ZookeeperPath(src), data, error_code)) {
            LOG(ERROR) << CLASS_NAME << " complete could not change the construction bit and file length";
            res.set_result(false);
			return;
        }
        res.set_result(true);
    }

    /**
     * Rename a file in the zookeeper filesystem
     */
    void ZkNnClient::rename(RenameRequestProto& req, RenameResponseProto& res) {
		std::string file_path = req.src();

        FileZNode znode_data;
        read_file_znode(znode_data, file_path);
        if (!file_exists(file_path)) {
            LOG(ERROR) << CLASS_NAME << "Requested rename source: " << file_path << " does not exist";
            res.set_result(false);
        }

        auto ops = std::vector<std::shared_ptr<ZooOp>>();
		if (znode_data.filetype == IS_DIR) {
			if(!rename_ops_for_dir(req.src(), req.dst(), ops)) {
				LOG(ERROR) << CLASS_NAME << "Failed to generate reame operatons for: " << file_path;
	            res.set_result(false);
			}

		} else if (znode_data.filetype == IS_FILE) {
			if(!rename_ops_for_file(req.src(), req.dst(), ops)) {
				LOG(ERROR) << CLASS_NAME << "Failed to generate reame operatons for: " << file_path;
	            res.set_result(false);
			}

		} else {
            LOG(ERROR) << CLASS_NAME << "Requested rename source: " << file_path << " is not a file or dir";
            res.set_result(false);
        }

		LOG(INFO) << CLASS_NAME << "Renameing multiop has " << ops.size() << " operations. Executing...";

		int error_code;
        std::vector<zoo_op_result> results;
        if (!zk->execute_multi(ops, results, error_code)) {
            LOG(ERROR) << "Failed multiop when renaming: '" << req.src() << "' to '" << req.dst() << "'";
            for (int i = 0; i < results.size(); i++) {
                LOG(ERROR) << "\t MULTIOP #" << i << " ERROR CODE: " << results[i].err;
            }
            res.set_result(false);
        } else {
			LOG(INFO) << CLASS_NAME << "Successfully exec'd multiop to rename " << req.src() << " to " << req.dst();
	        res.set_result(true);
		}
    }

    // ------- make a directory

    /**
     * Set the default information for a directory znode
     */
    void ZkNnClient::set_mkdir_znode(FileZNode* znode_data) {
        znode_data->length = 0;
        uint64_t mslong = current_time_ms();
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
     * Helper for creating a directory znode. Iterates over the parents and crates them
     * if necessary.
     */
    bool ZkNnClient::mkdir_helper(const std::string& path, bool create_parent) {
       	LOG(INFO) << "mkdir_helper called with input " << path;
	if (create_parent) {
	    std::vector<std::string> split_path;
            boost::split(split_path, path, boost::is_any_of("/"));
            bool not_exist = false;
            std::string unroll;
            std::string p_path = "";
            // Start at index 1 because it includes "/" as the first element
	    // in the array when we do NOT want that
	    for (int i = 1; i < split_path.size(); i++) {
                p_path += "/" + split_path[i];
                LOG(INFO) << "[in mkdir_helper] " << p_path;
		if (!file_exists(p_path)) {
		    // keep track of the path where we start creating directories
                    if (not_exist == false) {
                        unroll = p_path;
                    }
                    not_exist = true;
                    FileZNode znode_data;
                    set_mkdir_znode(&znode_data);
                    int error;
                    if ((error = create_file_znode(p_path, &znode_data))) {
                        // TODO unroll the created directories
                        //return false;
                    }
		} else {
			LOG(INFO) << "mkdir_helper is trying to create";
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

	bool ZkNnClient::get_listing(GetListingRequestProto& req, GetListingResponseProto& res) {
		int error_code;

		const std::string& src = req.src();
		const std::string& start_after = req.startafter();
		const bool need_location = req.needlocation();

		DirectoryListingProto *listing = res.mutable_dirlist();

		// if src is a file then just return that file with remaining = 0
		// otherwise return first 1000 files in src dir starting at start_after and set remaining to the number left after that first 1000
		// TODO handle lengths of more than 1000 files
		if (file_exists(src)) {
			FileZNode znode_data;
			read_file_znode(znode_data, src);
			if (znode_data.filetype == IS_FILE) {
				HdfsFileStatusProto *status = listing->add_partiallisting();
				set_file_info(status, src, znode_data);
			} else {
				std::vector<std::string> children;
				if (!zk->get_children(ZookeeperPath(src), children, error_code)) {
					LOG(FATAL) << "Failed to get children for " << ZookeeperPath(src);
					return false;
				} else {
					for (auto& child : children) {
						auto child_path = util::concat_path(src, child);
						FileZNode child_data;
						read_file_znode(child_data, child_path);
						HdfsFileStatusProto *status = listing->add_partiallisting();
						set_file_info(status, child_path, child_data);
                        // set up the value for LocatedBlocksProto
                        //LocatedBlocksProto *blocklocation = status->set_location();
                        //GetBlockLocationsRequestProto location_req;
                        LocatedBlocksProto* blocks = status->mutable_locations();
                        // TODO: 134217728 should be a variable
                        LOG(INFO) << "[child data length is] " << child_data.length;
                        get_block_locations(child_path, 0, child_data.length, blocks);
                        //get_block_locations()

                    }
				}
			}
			listing->set_remainingentries(0);
		} else {
			LOG(ERROR) << CLASS_NAME << "File does not exist with name " << src;
			return false;
		}
		return true;
	}


    void ZkNnClient::get_block_locations(GetBlockLocationsRequestProto& req, GetBlockLocationsResponseProto& res) {
        const std::string &src = req.src();
        google::protobuf::uint64 offset = req.offset();
        google::protobuf::uint64 length = req.length();
        LocatedBlocksProto* blocks = res.mutable_locations();
        get_block_locations(src, offset, length, blocks);
    }


    void ZkNnClient::get_block_locations(const std::string &src, google::protobuf::uint64 offset, google::protobuf::uint64 length, LocatedBlocksProto* blocks) {

	int error_code;
	const std::string zk_path = ZookeeperPath(src);

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

	std::sort(sorted_blocks.begin(), sorted_blocks.end());

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
			if (!zk->get(zk_path + "/" + sorted_block, data, error_code, sizeof(uint64_t))) {
				LOG(ERROR) << CLASS_NAME << "Failed to get " << zk_path << "/" << sorted_block << " info: " << error_code;
				return; // TODO: Signal error
			}
			uint64_t block_id = *(uint64_t *)(&data[0]);
			LOG(INFO) << CLASS_NAME << "Found block " << block_id << " for " << zk_path;

			// TODO: This block of code should be moved to a function, repeated with add_block
			LocatedBlockProto* located_block = blocks->add_blocks();
			located_block->set_corrupt(0);
			located_block->set_offset(size); // TODO: This offset may be incorrect

			buildExtendedBlockProto(located_block->mutable_b(), block_id, block_size);

			auto data_nodes = std::vector<std::string>();

			LOG(INFO) << CLASS_NAME << "Getting datanode locations for block: " << "/block_locations/" + std::to_string(block_id);

			if (!zk->get_children("/block_locations/" + std::to_string(block_id), data_nodes, error_code)) {
			    LOG(ERROR) << CLASS_NAME << "Failed getting datanode locations for block: " << "/block_locations/" + std::to_string(block_id) << " with error: " << error_code;
			}

			LOG(INFO) << CLASS_NAME << "Found block locations " << data_nodes.size();

			auto sorted_data_nodes = std::vector<std::string>();
			if (sort_by_xmits(data_nodes, sorted_data_nodes)) {
				for (auto data_node = sorted_data_nodes.begin(); data_node != sorted_data_nodes.end(); ++data_node) {
				    LOG(INFO) << "Block DN Loc: " << *data_node;
				    buildDatanodeInfoProto(located_block->add_locs(), *data_node);
				}
			} else {
				LOG(ERROR) << "Unable to sort DNs by # xmits in get_block_locations. Using unsorted instead.";
				for (auto data_node = data_nodes.begin(); data_node != data_nodes.end(); ++data_node) {
				    LOG(INFO) << "Block DN Loc: " << *data_node;
				    buildDatanodeInfoProto(located_block->add_locs(), *data_node);
				}
			}

			buildTokenProto(located_block->mutable_blocktoken());
	    	}
		size += block_size;
	}
    }


    // ---------------------------------------- HELPERS ----------------------------------------

	bool ZkNnClient::sort_by_xmits(const std::vector<std::string> &unsorted_dn_ids, std::vector<std::string> &sorted_dn_ids) {
		int error_code;
        std::priority_queue<TargetDN> targets;

		for (auto datanode : unsorted_dn_ids) {
	        std::string dn_stats_path = HEALTH_BACKSLASH + datanode + STATS;
	        std::vector<uint8_t> stats_payload;
	        stats_payload.resize(sizeof(DataNodePayload));
	        if (!zk->get(dn_stats_path, stats_payload, error_code, sizeof(DataNodePayload))) {
	            LOG(ERROR) << CLASS_NAME << "Failed to get " << dn_stats_path;
	            return false;
	        }
	        DataNodePayload stats = DataNodePayload();
	        memcpy(&stats, &stats_payload[0], sizeof(DataNodePayload));
            targets.push(TargetDN(datanode, stats.free_bytes, stats.xmits));
		}

		while (targets.size() > 0) {
            TargetDN target = targets.top();
            sorted_dn_ids.push_back(target.dn_id);
            targets.pop();
		}
	}

    std::string ZkNnClient::ZookeeperPath(const std::string &hadoopPath){
        std::string zkpath = NAMESPACE_PATH;
        if (hadoopPath.size() == 0) {
            LOG(ERROR) << " this hadoop path is invalid";
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


    void ZkNnClient::get_content(GetContentSummaryRequestProto& req, GetContentSummaryResponseProto& res) {
        const std::string& path = req.path();

        if (file_exists(path)) {
            LOG(INFO) << CLASS_NAME << "File exists";
            // read the node into the file node struct
            FileZNode znode_data;
            read_file_znode(znode_data, path);

            // set the file status in the get file info response res
            ContentSummaryProto* status = res.mutable_summary();

            set_file_info_content(status, path, znode_data);
            LOG(INFO) << CLASS_NAME << "Got info for file ";
            return;
        }
        LOG(INFO) << CLASS_NAME << "No file to get info for";
        return;
    }

    void ZkNnClient::set_file_info_content(ContentSummaryProto* status, const std::string& path, FileZNode& znode_data) {
        // get the filetype, since we do not want to serialize an enum
        int error_code = 0;
        if (znode_data.filetype == IS_FILE){
                    int num_file = 0;
                    int num_dir = 0;
                    std::vector<std::string> children;
                    if (!zk->get_children(ZookeeperPath(path), children, error_code)) {
                        LOG(FATAL) << "Failed to get children for " << ZookeeperPath(path);
                    } else {
                        for (auto& child : children) {
                            auto child_path = util::concat_path(path, child);
                            FileZNode child_data;
                            read_file_znode(child_data, child_path);
                            if (znode_data.filetype == IS_FILE){
                                num_file += 1;
                            }
                            else {
                                num_dir += 1;
                            }

                        }
                    }


                status->set_filecount(num_file);
                status->set_directorycount(num_dir);
        }
        else {
            status->set_filecount(1);
            status->set_directorycount(0);
        }


        //status->set_filetype(filetype);
        //status->set_path(path);
        status->set_length(znode_data.length);
        status->set_quota(1);
        status->set_spaceconsumed(1);
        status->set_spacequota(100000);


        LOG(INFO) << CLASS_NAME << "Successfully set the file info ";
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

    bool ZkNnClient::add_block(const std::string& file_path, std::uint64_t& block_id, std::vector<std::string> & data_nodes, uint32_t replicationFactor) {

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

        util::generate_uuid(block_id);
        block_id_str = std::to_string(block_id);
        LOG(INFO) << CLASS_NAME << "Generated block id " << block_id_str;

        if (!find_datanode_for_block(data_nodes, block_id, replicationFactor, true, znode_data.blocksize)) {
            return false;
        }

        // Generate the massive multi-op for creating the block

        std::vector<std::uint8_t> data;
        data.resize(sizeof(u_int64_t));
        memcpy(&data[0], &block_id, sizeof(u_int64_t));

        LOG(INFO) << CLASS_NAME << "Generating block for " << ZookeeperPath(file_path);

        // ZooKeeper multi-op to add block
	// (Note the actual path of the znode the first create op makes will have a 9
	// digit number appended onto the end because of the ZOO_SEQUENCE flag)
        auto seq_file_block_op = zk->build_create_op(ZookeeperPath(file_path + "/block_"), data, ZOO_SEQUENCE);
        auto ack_op = zk->build_create_op("/work_queues/wait_for_acks/" + block_id_str, ZKWrapper::EMPTY_VECTOR);
        auto block_location_op = zk->build_create_op("/block_locations/" + block_id_str, ZKWrapper::EMPTY_VECTOR);

        std::vector<std::shared_ptr<ZooOp>> ops = {seq_file_block_op, ack_op, block_location_op};

        auto results = std::vector <zoo_op_result>();
        int err;
		// We do not need to sync this multi-op immediately
        if (!zk->execute_multi(ops, results, err, false)) {
            LOG(ERROR) << CLASS_NAME << "Failed to write the addBlock multiop, ZK state was not changed";
            ZKWrapper::print_error(err);
            return false;
        }
        return true;
    }

    // TODO: To simplify signature, could just get rid of the newBlock param
    // and always check for preexisting replicas
    bool ZkNnClient::find_datanode_for_block(std::vector<std::string>& datanodes, const u_int64_t blockId, uint32_t replication_factor, bool newBlock, uint64_t blocksize) {
        // TODO: Actually perform this action
        // TODO: Perhaps we should keep a cached list of nodes

        std::vector<std::string> live_data_nodes = std::vector <std::string>();
        int error_code;

        // Get all of the live datanodes
        if (zk->get_children(HEALTH, live_data_nodes, error_code)) {

            // LOG(INFO) << CLASS_NAME << "Found live DNs: " << live_data_nodes;
            auto excluded_datanodes = std::vector <std::string>();
            if (!newBlock) {
                // Get the list of datanodes which already have a replica
                if (!zk->get_children(BLOCK_LOCATIONS + std::to_string(blockId), excluded_datanodes, error_code)) {
                    LOG(ERROR) << CLASS_NAME << "Error getting children of: " << BLOCK_LOCATIONS + std::to_string(blockId);
                    return false;
                }
            }

            std::priority_queue<TargetDN> targets;
            /* for each child, check if the ephemeral node exists */
            for(auto datanode : live_data_nodes) {
                bool isAlive;
                if (!zk->exists(HEALTH_BACKSLASH + datanode + HEARTBEAT, isAlive, error_code)) {
                    LOG(ERROR) << CLASS_NAME << "Failed to check if datanode: " + datanode << " is alive: " << error_code;
                }
                if (isAlive) {
                    bool exclude = false;
                    if (excluded_datanodes.size() > 0) {
                        // Remove the excluded datanodes from the live list
                        std::vector<std::string>::iterator it = std::find(
                                excluded_datanodes.begin(),
                                excluded_datanodes.end(),
                                datanode);

                        if (it == excluded_datanodes.end()) {
                            // This datanode was not in the excluded list, so keep it
                            exclude = false;
                        } else {
                            exclude = true;
                        }
                    }

                    if (!exclude) {
                        std::string dn_stats_path = HEALTH_BACKSLASH + datanode + STATS;
                        std::vector<uint8_t> stats_payload;
                        stats_payload.resize(sizeof(DataNodePayload));
                        if (!zk->get(dn_stats_path, stats_payload, error_code, sizeof(DataNodePayload))) {
                            LOG(ERROR) << CLASS_NAME << "Failed to get " << dn_stats_path;
                            return false;
                        }
                        DataNodePayload stats = DataNodePayload();
                        memcpy(&stats, &stats_payload[0], sizeof(DataNodePayload));
                        LOG(INFO) << "\t DN stats - free_bytes: " << unsigned(stats.free_bytes);
                        if (stats.free_bytes > blocksize) {
							auto queue = REPLICATE_QUEUES + datanode;
							auto repl_item = util::concat_path(queue, std::to_string(blockId));
							bool alreadyOnQueue;
                            // do not put something on queue if its already on there
							if (zk->exists(repl_item, alreadyOnQueue, error_code)) {
								if (alreadyOnQueue) {
                                    LOG(INFO) << "Skipping target" << datanode;
                                    continue;
								}
							}
                            LOG(INFO) << "Pushed target: " << datanode << " with " << stats.xmits << " xmits";
                            targets.push(TargetDN(datanode, stats.free_bytes, stats.xmits));
                        }
                    }
                }
            }

            LOG(INFO) << "There are " << targets.size() << " viable DNs. Requested: " << replication_factor;
            if (targets.size() < replication_factor) {
                LOG(ERROR) << "Not enough available DNs! Available: " << targets.size() << " Requested: " << replication_factor;
                // no return because we still want the client to write to some datanodes
            }

            while (datanodes.size() < replication_factor && targets.size() > 0) {
                LOG(INFO) << "DNs size IS : " << datanodes.size();
                TargetDN target = targets.top();
                datanodes.push_back(target.dn_id);
                LOG(INFO) << "Selecting target DN " << target.dn_id << " with " << target.num_xmits << " xmits and " << target.free_bytes << " free bytes";
                targets.pop();
            }

        } else {
            LOG(ERROR) << CLASS_NAME << "Failed to get list of datanodes at " + HEALTH + " " << error_code;
            return false;
        }

        // TODO: Read strategy from config, but as of now select the first few blocks that are valid

        /* Select a random subset of the datanodes if we've gotten more than the # requested. */
        while (datanodes.size() > replication_factor) {
            auto rem = rand() % datanodes.size();
            std::swap(datanodes[rem], datanodes.back());
            datanodes.pop_back();
        }

        if (datanodes.size() < replication_factor) {
            LOG(ERROR) << CLASS_NAME << "Failed to find at least " << replication_factor << " datanodes at " + HEALTH;
            // no return because we still want the client to write to some datanodes
        }

        return true;
    }


    /**
     * Generates multiop ops for renaming src to dst
     * @param src The path to the source file (not znode) within the filesystem
     * @param dst The path to the renamed destination file (not znode) within the filesystem
	 * @param ops The vector of multiops which will make up the overall atomic rename operation
     * @return Boolean indicating success or failure of the rename
     */
    bool ZkNnClient::rename_ops_for_file(const std::string &src, const std::string &dst, std::vector<std::shared_ptr<ZooOp>> &ops) {
        int error_code = 0;
        auto data = std::vector<std::uint8_t>();
        std::string src_znode = ZookeeperPath(src);
        std::string dst_znode = ZookeeperPath(dst);

        // Get the payload from the old filesystem znode for the src
        zk->get(src_znode, data, error_code);
        if (error_code != ZOK) {
            LOG(ERROR) << "Failed to get data from '" << src_znode << "' when renaming.";
            return false;
        }

        // Create a new znode in the filesystem for the dst
		LOG(INFO) << "Added op#" << ops.size() << ": create " << dst_znode;
        ops.push_back(zk->build_create_op(dst_znode, data));

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
			LOG(INFO) << "Added op#" << ops.size() << ": create " << dst_znode + "/" + child;
            ops.push_back(zk->build_create_op(dst_znode + "/" + child, child_data));

            // Delete src_znode's child
			LOG(INFO) << "Added op#" << ops.size() << ": delete " << src_znode + "/" + child;
            ops.push_back(zk->build_delete_op(src_znode + "/" + child));
        }

        // Remove the old znode for the src
		LOG(INFO) << "Added op#" << ops.size() << ": delete " << src_znode;
        ops.push_back(zk->build_delete_op(src_znode));

        return true;
    }


    bool ZkNnClient::rename_ops_for_dir(const std::string &src, const std::string &dst, std::vector<std::shared_ptr<ZooOp>> &ops) {
		// Create a znode for the dst dir
        int error_code = 0;
        auto data = std::vector<std::uint8_t>();
        std::string src_znode = ZookeeperPath(src);
        std::string dst_znode = ZookeeperPath(dst);

        // Get the payload from the old filesystem znode for the src
        zk->get(src_znode, data, error_code);
        if (error_code != ZOK) {
            LOG(ERROR) << "Failed to get data from '" << src_znode << "' when renaming.";
            return false;
        }

        // Create a new znode in the filesystem for the dst
		LOG(INFO) << "rename_ops_for_dir - Added op#" << ops.size() << ": create " << dst_znode;
        ops.push_back(zk->build_create_op(dst_znode, data));

		// Iterate through the items in this dir
		auto children = std::vector<std::string>();
        if (!zk->get_children(src_znode, children, error_code)) {
            LOG(ERROR) << "Failed to get children of znode '" << src_znode << "' when renaming.";
            return false;
        }

		auto nested_dirs = std::vector<std::string>();
		for (auto child : children) {
			std::string child_path = src + "/" + child;
	        FileZNode znode_data;
	        read_file_znode(znode_data, child_path);
			if (znode_data.filetype == IS_DIR) {
				LOG(INFO) << "Child: " << child << " is DIR";
				// Keep track of any nested directories
				nested_dirs.push_back(child);
			} else if (znode_data.filetype == IS_FILE) {
				// Generate ops for each file in the dir
				LOG(INFO) << "Child: " << child << " is FILE";
				if(!rename_ops_for_file(src + "/" + child, dst + "/" + child, ops)) {
					return false;
				}
			} else {
	            LOG(ERROR) << CLASS_NAME << "Requested rename source: " << child_path << " is not a file or dir";
				return false;
	        }
		}

		// Iterate through the found nested directories and generate ops for them
		LOG(INFO) << "Found " << nested_dirs.size() << " nested dirs";
		for (auto dir : nested_dirs) {
			LOG(INFO) << "Call recursively on: " << src + "/" + dir;
			if(!rename_ops_for_dir(src + "/" + dir, dst + "/" + dir, ops)) {
				return false;
			}
		}
		// Delete the old dir
		LOG(INFO) << "Added op#" << ops.size() << ": delete " << src_znode;
        ops.push_back(zk->build_delete_op(src_znode));

		return true;
	}

    /**
	 * Checks that each block UUID in the wait_for_acks dir:
	 *	 1. has REPLICATION_FACTOR many children
	 *	 2. if the block UUID was created more than ACK_TIMEOUT milliseconds ago
	 * TODO: Add to header file
	 * @return
	 */
    bool ZkNnClient::check_acks() {
        int error_code = 0;

        // Get the current block UUIDs that are waiting to be fully replicated
        // TODO: serialize block_uuids as u_int64_t rather than strings
        auto block_uuids = std::vector<std::string>();
        // TODO: Change all path constants in zk_client_common to NOT end in /
        if (!zk->get_children(WORK_QUEUES + WAIT_FOR_ACK, block_uuids, error_code)) {
            LOG(ERROR) << CLASS_NAME << "ERROR CODE: " << error_code << " occurred in check_acks when getting children for " << WORK_QUEUES + WAIT_FOR_ACK;
            return false; // TODO: Is this the right return val?
        }
        LOG(INFO) << CLASS_NAME << "Checking acks for: " << block_uuids.size() << " blocks";

        for (auto block_uuid : block_uuids) {
            LOG(INFO) << CLASS_NAME << "Considering block: " << block_uuid;
            std::string block_path = WORK_QUEUES + WAIT_FOR_ACK_BACKSLASH + block_uuid;

            auto data = std::vector<std::uint8_t>();
            if (!zk->get(block_path, data, error_code)) {
                LOG(ERROR) << CLASS_NAME << "Error getting payload at: " << block_path;
                return false;
            }
            int replication_factor = unsigned(data[0]);
            LOG(INFO) << CLASS_NAME << "Replication factor for " << block_uuid << " is " << replication_factor;

            // Get the datanodes with have replicated this block
            auto existing_dn_replicas = std::vector<std::string>();
            if (!zk->get_children(block_path, existing_dn_replicas, error_code)) {
                LOG(ERROR) << CLASS_NAME << "ERROR CODE: " << error_code << " occurred in check_acks when getting children for " << block_path;
                return false;
            }
            LOG(INFO) << CLASS_NAME << "Found " << existing_dn_replicas.size() << " replicas of " << block_uuid;

            int elapsed_time = ms_since_creation(block_path);
            if (elapsed_time < 0) {
                LOG(ERROR) << CLASS_NAME << "Failed to get elapsed time";
            }

            if (existing_dn_replicas.size() == 0 && elapsed_time > ACK_TIMEOUT) {
                // Block is not available on any DNs and cannot be replicated.
                // Emit error and remove this block from wait_for_acks
                LOG(ERROR) << CLASS_NAME << block_path << " has 0 replicas! Delete from wait_for_acks.";
                if (!zk->delete_node(block_path, error_code)) {
                    LOG(ERROR) << CLASS_NAME << "Failed to delete: " << block_path;
                    return false;
                }
                return false;

            } else if (existing_dn_replicas.size() < replication_factor && elapsed_time > ACK_TIMEOUT) {
                LOG(INFO) << CLASS_NAME << "Not yet enough replicas after time out for " << block_uuid;
                // Block hasn't been replicated enough, request remaining replicas
                int replicas_needed = replication_factor - existing_dn_replicas.size();
                LOG(INFO) << CLASS_NAME << replicas_needed << " replicas are needed";

				std::vector<std::string> to_replicate;
				for (int i = 0; i < replicas_needed; i++) {
					to_replicate.push_back(block_uuid);
				}
                if (!replicate_blocks(to_replicate, error_code)) {
					LOG(ERROR) << "Failed to add necessary items to replication queue.";
					return false;
				}
				LOG(INFO) << "Created " << to_replicate.size() << " items in the replication queue.";

            } else if (existing_dn_replicas.size() == replication_factor) {
                LOG(INFO) << CLASS_NAME << "Enough replicas have been made, no longer need to wait on " << block_path;
                if (!zk->recursive_delete(block_path, error_code)) {
                    LOG(ERROR) << CLASS_NAME << "Failed to delete: " << block_path;
                    return false;
                }
            } else {
                LOG(INFO) << CLASS_NAME << "Not enough replicas, but still time left" << block_path;
            }
        }

        return true;
    }

	bool ZkNnClient::replicate_blocks(const std::vector<std::string> &to_replicate, int err) {
	        std::vector<std::shared_ptr<ZooOp>> ops;
	        std::vector<zoo_op_result> results;

		for (auto repl : to_replicate) {
			std::string read_from;
			std::vector<std::string> target_dn;
			std::uint64_t block_id;
			std::stringstream strm(repl);
			strm >> block_id;
			uint64_t blocksize;
			if (!get_block_size(block_id, blocksize)) {
				LOG(ERROR) << CLASS_NAME << "Replicate could not read the block size for block: " << block_id;
				return false;
			}
			if (!find_datanode_for_block(target_dn, block_id, 1, false, blocksize) || target_dn.size() == 0) {
				LOG(ERROR) << CLASS_NAME << " Failed to find datanode for this block! " << repl;
				return false;
			}
			auto queue = REPLICATE_QUEUES + target_dn[0];
			auto repl_item = util::concat_path(queue, repl);
			ops.push_back(zk->build_create_op(repl_item, ZKWrapper::EMPTY_VECTOR));
		}

		// We do not need to sync this multi-op immediately
		if (!zk->execute_multi(ops, results, err, false)){
			LOG(ERROR) << "Failed to execute multiop for replicate_blocks";
			return false;
		}
	}


    bool ZkNnClient::find_all_datanodes_with_block(const std::string &block_uuid_str, std::vector<std::string>
        &rdatanodes, int &error_code) {

        std::string block_loc_path = BLOCK_LOCATIONS + block_uuid_str;

        if (!zk->get_children(block_loc_path, rdatanodes, error_code)) {
            LOG(ERROR) << "Failed to get children of: " << block_loc_path;
            return false;
        }
        if (rdatanodes.size() < 1) {
            LOG(ERROR) << "There are no datanodes with a replica of block " << block_uuid_str;
            return false;
        }
        return true;
    }

    int ZkNnClient::ms_since_creation(std::string &path) {
        int error;
        struct Stat stat;
        if (!zk->get_info(path, stat, error)) {
            LOG(ERROR) << "Failed to get info for: " << path;
            return -1;
        }
        LOG(INFO) << "Creation time of " << path << " was: " << stat.ctime << " ms ago";
        uint64_t current_time = current_time_ms();
        LOG(INFO) << "Current time is: " << current_time << "ms";
        int elapsed = current_time - stat.ctime;
        LOG(INFO) << "Elapsed ms: " << elapsed;
        return elapsed;
    }

    /**
     * Returns the current timestamp in milliseconds
     */
    uint64_t ZkNnClient::current_time_ms() {
        // http://stackoverflow.com/questions/19555121/how-to-get-current-timestamp-in-milliseconds-since-1970-just-the-way-java-gets
        struct timeval tp;
        // Get current timestamp
        gettimeofday(&tp, NULL);
        // Convert to milliseconds
        return (uint64_t) tp.tv_sec * 1000L + tp.tv_usec / 1000;
    }


    bool ZkNnClient::buildDatanodeInfoProto(DatanodeInfoProto* dn_info, const std::string& data_node) {

        int error_code;

        std::vector<std::string> split_address;
        boost::split(split_address, data_node, boost::is_any_of(":"));
        assert(split_address.size() == 2);

        auto data = std::vector<std::uint8_t>();
        if (zk->get(HEALTH_BACKSLASH + data_node + STATS, data, error_code, sizeof(zkclient::DataNodePayload))) {
            LOG(ERROR) << CLASS_NAME << "Getting data node stats failed with " << error_code;
        }

        zkclient::DataNodePayload * payload = (zkclient::DataNodePayload *) (&data[0]);

        DatanodeIDProto* id = dn_info->mutable_id();
        dn_info->set_location("/fixlocation");
		id->set_ipaddr(split_address[0]);
        id->set_hostname("localhost"); // TODO: Fill out with the proper value
        id->set_datanodeuuid("1234");
        id->set_xferport(payload->xferPort);
        id->set_infoport(50020);
        id->set_ipcport(payload->ipcPort);

		return true;
    }

    bool ZkNnClient::buildTokenProto(hadoop::common::TokenProto* token) {
        token->set_identifier("open");
        token->set_password("sesame");
        token->set_kind("foo");
        token->set_service("bar");
        return true;
    }

    bool ZkNnClient::buildExtendedBlockProto(ExtendedBlockProto* eb, const std::uint64_t& block_id,
                                             const uint64_t& block_size) {
		eb->set_poolid("0");
        eb->set_blockid(block_id);
        eb->set_generationstamp(1);
        eb->set_numbytes(block_size);
        return true;
    }
}

#endif
