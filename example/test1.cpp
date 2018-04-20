/*
 *Copyright (c) 2018-2018, yinqiwen <yinqiwen@gmail.com>
 *All rights reserved.
 *
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Redis nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 *THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 *BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "multiproc.hpp"
#include "hello.pb.h"
#include "hello.pb.shm.hpp"

using namespace shm_multiproc;

static void start_master(int argc, const char** argv)
{
    MultiProcOptions options;
    options.home = "./";
    options.worker_home = "./";
    options.max_waitms = 1000;

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
        auto write_callback = [](const WorkerId& w, int err){

        };
        std::cout << "##Main writer shm used:" << master.GetMainShm().GetAllocator().used_space() << std::endl;
        master.WriteToWorkers(workers, dv, write_callback);
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

