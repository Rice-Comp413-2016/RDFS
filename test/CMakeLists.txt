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

add_executable(RunAllTests testAll.cc)
target_link_libraries(RunAllTests ${GTEST_LIBRARIES} pthread)
target_link_libraries(RunAllTests ${GTEST})
target_link_libraries(RunAllTests zk_client_dn)
target_link_libraries(RunAllTests zk_client_common)

add_executable(testZKDNCli zookeeper/zk_dn_client_test.cc)
target_link_libraries(testZKDNCli ${GTEST_LIBRARIES} pthread)
target_link_libraries(testZKDNCli ${GTEST})
target_link_libraries(testZKDNCli zk_client_dn)
target_link_libraries(testZKDNCli zk_client_common)

