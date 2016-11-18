#ifndef RDFS_ZK_CLIENT_DN_CC
#define RDFS_ZK_CLIENT_DN_CC

#include "zk_dn_client.h"
#include "zk_lock.h"
#include "zk_queue.h"
#include <easylogging++.h>

namespace zkclient{


	const std::string ZkClientDn::CLASS_NAME = ": **ZkClientDn** : ";

	ZkClientDn::ZkClientDn(const std::string& ip, const std::string& hostname, std::shared_ptr <ZKWrapper> zk_in,
		const uint32_t ipcPort, const uint32_t xferPort) : ZkClientCommon(zk_in) {
			// TODO: refactor with the next constructor.

			data_node_id = DataNodeId();
			data_node_id.ip = ip;
			data_node_id.ipcPort = ipcPort;

			// TODO: Fill in other data_node stats, and refactor with the blow.
			data_node_payload = DataNodePayload();
			data_node_payload.ipcPort = ipcPort;
			data_node_payload.xferPort = xferPort;

			registerDataNode();
			LOG(INFO) << "Registered datanode " + build_datanode_id(data_node_id);

		}

	ZkClientDn::ZkClientDn(const std::string& ip, const std::string& hostname, const std::string& zkIpAndAddress,
		const uint32_t ipcPort, const uint32_t xferPort) : ZkClientCommon(zkIpAndAddress) {

			data_node_id = DataNodeId();
			data_node_id.ip = ip;
			data_node_id.ipcPort = ipcPort;

			// TODO: Fill in other data_node stats
			data_node_payload = DataNodePayload();
			data_node_payload.ipcPort = ipcPort;
			data_node_payload.xferPort = xferPort;

			registerDataNode();
			LOG(INFO) << "Registered datanode " + build_datanode_id(data_node_id);
		}

	bool ZkClientDn::blockReceived(uint64_t uuid, uint64_t size_bytes) {
		int error_code;
		bool exists;

		LOG(INFO) << "DataNode received a block with UUID " << std::to_string(uuid);
		std::string id = build_datanode_id(data_node_id);


		bool created_correctly = true;

		// Add acknowledgement
		ZKLock queue_lock(*zk.get(), WORK_QUEUES + WAIT_FOR_ACK_BACKSLASH + std::to_string(uuid));
		if (queue_lock.lock() != 0) {
			LOG(ERROR) << CLASS_NAME <<  "Failed locking on /work_queues/wait_for_acks/<block_uuid> " << error_code;
			created_correctly = false;
		}


		if (zk->exists(WORK_QUEUES + WAIT_FOR_ACK_BACKSLASH + std::to_string(uuid), exists, error_code)) {
			if (!exists) {
				if(!zk->create(WORK_QUEUES + WAIT_FOR_ACK_BACKSLASH + std::to_string(uuid), ZKWrapper::EMPTY_VECTOR, error_code)) {
					LOG(ERROR) << CLASS_NAME <<  "Failed to create wait_for_acks/<block_uuid> " << error_code;
					created_correctly = false;
				}
			} else {
				created_correctly = false;
			}
			if(!zk->create(WORK_QUEUES + WAIT_FOR_ACK_BACKSLASH + std::to_string(uuid) + "/" + id, ZKWrapper::EMPTY_VECTOR, error_code, false)) {
				LOG(ERROR) << CLASS_NAME <<  "Failed to create wait_for_acks/<block_uuid>/datanode_id " << error_code;
				created_correctly = false;
			}
		}

		if (queue_lock.unlock() != 0) {
			LOG(ERROR) << CLASS_NAME <<  "Failed unlocking on /work_queues/wait_for_acks/<block_uuid> " << error_code;
			created_correctly = false;
		}


		if (zk->exists(BLOCK_LOCATIONS + std::to_string(uuid), exists, error_code)) {
			if (exists) {
				// Write the block size
				BlockZNode block_data;
				block_data.block_size = size_bytes;
				std::vector<std::uint8_t> data_vect(sizeof(block_data));
				memcpy(&data_vect[0], &block_data, sizeof(block_data));
				if(!zk->set(BLOCK_LOCATIONS + std::to_string(uuid), data_vect, error_code)) {
					LOG(ERROR) << CLASS_NAME <<  "Failed writing block size to /block_locations/<block_uuid> " << error_code;
					created_correctly = false;
				}

				// Add this datanode as the block's location in block_locations
				if(!zk->create(BLOCK_LOCATIONS + std::to_string(uuid) + "/" + id, ZKWrapper::EMPTY_VECTOR, error_code, false)) {
					LOG(ERROR) << CLASS_NAME <<  "Failed creating /block_locations/<block_uuid>/<datanode_id> " << error_code;
					created_correctly = false;
				}
			}
			else {
				LOG(ERROR) << CLASS_NAME << "/block_locations/<block_uuid> did not exist " << error_code;
				return false;
			}
		}

		// Write block to /blocks
		if (zk->exists(HEALTH_BACKSLASH + id + BLOCKS, exists, error_code)) {
			if (exists) {
				if (!zk->create(HEALTH_BACKSLASH + id + BLOCKS + "/" + std::to_string(uuid), ZKWrapper::EMPTY_VECTOR, error_code)) {
					LOG(ERROR) << CLASS_NAME <<  "Failed creating /health/<data_node_id>/blocks/<block_uuid> " << error_code;
				}
			}
		}


		return created_correctly;
	}

	void ZkClientDn::registerDataNode() {
		// TODO: Consider using startup time of the DN along with the ip and port
		int error_code;
		bool exists;

		std::string id = build_datanode_id(data_node_id);
		// TODO: Add a watcher on the health node
		if (zk->exists(HEALTH_BACKSLASH + id, exists, error_code)) {
			if (exists) {
				if (!zk->recursive_delete(HEALTH_BACKSLASH + id, error_code)) {
					LOG(ERROR) << CLASS_NAME <<  "Failed deleting /health/<data_node_id> " << error_code;
				}
			}
		}

		if (!zk->create(HEALTH_BACKSLASH + id, ZKWrapper::EMPTY_VECTOR, error_code)) {
			LOG(ERROR) << CLASS_NAME <<  "Failed creating /health/<data_node_id> " << error_code;
		}


		// Create an ephemeral node at /health/<datanode_id>/heartbeat
		// if it doesn't already exist. Should have a ZOPERATIONTIMEOUT

		if (!zk->create(HEALTH_BACKSLASH + id + HEARTBEAT, ZKWrapper::EMPTY_VECTOR, error_code, true)) {
			LOG(ERROR) << CLASS_NAME <<  "Failed creating /health/<data_node_id>/heartbeat " << error_code;
		}

		std::vector<uint8_t> data;
		data.resize(sizeof(DataNodePayload));
		memcpy(&data[0], &data_node_payload, sizeof(DataNodePayload));

		if (!zk->create(HEALTH_BACKSLASH + id + STATS, data, error_code, false)) {
			LOG(ERROR) << CLASS_NAME <<  "Failed creating /health/<data_node_id>/stats " << error_code;
		}

		if (!zk->create(HEALTH_BACKSLASH + id + BLOCKS, ZKWrapper::EMPTY_VECTOR, error_code)) {
			LOG(ERROR) << CLASS_NAME <<  "Failed creating /health/<data_node_id>/blocks " << error_code;
		}

		// Create the work queues, set their watchers
		ZkClientDn::initWorkQueue(REPLICATE_QUEUES, id);
		ZkClientDn::initWorkQueue(DELETE_QUEUES, id);

		// Register the queue watchers for this dn
		std::vector <std::string> children = std::vector <std::string>();
		if(!zk->wget_children(REPLICATE_QUEUES + id, children, ZkClientDn::thisDNReplicationQueueWatcher, this, error_code)){
			LOG(INFO) << "getting children for replicate queue failed";
		}
		if(!zk->wget_children(DELETE_QUEUES + id, children, ZkClientDn::thisDNDeleteQueueWatcher, this, error_code)){
			LOG(INFO) << "getting children for delete queue failed";
		}

        // TODO: For debugging only
		std::vector <std::uint8_t> replUUID (1);
		replUUID[0] = 12;
        push(zk, REPLICATE_QUEUES + id, replUUID, error_code);
	}

	void ZkClientDn::initWorkQueue(std::string queueName, std::string id){
        int error_code;
        bool exists;

        // Creqte queue for this datanode
        // TODO: Replace w/ actual queues when they're created
        if (zk->exists(queueName + id, exists, error_code)){
            if (!exists){
                LOG(INFO) << "doesn't exist, trying to make it";
                if (!zk->create(queueName + id, ZKWrapper::EMPTY_VECTOR, error_code, false)){
                    LOG(INFO) << "Creation failed";
                }
            }
        }

	}

	void ZkClientDn::thisDNReplicationQueueWatcher(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx){
		LOG(INFO) << "In the replication watcher";
		LOG(INFO) << "Replication watcher triggered on path: " << path;
		int error_code;

		ZkClientDn *thisDn = static_cast<ZkClientDn *>(watcherCtx);
		thisDn->processReplQueue(path);
	}

	void ZkClientDn::thisDNDeleteQueueWatcher(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx){
		LOG(INFO) << "Delete watcher triggered on path: " << path;

		ZkClientDn *thisDn = static_cast<ZkClientDn *>(watcherCtx);
		thisDn->processDeleteQueue(path);
	}

	void ZkClientDn::processReplQueue(std::string path){
                int error_code;
                bool exists;
		std::string peeked;
		std::vector<uint8_t> queue_vec(1); //TODO: Size?

		LOG(INFO) << "In process repl queue";

		peek(zk, path, peeked, error_code);
		while(peeked != path){
			LOG(INFO) << "Found queue item";
			if(pop(zk, path, queue_vec, error_code)){
				LOG(INFO) << "Popped node w/ block id: " + std::to_string(queue_vec[0]);
			}else{
				LOG(INFO) << "Error popping from queue while processing Replication Queue";
			}
			peek(zk, path, peeked, error_code);
		}
	}

	void ZkClientDn::processDeleteQueue(std::string path) {
		int error_code;
		bool exists;
		std::string peeked;
		std::vector<uint8_t> queue_vec(1); //TODO: Size?

		peek(zk, path, peeked, error_code);
		while(peeked != path){
			LOG(INFO) << "Found item in delete queue";

			peek(zk, path, peeked, error_code);
		}

	}

	ZkClientDn::~ZkClientDn() {
		zk->close();
	}

	std::string ZkClientDn::build_datanode_id(DataNodeId data_node_id) {
		return data_node_id.ip + ":" + std::to_string(data_node_id.ipcPort);
	}

	void ZkClientDn::incrementNumXmits(){
		xmits++;
	}

	void ZkClientDn::decrementNumXmits(){
		xmits--;
	}
}
#endif //RDFS_ZK_CLIENT_DN_H
