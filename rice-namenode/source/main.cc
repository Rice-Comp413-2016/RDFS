#define ELPP_FRESH_LOG_FILE
#define ELPP_THREAD_SAFE

#include <cstdlib>
#include <iostream>
#include <asio.hpp>
#include <rpcserver.h>
#include <easylogging++.h>
#include "zk_nn_client.h"
#include "ClientNamenodeProtocolImpl.h"

INITIALIZE_EASYLOGGINGPP

#define LOG_CONFIG_FILE "nn-log-conf.conf"

using namespace client_namenode_translator;

int main(int argc, char* argv[]) {
	el::Configurations conf(LOG_CONFIG_FILE);
	el::Loggers::reconfigureAllLoggers(conf);
	el::Loggers::addFlag(el::LoggingFlag::LogDetailedCrashReason);

	asio::io_service io_service;

	// usage: namenode ip ip ip [port], optional
	short port = 5351;
	short zk_port = 2181;
	// Hard coded ip:port pairs for the zk quorum 
	std::string ip_port_pairs("34.194.46.46:2181,34.194.83.197:2181,34.194.27.197:2181");

	if (argc >= 2) {
		port = std::atoi(argv[1]);
	}

	zkclient::ZkNnClient nncli(ip_port_pairs);
	nncli.register_watches();
	std::cout << "Namenode is starting" << std::endl;
	ClientNamenodeTranslator translator(port, nncli);
	translator.getRPCServer().serve(io_service);
}
