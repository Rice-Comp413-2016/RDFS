//
// Created by Prudhvi Boyapalli on 10/3/16.
//
//
// Modified by Zhouhan Chen on 10/4/16.
//

#include "zkwrapper.h"

const std::string ZKWrapper::CLASS_NAME = ": **ZkWrapper** : ";

int init = 0;
zhandle_t *zh;
clientid_t myid;

const std::vector <std::uint8_t>
ZKWrapper::EMPTY_VECTOR = std::vector<std::uint8_t>(0);

const std::map<int, std::string> ZKWrapper::error_message = {
	{0, "ZOK"},
	{-1, "ZSYSTEMERROR"},
	{-2, "ZRUNTIMEINCONSISTENCY"},
	{-3, "ZDATAINCONSISTENCY"},
	{-4, "ZCONNECTIONLOSS"},
	{-5, "ZMARSHALLINGERROR"},
	{-6, "ZUNIMPLEMENTED"},
	{-7, "ZOPERATIONTIMEOUT"},
	{-8, "ZBADARGUMENTS"},
	{-9, "ZINVALIDSTATE"},
	{-100, "ZAPIERROR"},
	{-101, "ZNONODE"},
	{-102, "ZNOAUTH"},
	{-103, "ZBADVERSION"},
	{-108, "ZNOCHILDRENFOREPHEMERALS"},
	{-110, "ZNODEEXISTS"},
	{-111, "ZNOTEMPTY"},
	{-112, "ZSESSIONEXPIRED"},
	{-113, "ZINVALIDCALLBACK"},
	{-114, "ZINVALIDACL"},
	{-115, "ZAUTHFAILED"},
	{-116, "ZCLOSING"},
	{-117, "ZNOTHING"},
	{-118, "ZSESSIONMOVED"},
	{-120, "ZNEWCONFIGNOQUORUM"},
	{-121, "ZRECONFIGINPROGRESS"},
	{-999, "ZKWRAPPERDEFAULTERROR"},
};

/**
 * TODO
 *
 * @param zzh zookeeper handle
 * @param type type of event
 * @param state state of the event
 * @param path path to the watcher node
 * @param watcherCtx the state of the watcher
 */
void watcher(zhandle_t *zzh,
		int type,
		int state,
		const char *path,
		void *watcherCtx) {
	LOG(INFO) << "[Global watcher] Watcher triggered on path '" << path << "'"
		;
	char health[] = "/health/datanode_";
	if (type == ZOO_SESSION_EVENT) {
		if (state == ZOO_CONNECTED_STATE) {
			return;
		} else if (state == ZOO_AUTH_FAILED_STATE) {
			zookeeper_close(zzh);
			exit(1);
		} else if (state == ZOO_EXPIRED_SESSION_STATE) {
			zookeeper_close(zzh);
			exit(1);
		}
	}
}


watcher_fn ZKWrapper::watcher_health_factory(std::string path){
	class factory_wrapper{
		public:

			static void watcher_health(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx) {

				struct String_vector stvector;
				struct String_vector *vector = &stvector;
				/* reinstall watcher */
				int rc = zoo_wget_children(zzh, path, watcher_health, nullptr, vector);
				LOG(INFO)  <<  "[In watcher_health rc] health:" << rc;
				int i;
				std::vector <std::string> children;
				for (i = 0; i < stvector.count; i++) {
					children.push_back(stvector.data[i]);
				}

				if (children.size() == 0){
					// ZkNnClient::CLASS_NAME is not in scope when put into zkwrapper
					LOG(INFO) <<  "no childs to retrieve";
				}

				for (int i = 0; i < children.size(); i++) {
					LOG(INFO) <<  "[In watcher_health] Attaching child to " << children[i];
					//ZkClientCommon::HEALTH_BACKSLASH + children[i]).c_str(),
					int rc = zoo_wget_children(zzh, (path+children[i]).c_str(),
							ZKWrapper::watcher_health_child, nullptr,
							vector);
				}
			}
	};
	return factory_wrapper::watcher_health;
}

/*
 * Watcher for health child node (/health/datanode_)
 */
void ZKWrapper::watcher_health_child(zhandle_t *zzh, int type, int state, const char *path, void *watcherCtx) {
	LOG(INFO) << CLASS_NAME << "[health child] Watcher triggered on path '" << path;
	char health[] = "/health/datanode_";
	LOG(INFO) << CLASS_NAME << "[health child] Receive a heartbeat. A child has been added under path" << path;

	struct String_vector stvector;
	struct String_vector *vector = &stvector;
	int rc = zoo_wget_children(zzh, path, watcher_health_child, nullptr, vector);
	int i = 0;
	if (vector->count == 0){
		// client needs to pass a function ptr so they will be notified
		LOG(INFO) << CLASS_NAME << "no childs to retrieve";
	}
	while (i < vector->count) {
		LOG(INFO) <<  CLASS_NAME << "Children" << vector->data[i++];
	}
	if (vector->count) {
		deallocate_String_vector(vector);
	}
}

std::string ZKWrapper::translate_error(int errorcode) {
	std::string message;
	message = error_message.at(errorcode);
	return message;
}

ZKWrapper::ZKWrapper(std::string host, int &error_code, std::string root_path) {
	// TODO: Move these default values to some readable CONSTANT value
	zh = zookeeper_init(host.c_str(), watcher, 10000, 0, 0, 0);
	if (!zh) {
		LOG(ERROR) << CLASS_NAME <<  "zk init failed!";
		error_code = -999;
	}
	init = 1;
	if (root_path.size() != 0) {
		bool root_exists;
		if (!exists(root_path, root_exists, error_code)){
			LOG(ERROR) << CLASS_NAME <<  "Failed to check if root directory " << root << " exists " << error_code;
			init = -1;
			return;
		}
		if (!root_exists) {
			if (!recursive_create(root_path, EMPTY_VECTOR, error_code)) {
				LOG(ERROR) << CLASS_NAME <<  "Failed to create root directory " << root << " with error " << error_code;
				init = -1;
				return;
			}
		}
	}
	root = root_path;
}

std::string ZKWrapper::prepend_zk_root(const std::string& path) const {
	if (root.size() == 0) {
		return path;
	}
	if (path == "/") {
		return root;
	}
	return root + path;
}

std::string ZKWrapper::removeZKRoot(const std::string& path) const {
	return path.substr(root.size());
}

/* Wrapper Implementation of Zookeeper Functions */

bool ZKWrapper::create(const std::string &path,
		const std::vector <std::uint8_t> &data,
		int &error_code,
		bool ephemeral) const {
	if (!init) {
		LOG(ERROR) << CLASS_NAME <<  "Attempt to create before init!";
		error_code = -999;
		return false;
	}
	auto real_path = prepend_zk_root(path);
	LOG(INFO) << CLASS_NAME <<	"creating ZNode at " << real_path;
	int flag = (ephemeral) ? ZOO_EPHEMERAL : 0;
	int rc = zoo_create(zh,
			real_path.c_str(),
			reinterpret_cast<const char *>(data.data()),
			data.size(),
			&ZOO_OPEN_ACL_UNSAFE,
			flag,
			nullptr,
			0);
	error_code = rc;
	if (!rc)
		return true;
	LOG(ERROR) << CLASS_NAME <<  "Failed to create ZNode at " << real_path;
	print_error(error_code);
	return false;
}

// TODO: Modify this
bool ZKWrapper::create_sequential(const std::string &path,
		const std::vector <std::uint8_t> &data,
		std::string &new_path,
		bool ephemeral,
		int &error_code) const {

	LOG(INFO) << CLASS_NAME <<	"Starting sequential for " << path;
	if (!init) {
		LOG(ERROR) << CLASS_NAME <<  "Attempt to create sequential before init!";
		return false;
	}
	int flag = ZOO_SEQUENCE;
	if (ephemeral) {
		flag = flag | ZOO_EPHEMERAL;
	}
	LOG(INFO) << CLASS_NAME <<	"Attempting to generate new path" << new_path;
	LOG(INFO) << CLASS_NAME <<	"creating seq ZNode at " << prepend_zk_root(path);

	int len = prepend_zk_root(path).size();
	new_path.resize(MAX_PATH_LEN);
	int rc = zoo_create(zh,
			prepend_zk_root(path).c_str(),
			reinterpret_cast<const char *>(data.data()),
			data.size(),
			&ZOO_OPEN_ACL_UNSAFE,
			flag,
			reinterpret_cast<char *>(&new_path[0]),
			MAX_PATH_LEN);
	error_code = rc;
	if (rc) { // Z_OK is 0, so if we receive anything else fail
		LOG(ERROR) << CLASS_NAME <<  "Create for " << prepend_zk_root(path) << " failed " << rc;
		print_error(error_code);
		return false;
	}
	int i = 0;
	LOG(INFO) << CLASS_NAME <<	"NEW path is " << new_path;
	new_path.resize(len + NUM_SEQUENTIAL_DIGITS);
	new_path = removeZKRoot(new_path);
	LOG(INFO) << CLASS_NAME <<	"NEW path is now this" << new_path;
	return true;
}

bool ZKWrapper::recursive_create(const std::string &path,
		const std::vector <std::uint8_t> &data,
		int &error_code) const {
	for (int i=1; i<path.length(); ++i){
		if (path[i] == '/'){
			LOG(INFO) << CLASS_NAME <<	"Generating " << path.substr(0, i);
			if (!create(path.substr(0, i), ZKWrapper::EMPTY_VECTOR, error_code)){
				if (error_code != ZNODEEXISTS){
					LOG(ERROR) << CLASS_NAME <<  "Failed to recursively create " << path;
					print_error(error_code);
					return false;
				}
			}
			error_code = ZOK;
		}
	}
	LOG(INFO) << CLASS_NAME <<	"Generating " << path;
	return create(path, data, error_code);

}

bool ZKWrapper::wget(const std::string &path,
		std::vector <std::uint8_t> &data,
		watcher_fn watch,
		void *watcherCtx,
		int &error_code,
		int length) const {
	// TODO: Make this a constant value. Define a smarter retry policy for oversized data
	int len = length;
	data.resize(len);
	struct Stat stat;
	error_code = zoo_wget(zh,
			prepend_zk_root(path).c_str(),
			watch,
			watcherCtx,
			reinterpret_cast<char *>(data.data()),
			&len,
			&stat);
	if (error_code != ZOK) {
		LOG(ERROR) << CLASS_NAME <<  "wget on " << path << " failed";
		print_error(error_code);
		return false;
	}
	data.resize(len);
	return true;
}

bool ZKWrapper::get(const std::string &path,
		std::vector <std::uint8_t> &data,
		int &error_code,
		int length) const {

	// TODO: Make this a constant value. Define a smarter retry policy for oversized data
	struct Stat stat;
	int len = length;
	// TODO: Perhaps we can be smarter about this
	// LOG(INFO) << CLASS_NAME <<  "Data resizing to " << len;
	data.resize(len);
	// LOG(INFO) << CLASS_NAME <<  "Data resizing to 1;" << data.size();
	error_code = zoo_get(zh,
			prepend_zk_root(path).c_str(),
			0,
			reinterpret_cast<char *>(data.data()),
			&len,
			&stat);
	if (error_code != ZOK) {
		LOG(ERROR) << CLASS_NAME <<  "get on " << path << " failed";
		print_error(error_code);
		return false;
	}
	data.resize(len);
	return true;
}

bool ZKWrapper::set(const std::string &path,
		const std::vector <std::uint8_t> &data,
		int &error_code,
		int version) const {

	error_code = zoo_set(zh,
			prepend_zk_root(path).c_str(),
			reinterpret_cast<const char *>(data.data()),
			data.size(),
			version);
	if (error_code != ZOK) {
		LOG(ERROR) << CLASS_NAME <<  "set on " << path << " failed";
		print_error(error_code);
		return false;
	}
	return true;
}

bool ZKWrapper::exists(const std::string &path,
		bool &exist,
		int &error_code) const {
	// TODO: for now watch argument is set to 0, need more error checking
	int rc = zoo_exists(zh, prepend_zk_root(path).c_str(), 0, 0);
	error_code = rc;
	if (rc == ZOK) {
		exist = true;
		return true;
	} else if (rc == ZNONODE) {
		exist = false;
		return true;
	} else {
		// NOTE: value exist is not updated in this case
		LOG(ERROR) << CLASS_NAME <<  "exists on " << path << " failed";
		print_error(error_code);
		return false;
	}
}

bool ZKWrapper::wexists(const std::string &path,
		bool &exist,
		watcher_fn watch,
		void *watcherCtx,
		int &error_code) const {
	struct Stat stat;
	int rc = zoo_wexists(zh, prepend_zk_root(path).c_str(), watch, watcherCtx, &stat);
	error_code = rc;
	if (rc == ZOK) {
		exist = true;
		return true;
	} else if (rc == ZNONODE) {
		exist = false;
		return true;
	} else {
		// NOTE: value exist is not updated in this case
		LOG(ERROR) << CLASS_NAME <<  "wexists on " << path << " failed";
		print_error(error_code);
		return false;
	}
}

bool ZKWrapper::delete_node(const std::string &path, int &error_code) const {
	// NOTE: use -1 for version, check will not take place.
	error_code = zoo_delete(zh, prepend_zk_root(path).c_str(), -1);
	if (error_code != ZOK) {
		LOG(ERROR) << CLASS_NAME <<  "delete on " << path << " failed";
		print_error(error_code);
		return false;
	}
	return true;
}

// TODO: Modify
bool ZKWrapper::recursive_delete(const std::string &path, int &error_code) const {
	LOG(INFO) << CLASS_NAME <<	"Recursively deleting " << path;
	bool root = ("/" == path);
	bool endsSlash = path[path.size() - 1] == '/';
	int rc = 0;

	std::string znodePath = endsSlash ? path.substr(0, path.size() - 1) : path;
	std::vector <std::string> children;
	if (!get_children(root ? "/" : znodePath, children, rc)){
		LOG(ERROR) << CLASS_NAME <<  "recursive_delete on " << path << " failed: couldn't get children";
		return false;
	}

	for (auto child : children) {
		LOG(INFO) << CLASS_NAME <<	"child is " << child;
		if (child.size() == 0){
			continue;
		}
		std::string newPath = znodePath + "/" + child;
		int result = recursive_delete(newPath, error_code);
		rc = (result != 0) ? result : rc;
	}

	int result = delete_node(path, error_code);
	rc = (result != 0) ? result : rc;

	if (error_code != ZOK) {
		LOG(ERROR) << CLASS_NAME <<  "recursive_delete on " << path << " failed.";
		return false;
	}
	return true;
}

bool ZKWrapper::get_children(const std::string &path,
		std::vector <std::string> &children,
		int &error_code) const {

	struct String_vector stvector;
	struct String_vector *vector = &stvector;
	error_code = zoo_get_children(zh, prepend_zk_root(path).c_str(), 0, vector);
	if (error_code != ZOK) {
		LOG(ERROR) << CLASS_NAME <<  "get_children on " << path << " failed";
		print_error(error_code);
		return false;
	}
	int i;
	for (i = 0; i < stvector.count; i++) {
		children.push_back(stvector.data[i]);
	}
	return true;
}

bool ZKWrapper::wget_children(const std::string &path,
		std::vector <std::string> &children,
		watcher_fn watch,
		void *watcherCtx,
		int &error_code) const {

	struct String_vector stvector;
	struct String_vector *vector = &stvector;
	error_code = zoo_wget_children(zh, prepend_zk_root(path).c_str(), watch, watcherCtx, vector);
	if (error_code != ZOK) {
		LOG(ERROR) << CLASS_NAME <<  "wget_children on " << path << " failed";
		print_error(error_code);
		return false;
	}

	int i;
	for (i = 0; i < stvector.count; i++) {
		children.push_back(stvector.data[i]);
	}
	return true;
}

/* Multi-Operations */

std::shared_ptr <ZooOp> ZKWrapper::build_create_op(const std::string &path,
		const std::vector <std::uint8_t> &data,
		const int flags) const {
	auto op = std::make_shared<ZooOp>(prepend_zk_root(path), data);
	zoo_create_op_init(op->op,
			op->path,
			op->data,
			op->num_bytes,
			&ZOO_OPEN_ACL_UNSAFE,
			flags,
			nullptr,
			0);
	return op;
}

std::shared_ptr <ZooOp> ZKWrapper::build_delete_op(const std::string &path,
		int version) const {
	auto op = std::make_shared<ZooOp>(prepend_zk_root(path), ZKWrapper::EMPTY_VECTOR);
	zoo_delete_op_init(op->op, op->path, version);
	return op;
}

std::shared_ptr <ZooOp> ZKWrapper::build_set_op(const std::string &path,
		const std::vector <std::uint8_t> &data,
		int version) const {
	auto op = std::make_shared<ZooOp>(prepend_zk_root(path), data);
	zoo_set_op_init(op->op,
			op->path,
			op->data,
			op->num_bytes,
			version,
			nullptr);
	return op;
}

bool ZKWrapper::execute_multi(const std::vector <std::shared_ptr<ZooOp>> ops,
		std::vector <zoo_op_result> &results, int &error_code) const {
	std::vector <zoo_op_t> trueOps = std::vector<zoo_op_t>();
	results.resize(ops.size());
	for (auto op : ops) {
		trueOps.push_back(*(op->op));
	}
	error_code = zoo_multi(zh, ops.size(), &trueOps[0], &results[0]);
	if (error_code != ZOK){
		LOG(ERROR) << CLASS_NAME <<  "multiop failed";
		print_error(error_code);
		return false;
	}
	return true;
}

std::vector <uint8_t> ZKWrapper::get_byte_vector(const std::string &string) {
	std::vector <uint8_t> vec(string.begin(), string.end());
	return vec;
}

void ZKWrapper::close() {
	zookeeper_close(zh);
}