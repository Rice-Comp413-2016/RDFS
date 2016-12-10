#define ELPP_FRESH_LOG_FILE
#define ELPP_THREAD_SAFE

#include <cstdlib>
#include <thread>
#include <iostream>
#include <asio.hpp>
#include <rpcserver.h>
#include <easylogging++.h>
#include "ClientDatanodeProtocolImpl.h"
#include "data_transfer_server.h"
#include "native_filesystem.h"
#include "zk_dn_client.h"
#include "DaemonFactory.h"

// initialize the logging library (only do this once!)
INITIALIZE_EASYLOGGINGPP

#define LOG_CONFIG_FILE "dn-log-conf.conf"

using namespace client_datanode_translator;

int main(int argc, char* argv[]) {
	el::Configurations conf(LOG_CONFIG_FILE);
	el::Loggers::reconfigureAllLoggers(conf);

	int error_code = 0;

	asio::io_service io_service;
	unsigned short xferPort = 50010;
	unsigned short ipcPort = 50020;
	std::string backingStore("/dev/sdb");

	// Must pass the datanode private IP as first arg

	if (argc < 2) {
		LOG(INFO) << "Make sure to pass datanode private IP as first arg. Exiting";
		return 1;
	}

	// Hard coded ip:port pairs for zk quorum
	std::string ip_port_pairs("34.194.46.46:2181,34.194.83.197:2181,34.194.27.197:2181");

	if (argc >= 3) {
		xferPort = std::atoi(argv[2]);
	}
	if (argc >= 4) {
		ipcPort = std::atoi(argv[3]);
	}
	if (argc >= 5) {
		backingStore = argv[4];
	}
	LOG(INFO) << "my backingstore is " << backingStore;
	auto fs = std::make_shared<nativefs::NativeFS>(backingStore);
    if (fs == nullptr){
        LOG(INFO) << "Failed to create filesystem!";
        return -1;
    }
	uint64_t total_disk_space = fs->getTotalSpace();

	auto dncli = std::make_shared<zkclient::ZkClientDn>(argv[1], ip_port_pairs, total_disk_space, ipcPort, xferPort);
	ClientDatanodeTranslator translator(ipcPort);
	auto transfer_server = std::make_shared<TransferServer>(xferPort, fs, dncli);
    dncli->setTransferServer(transfer_server);
	daemon_thread::DaemonThreadFactory factory;
	factory.create_daemon_thread(&TransferServer::sendStats, transfer_server.get(), 3);
	factory.create_daemon_thread(&TransferServer::poll_replicate, transfer_server.get(), 2);
	factory.create_daemon_thread(&TransferServer::poll_delete, transfer_server.get(), 5);
	std::thread(&TransferServer::serve, transfer_server.get(), std::ref(io_service)).detach();
	translator.getRPCServer().serve(io_service);
}
