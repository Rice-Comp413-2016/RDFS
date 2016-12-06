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
	std::string ip_port_pairs("");

	if (argc == 5 || argc == 4) {
		if (argc == 5) {
			port = std::atoi(argv[4]);
		}
		std::string comma(",");
		std::string colon(":");
		std::string pstr(std::to_string(zk_port));
		ip_port_pairs += argv[1] + colon + pstr + comma + argv[2] + colon + pstr + comma + argv[3] + colon + pstr;
		LOG(INFO) << "IP and port pairs are " << ip_port_pairs;
	} else {
		/* bad args */
		LOG(INFO) << "Bad arguments supplied, exiting";
		return 1;
	}

	zkclient::ZkNnClient nncli(ip_port_pairs);
	nncli.register_watches();
	std::cout << "Namenode is starting" << std::endl;
	ClientNamenodeTranslator translator(port, nncli);
	translator.getRPCServer().serve(io_service);
}
