#include <iostream>
#include <unordered_map>

#pragma once

namespace config_reader {

/**
 * This is for reading config files. 
 * The methods will need to be more specified to pick which config
 * file you are reading from. Right now it only reads in the HDFS
 * default xml file.
 *
 * If you want to add files, then just add a function in the constructor
 * which parses the file. This assumes all key names are unique across ALL
 * files
 */
class ConfigReader {
	public:
		ConfigReader();
	
		std::string getString(std::string key); 	
		int getInt(std::string key);
		bool getBool(std::string key);

	private:
		static const char* HDFS_DEFAULTS_CONFIG;		
 		std::unordered_map<std::string, std::string> conf_strings;
		std::unordered_map<std::string, int> conf_ints;
		std::unordered_map<std::string, bool> conf_bools;

		void InitHDFSDefaults();

		static const std::string CLASS_NAME;
};
}	
