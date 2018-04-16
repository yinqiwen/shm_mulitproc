# shm_mulitproc

A C++ multi-processing framework lib.

## Features
- Master-Worker Pattern with multiple woker process   
- Super fast IPC with zero memory copy & zero protocol decode/encode

## Dependency

- [mmdata](https://github.com/yinqiwen/mmdata)
- [shm_proto](https://github.com/yinqiwen/shm_proto)


## Example
The master example code to start the processes.

```cpp
#include "multiproc.hpp"
#include "hello.pb.h"
#include "hello.pb.shm.hpp"

using namespace shm_multiproc;

static void start_master(int argc, const char** argv)
{
    MultiProcOptions options;
    options.home = "./";
    options.worker_home = "./";
    options.max_waitms = 1500;

    WorkerOptions worker;
    worker.name = "test_worker";
    worker.count = 2;
    worker.shm_fifo_maxsize = 10000;
    worker.so_home = "./";

    options.workers.push_back(worker);
    Master master;
    if(0 != master.Start(argc, argv, options))
    {
    	 printf("Master start error:%s\n", master.LastError().c_str());
    	 return;
    }


    auto consume = [](const char* type, const void* data)
    {
        const helloworld::ShmHelloTestData* t = (const helloworld::ShmHelloTestData*) data;
        std::cout << "Recv From Worker:" << *t << std::endl;
        return 0;
    };

    while (1)
    {
        //construct protobuf message
        helloworld::HelloTestData* dv = new helloworld::HelloTestData;
        //.....
        
        std::vector<WorkerId> workers;
        for (int i = 0; i < worker.count; i++)
        {
            WorkerId id;
            id.name = worker.name;
            id.idx = i;
            workers.push_back(id);
        }
        //std::cout << "##Main writer shm used:" << master.GetMainShm().GetAllocator().used_space() << std::endl;
        master.WriteToWorkers(workers, dv);
        master.Routine(consume);
    }
}

static void start_worker(int argc, const char** argv)
{
    Worker worker;
    if(0 != worker.Start(argc, argv))
    {
    	printf("Worker start error:%s\n", worker.LastError().c_str());
    	return;
    }
    while (1)
    {
        worker.Routine(5);
    }
}

int main(int argc, const char** argv)
{
    if (argc == 1)
    {
        start_master(argc, argv);
    }
    else
    {
        start_worker(argc, argv);
    }
    return -1;
}
```

The worker example code to consume&produce messages.

```cpp
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
    //std::cout << "##Writer shm used:" << writer.GetMMData().GetAllocator().used_space() << std::endl;

    helloworld::ShmHelloTestData* item = writer.New<helloworld::ShmHelloTestData>();
    item->sv.assign("FromWorker");
    writer.Offer(item);
    return 0;
}
```

