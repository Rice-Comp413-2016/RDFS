#!/bin/sh
# Provisioning the vagrant box for development on RDFS.

set -e
set -x

apt-get update
# clean out redundant packages from vagrant base image
apt-get autoremove -y

# Install some basics
apt-get install -y language-pack-en zip unzip curl

apt-get install -y git build-essential cmake automake autoconf libtool

wget --quiet https://github.com/google/protobuf/releases/download/v3.0.0/protobuf-cpp-3.0.0.tar.gz
tar -xf protobuf-cpp-3.0.0.tar.gz
rm protobuf-cpp-3.0.0.tar.gz
cd protobuf-3.0.0; ./autogen.sh && ./configure --prefix=/usr && make && make check && make install
cd /home/vagrant/; ldconfig

# Install and setup dependencies of hadoop
apt-get install -y ssh pdsh openjdk-8-jdk-headless
# passphraseless ssh
ssh-keygen -b 2048 -t rsa -f /home/vagrant/.ssh/id_rsa -N ""
cp /home/vagrant/.ssh/id_rsa.pub /home/vagrant/.ssh/authorized_keys

# Setup Apache hadoop for pseudo-distributed usage
wget --quiet http://mirror.olnevhost.net/pub/apache/hadoop/common/hadoop-2.7.3/hadoop-2.7.3.tar.gz
tar -xf hadoop-2.7.3.tar.gz
mv hadoop-2.7.3 /home/vagrant/hadoop
rm hadoop-2.7.3.tar.gz
echo 'export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64/jre' >> /home/vagrant/.bashrc
echo 'export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64/jre' >> /home/vagrant/hadoop/etc/hadoop/hadoop-env.sh
cat > /home/vagrant/hadoop/etc/core-site.xml <<EOF
<configuration>
    <property>
        <name>fs.defaultFS</name>
        <value>hdfs://localhost:9000</value>
    </property>
</configuration>
EOF

cat > /home/vagrant/hadoop/etc/hdfs-site.xml <<EOF
<configuration>
    <property>
        <name>dfs.replication</name>
        <value>1</value>
        <name>dfs.name.dir</name>
        <value>/home/vagrant/hadoop/cache/dfs/name</value>
    </property>
</configuration>
EOF
# add hadoop to path
echo 'export PATH=/home/vagrant/hadoop/bin:$PATH' >> /home/vagrant/.bashrc

# TODO: Setup Apache zookeeper


# Add Google Test
apt-get install libgtest-dev
cd /usr/src/gtest
cmake CMakeLists.txt
make
cp *.a /usr/lib


# Put everything under /home/vagrant and /home/vagrant/.ssh.
chown -R vagrant:vagrant /home/vagrant/*
chown -R vagrant:vagrant /home/vagrant/.ssh/*
