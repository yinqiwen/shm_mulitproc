/*
 * test1.cpp
 *
 *  Created on: 2018Äê4ÔÂ12ÈÕ
 *      Author: qiyingwang
 */
#include "multiproc.hpp"
#include "hello.pb.h"
#include "hello.pb.shm.hpp"

using namespace shm_multiproc;

static void start_master(int argc, const char** argv)
{
    MultiProcOptions options;
    options.home = "/home/wqy/share/shm_mulitproc";
    options.worker_home = "/home/wqy/share/shm_mulitproc";
    options.max_waitms = 1000;

    WorkerOptions worker;
    worker.name = "test_worker";
    worker.count = 2;
    worker.shm_fifo_maxsize = 20;
    worker.so_script.compiler_flag = "-std=c++11";
    worker.so_script.incs.push_back("./");
    worker.so_script.incs.push_back("../mmdata/src");
    worker.so_script.incs.push_back("../shm_proto");
    worker.so_script.path = "./example/test1_worker.cpp";

    options.workers.push_back(worker);
    Master master;
    master.Start(argc, argv, options);

    auto consume = [](const char* type, const void* data)
    {
        const helloworld::ShmHelloTestData* t = (const helloworld::ShmHelloTestData*) data;
        std::cout << "Recv From Worker:" << *t << std::endl;
        return 0;
    };

    int64_t cursor = 0;
    while (1)
    {
        helloworld::HelloTestData* dv = new helloworld::HelloTestData;
        dv->add_k(cursor);
        dv->add_k(cursor + 1);
        std::stringstream ss;
        ss << "hello,world!" << "_" << cursor;
        dv->set_sv(ss.str());
        dv->mutable_tk()->set_ruleid(100 + cursor);
        dv->mutable_tk()->set_testid(101 + cursor);
        dv->add_items()->set_ruleid(1000 + cursor);
        dv->add_items()->set_testid(1001 + cursor);
        (*dv->mutable_tm())[cursor] = "hello";
        (*dv->mutable_tm())[cursor + 1] = "world";
        cursor++;
        std::vector<WorkerId> workers;
        for (int i = 0; i < worker.count; i++)
        {
            WorkerId id;
            id.name = worker.name;
            id.idx = i;
            workers.push_back(id);
        }
        std::cout << "##Main writer shm used:" << master.GetMainShm().GetAllocator().used_space() << std::endl;
        master.WriteToWorkers(workers, dv);
        master.Routine(consume);
    }
}

static void start_worker(int argc, const char** argv)
{
    Worker worker;
    worker.Start(argc, argv);
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

