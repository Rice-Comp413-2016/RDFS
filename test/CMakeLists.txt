cmake_minimum_required(VERSION 2.8)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Locate GTest
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# Locate GMock
include_directories(${GMOCK_INCLUDE_DIRS})

set(GTEST "/usr/src/gtest/libgtest.a" "/usr/src/gmock/libgmock.a")

include_directories(/usr/local/include/zookeeper)
include_directories(../zkwrapper/include)
include_directories(../zookeeper/include)
include_directories(../utility/config-reader)
include_directories(../utility/logging)
include_directories(../utility/include)
include_directories(../rice-namenode/include)
include_directories(../rice-datanode/include)
include_directories(../native-filesystem/include)
include_directories(../rpcserver/include)

link_directories(/home/vagrant/rdfs/build/zkwrapper)

add_executable(RunAllTests run-all/run-all-tests.cc)

add_executable(NameNodeTest namenode/NameNodeTest.cc)
target_link_libraries(NameNodeTest namenode_protos)
target_link_libraries(NameNodeTest zk_client_nn zk_client_dn)
target_link_libraries(NameNodeTest zkwrapper /usr/local/lib/libzookeeper_mt.so)
target_link_libraries(NameNodeTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(NameNodeTest ${GTEST})
target_link_libraries(NameNodeTest protobuf)

add_executable(ReadWriteTest integration/ReadWriteTest.cc)
target_link_libraries(ReadWriteTest native-fs)
target_link_libraries(ReadWriteTest zk_client_nn zk_client_dn rpcserver)
target_link_libraries(ReadWriteTest data_transfer_server)
target_link_libraries(ReadWriteTest namenode_protos)
target_link_libraries(ReadWriteTest zkwrapper /usr/local/lib/libzookeeper_mt.so)
target_link_libraries(ReadWriteTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(ReadWriteTest ${GTEST})

add_executable(ReplicationTest integration/ReplicationTest.cc)
target_link_libraries(ReplicationTest native-fs)
target_link_libraries(ReplicationTest zk_client_nn zk_client_dn rpcserver)
target_link_libraries(ReplicationTest data_transfer_server)
target_link_libraries(ReplicationTest namenode_protos)
target_link_libraries(ReplicationTest zkwrapper /usr/local/lib/libzookeeper_mt.so)
target_link_libraries(ReplicationTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(ReplicationTest ${GTEST})

add_executable(DeleteTest integration/DeleteTest.cc)
target_link_libraries(DeleteTest zk_client_nn rpcserver)
target_link_libraries(DeleteTest namenode_protos)
target_link_libraries(DeleteTest zkwrapper /usr/local/lib/libzookeeper_mt.so)
target_link_libraries(DeleteTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(DeleteTest ${GTEST})

add_executable(ZKWrapperTest zkwrapper/ZKWrapperTest.cc)
target_link_libraries(ZKWrapperTest zkwrapper /usr/local/lib/libzookeeper_mt.so)
target_link_libraries(ZKWrapperTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(ZKWrapperTest ${GTEST})

add_executable(ZKLockTest zkwrapper/ZKLockTest.cc)
target_link_libraries(ZKLockTest zk_lock zkwrapper /usr/local/lib/libzookeeper_mt.so)
target_link_libraries(ZKLockTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(ZKLockTest ${GTEST})

add_executable(NativeFsTest native-filesystem/native_fs_tests.cc)
target_link_libraries(NativeFsTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(NativeFsTest ${GTEST})
target_link_libraries(NativeFsTest native-fs)

add_executable(ZKDNClientTest zookeeper/zk_dn_client_test.cc)
target_link_libraries(ZKDNClientTest ${GTEST_LIBRARIES} pthread)
target_link_libraries(ZKDNClientTest ${GTEST})
target_link_libraries(ZKDNClientTest zk_client_dn)
target_link_libraries(ZKDNClientTest zk_client_common)
target_link_libraries(ZKDNClientTest data_transfer_server)
target_link_libraries(ZKDNClientTest namenode_protos)
target_link_libraries(ZKDNClientTest native-fs)

