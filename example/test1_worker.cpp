/*
 * test1_woker.cpp
 *
 *  Created on: 2018��4��12��
 *      Author: qiyingwang
 */
#include "worker.hpp"
#include "hello.pb.shm.hpp"
#include "shm_fifo.hpp"
#include <string.h>
#include <iostream>
using namespace shm_multiproc;

DEFINE_ENTRY(test, writer, type, data)
{
    if (0 != strcmp(type, helloworld::ShmHelloTestData::GetTypeName()))
    {
        return -1;
    }
    const helloworld::ShmHelloTestData* t = (const helloworld::ShmHelloTestData*) data;
    //std::cout << "Recv From Master:" << *t << std::endl;
    std::cout << "##Writer shm used:" << writer.GetMMData().GetAllocator().used_space() << std::endl;

    helloworld::ShmHelloTestData* item = writer.New<helloworld::ShmHelloTestData>();
    item->sv.assign("FromWorker");
    writer.Offer(item);
    return 0;
}

