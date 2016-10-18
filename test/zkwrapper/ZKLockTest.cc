//
// Created by Nicholas Kwon on 10/16/16.
//

#include <gtest/gtest.h>
#include <stdlib.h>
#include <future>
#include "zk_lock.h"

namespace {

    const std::string zk_dir = "/a";
    class ZKLockTest : public ::testing::Test {
    protected:
        virtual void SetUp() {
            // Code here will be called immediately after the constructor (right
            // before each test).
            system("sudo ~/zookeeper/bin/zkServer.sh start");
            zkWrapper = new ZKWrapper("localhost:2181");
        }

        virtual void TearDown() {
            // Code here will be called immediately after each test (right
            // before the destructor).
            std::string command("sudo ~/zookeeper/bin/zkCli.sh rmr /_locknode_");
            system(command.data());
            system("sudo ~/zookeeper/bin/zkServer.sh stop");
        }

        static void lock_and_write(ZKWrapper &zkWrapper, std::condition_variable &cv, int &x) {
            ZKLock lock1(zkWrapper, zk_dir);
            lock1.lock();
            cv.notify_one();
            x = 5;
            lock1.unlock();
        }

        static void lock_and_read(ZKWrapper &zkWrapper,std::condition_variable &cv, int &x, std::promise<int> &p) {
            ZKLock lock2(zkWrapper, zk_dir);
            std::mutex mtx;
            std::unique_lock<std::mutex> lck(mtx);
            cv.wait(lck);
            lock2.lock();
            p.set_value(x);
            lock2.unlock();
        }

        // Objects declared here can be used by all tests in the test case for Foo.
        ZKWrapper *zkWrapper;
    };



    TEST_F(ZKLockTest, AtomicInteger) {
        std::condition_variable cv;
        std::promise<int> promise;
        auto future = promise.get_future();
        int x = 0;
        std::thread thread1(lock_and_write, std::ref(*zkWrapper), std::ref(cv), std::ref(x));
        std::thread thread2(lock_and_read, std::ref(*zkWrapper), std::ref(cv), std::ref(x), std::ref(promise));
        thread1.join();
        thread2.join();

        ASSERT_EQ(5, future.get());
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}