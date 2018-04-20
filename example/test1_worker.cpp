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
#include "worker.hpp"
#include "hello.pb.shm.hpp"
#include "shm_fifo.hpp"
#include <string.h>
#include <iostream>
using namespace shm_multiproc;

DEFINE_INIT(worker)
{
    printf("init called\n");
    return 0;
}

DEFINE_ENTRY(worker, type, data)
{
    if (0 != strcmp(type, helloworld::ShmHelloTestData::GetTypeName()))
    {
        return -1;
    }
    const helloworld::ShmHelloTestData* t = (const helloworld::ShmHelloTestData*) data;
    std::cout << "Recv From Master:" << *t << std::endl;
//    std::cout << "##Writer shm used:" << worker.writer.GetMMData().GetAllocator().used_space() << std::endl;
//
//    helloworld::ShmHelloTestData* item = worker.writer.New<helloworld::ShmHelloTestData>();
//    item->sv.assign("FromWorker");
//    worker.writer.Offer(item);
    return 0;
}

