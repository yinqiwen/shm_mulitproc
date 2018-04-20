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

#ifndef WORKER_HPP_
#define WORKER_HPP_
#include "shm_fifo.hpp"

namespace shm_multiproc
{
    struct WorkerId
    {
            std::string name;
            int idx;
            WorkerId()
                    : idx(0)
            {
            }
            bool operator<(const WorkerId& other) const
            {
                if (idx == other.idx)
                {
                    return name < other.name;
                }
                return idx < other.idx;
            }
    };
    struct WorkerObj
    {
            const WorkerId& id;
            ShmFIFO& writer;
            ShmFIFOPoller& poller;
            WorkerObj(const WorkerId& wid, ShmFIFO& f, ShmFIFOPoller& p);
    };
    typedef int OnMessage(shm_multiproc::WorkerObj& worker, const char* type, const void* data);
    typedef int OnInit(shm_multiproc::WorkerObj& worker);
    typedef int OnDestroy(shm_multiproc::WorkerObj& worker);
    class EntryFuncRegister
    {
        public:
            EntryFuncRegister(OnMessage* func);
            EntryFuncRegister(OnInit* func, bool is_init);
    };
}

#define DEFINE_ENTRY(worker, type, data)   static int OnMessageExecute(shm_multiproc::WorkerObj& worker, const char* type, const void* data);\
                       static shm_multiproc::EntryFuncRegister entry_instance(OnMessageExecute);\
                       static int OnMessageExecute(shm_multiproc::WorkerObj& worker, const char* type, const void* data)
#define DEFINE_INIT(worker)   static int OnInitExecute(shm_multiproc::WorkerObj& worker);\
                       static shm_multiproc::EntryFuncRegister init_instance(OnInitExecute, true);\
                       static int OnInitExecute(shm_multiproc::WorkerObj& worker)
#define DEFINE_DESTROY(worker)   static int OnDestroyExecute(shm_multiproc::WorkerObj& worker);\
                       static shm_multiproc::EntryFuncRegister destroy_instance(OnDestroyExecute, false);\
                       static int OnDestroyExecute(shm_multiproc::WorkerObj& worker)

#endif /* WORKER_H_ */
