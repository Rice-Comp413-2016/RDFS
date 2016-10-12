#ifndef RDFS_ZKCLIENTCOMMON_H
#define RDFS_ZKCLIENTCOMMON_H

#include "zkwrapper.h"

#include <boost/shared_ptr.hpp>

class ZkClientCommon {
public:
    ZkClientCommon();
    void init();

private:
    std::shared_ptr<ZKWrapper> zk;
};

#endif //RDFS_ZKCLIENTCOMMON_H
