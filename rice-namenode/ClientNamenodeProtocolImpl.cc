#include <iostream>
#include <string>
#include <google/protobuf/arena.h>
#include <google/protobuf/arenastring.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/metadata.h>
#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/generated_enum_reflection.h>
#include <google/protobuf/unknown_field_set.h>

#include <easylogging++.h>
#include <rpcserver.h>
#include <pugixml.hpp>

#include "ClientNamenodeProtocolImpl.h"

/**
 * The implementation of the rpc calls. 
 */
namespace client_namenode_translator {

// the .proto file implementation's namespace, used for messages
using namespace hadoop::hdfs;

// static string info
const char* ClientNamenodeTranslator::HDFS_DEFAULTS_CONFIG = "hdfs-default.xml";

// config
std::map <std::string, std::string> config;

// TODO - this will probably take some zookeeper object
ClientNamenodeTranslator::ClientNamenodeTranslator(int port_arg)
	: port(port_arg), server(port) {
	InitServer();
	Config();
	LOG(INFO) << "Created client namenode translator.";
}

std::string ClientNamenodeTranslator::getFileInfo(std::string input) {
	GetFileInfoRequestProto req;
	req.ParseFromString(input);
	logMessage(req);
	const std::string& src = req.src();
	// from here, we would ask zoo-keeper something, we should check
	// the response, and either return the response or return some 
	// void response...for now we will just return			
	std::string out; 
	GetFileInfoResponseProto res;
	return Serialize(&out, res);
}

std::string ClientNamenodeTranslator::mkdir(std::string input) {
	MkdirsRequestProto req;
	req.ParseFromString(input);
	logMessage(req);
	const std::string& src = req.src();
	const hadoop::hdfs::FsPermissionProto& permission_msg = req.masked();
	bool create_parent = req.createparent();
	std::string out;
	MkdirsResponseProto res;
	// TODO for now, just say the mkdir command failed
	res.set_result(false);
	return Serialize(&out, res);
}

std::string ClientNamenodeTranslator::append(std::string input) {
	AppendRequestProto req;
	req.ParseFromString(input);
    logMessage(req);
	const std::string& src = req.src();
	const std::string& clientName = req.clientname();
	std::string out;
	AppendResponseProto res;
	// TODO We don't support this operation, so we need to return some
	// kind of failure status. I've looked around and I'm not sure 
	// how to do this since this message only contains an optional
	// LocatedBlockProto. No LocatedBlockProto might be failure
	return Serialize(&out, res);
}

std::string ClientNamenodeTranslator::destroy(std::string input) {
	DeleteRequestProto req;
	req.ParseFromString(input);
	logMessage(req);
	const std::string& src = req.src();
	const bool recursive = req.recursive();
	std::string out;
	DeleteResponseProto res;
	// TODO for now, just say the delete command failed
	res.set_result(false);
	return Serialize(&out, res);
}

std::string ClientNamenodeTranslator::create(std::string input) {
	CreateRequestProto req;
	req.ParseFromString(input);
	logMessage(req);
	const std::string& src = req.src();
	const hadoop::hdfs::FsPermissionProto& masked = req.masked();
	std::string out;
	CreateResponseProto res;
	// TODO for now, just say the create command failed. Not entirely sure
	// how to do that, but I think you just don't include an
	// HDFSFileStatusProto
	return Serialize(&out, res);
}


std::string ClientNamenodeTranslator::getBlockLocations(std::string input) {
	GetBlockLocationsRequestProto req;
	req.ParseFromString(input);
	logMessage(req);
	const std::string& src = req.src();
	google::protobuf::uint64 offset = req.offset();
	google::protobuf::uint64 length = req.offset();
	std::string out;
	GetBlockLocationsResponseProto res;
	// TODO for now, just say the getBlockLocations command failed. Not entirely sure
	// how to do that, but I think you just don't include a
	// LocatedBlocksProto
	return Serialize(&out, res);
}

std::string ClientNamenodeTranslator::getServerDefaults(std::string input) {
	GetServerDefaultsRequestProto req;
	req.ParseFromString(input);
	logMessage(req);
	std::string out;
	GetServerDefaultsResponseProto res;
	return Serialize(&out, res);
}

/**
 * Serialize the message 'res' into out. If the serialization fails, then we must find out to handle it
 * If it succeeds, we simly return the serialized string. 
 */
std::string ClientNamenodeTranslator::Serialize(std::string* out, google::protobuf::Message& res) {
	if (!res.SerializeToString(out)) {
		// TODO handle error
	}
	return *out;
}

/**
 * Set the configuration info for the namenode
 */
void ClientNamenodeTranslator::Config() {
	// Read the hdfs-defaults xml file 
	{
		using namespace pugi;
		xml_document doc;
		xml_parse_result result = doc.load_file(HDFS_DEFAULTS_CONFIG);
		if (!result) {
		    LOG(ERROR) << "XML [" << HDFS_DEFAULTS_CONFIG << "] parsed with errors, attr value: [" << 
		    	doc.child("node").attribute("attr").value() << "]\n";
    		LOG(ERROR) << "Error description: " << result.description() << "\n";
    		return;
		}
			
		xml_node properties = doc.child("configuration");
		for (xml_node child : properties.children()) {
			// the name and value nodes in the xml 
			xml_node name = child.first_child();
			xml_node value = name.next_sibling();	
			const char* name_str = name.first_child().text().get();
			const char* val_str = value.first_child().text().get();
			bool is_nn_config = strstr(name_str, "namenode");
			if (is_nn_config) {
				config[name_str] = val_str;
				LOG(INFO) << name_str << " : " << config[name_str];
			}
		}
		LOG(INFO) << "Configured namenode (but not really!)";
	}

	// TODO any other configs that we need to read? 	
}

/**
 * Initialize the rpc server
 */
void ClientNamenodeTranslator::InitServer() {
	LOG(INFO) << "Initializing namenode server...";
	RegisterClientRPCHandlers();
}

/**
 * Register our rpc handlers with the server
 */
void ClientNamenodeTranslator::RegisterClientRPCHandlers() {
    using namespace std::placeholders; // for `_1`

	// The reason for these binds is because it wants static functions, but we want to give it member functions
    // http://stackoverflow.com/questions/14189440/c-class-member-callback-simple-examples

	server.register_handler("getFileInfo", std::bind(&ClientNamenodeTranslator::getFileInfo, this, _1));
	server.register_handler("mkdir", std::bind(&ClientNamenodeTranslator::mkdir, this, _1));
	server.register_handler("append", std::bind(&ClientNamenodeTranslator::append, this, _1));
	server.register_handler("destroy", std::bind(&ClientNamenodeTranslator::destroy, this, _1));
	server.register_handler("create", std::bind(&ClientNamenodeTranslator::create, this, _1));
	server.register_handler("getBlockLocations", std::bind(&ClientNamenodeTranslator::getBlockLocations, this, _1));
}

/**
 * Get the RPCServer this namenode uses to connect with clients
 */ 
RPCServer ClientNamenodeTranslator::getRPCServer() {
	return server; 
} 

/**
 * Get the port this namenode listens on
 */
int ClientNamenodeTranslator::getPort() {
	return port;
}

void ClientNamenodeTranslator::logMessage(google::protobuf::Message& req) {
    LOG(INFO) << "Got mkdir request with input " << req.DebugString();
}

} //namespace
