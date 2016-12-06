#!/bin/bash
# Provisioning the EC2 instance for running RDFS

cd /home/ubuntu

mkdir /home/ubuntu/env

set -e
set -x

sudo apt-get update
# clean out redundant packages from vagrant base image
sudo apt-get autoremove -y

# Install some basics
sudo apt-get install -y language-pack-en zip unzip curl
sudo apt-get install -y git build-essential cmake automake autoconf libtool libboost-all-dev libasio-dev

# Install and setup dependencies of ZK (i.e. Java)
sudo apt-get install -y ssh pdsh openjdk-8-jdk-headless
sudo echo 'export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64/jre' >> ~/.bashrc

sudo wget --quiet https://github.com/google/protobuf/releases/download/v3.0.0/protobuf-cpp-3.0.0.tar.gz
sudo tar -xf protobuf-cpp-3.0.0.tar.gz
sudo rm protobuf-cpp-3.0.0.tar.gz
cd protobuf-3.0.0; sudo ./autogen.sh && sudo ./configure --prefix=/usr && sudo make && sudo make install
cd /home/ubuntu/env/; sudo ldconfig

# Setup Apache zookeeper
sudo wget --quiet http://mirror.olnevhost.net/pub/apache/zookeeper/zookeeper-3.4.9/zookeeper-3.4.9.tar.gz
sudo tar -xf zookeeper-3.4.9.tar.gz
sudo mv zookeeper-3.4.9 /home/ubuntu/env/zookeeper
sudo rm zookeeper-3.4.9.tar.gz 

sudo chown -R :ubuntu /home/ubuntu/env/zookeeper
sudo chmod 775 /home/ubuntu/env
sudo chmod 775 /home/ubuntu/env/zookeeper/
sudo chmod 775 /home/ubuntu/env/zookeeper/conf

sudo cat > /home/ubuntu/env/zookeeper/conf/zoo.cfg <<EOF
tickTime=2000
dataDir=/var/zookeeper
clientPort=2181
initLimit=5
syncLimit=2
EOF

# Set up the ZooKeeper client libraries
sudo apt-get --assume-yes install ant
cd /home/ubuntu/env/zookeeper
sudo ant compile_jute
cd /home/ubuntu/env/zookeeper/src/c
sudo apt-get --assume-yes install autoconf
sudo apt-get --assume-yes install libcppunit-dev
sudo apt-get --assume-yes install libtool
sudo autoreconf -if
sudo ./configure
sudo make && sudo make install

# Add Google Mock
sudo apt-get install -y google-mock
cd /usr/src/gmock
sudo cmake CMakeLists.txt
sudo make
sudo cp *.a /usr/lib

# Add Google Test
sudo apt-get install -y libgtest-dev
cd /usr/src/gtest
sudo cmake CMakeLists.txt
sudo make
sudo cp *.a /usr/lib
cd /home/ubuntu/env

# Pull down the code and build it 
mkdir rdfs
cd rdfs
sudo git clone https://github.com/Rice-Comp413-2016/RDFS.git .
sudo chown -R :ubuntu /home/ubuntu/env/rdfs
mkdir build
cd build
sudo cmake ..
sudo make
sudo rice-namenode/namenode

# Allow us to write to /dev/sdb.
sudo chown :ubuntu /dev/sdb

# Signify that configuration is done
cd /home/ubuntu/env
sudo touch CONFIGURATION_DONE
