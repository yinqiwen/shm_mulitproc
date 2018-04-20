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

#ifndef WOKER_ENTRY_HPP_
#define WOKER_ENTRY_HPP_
#include "worker.hpp"
#include <map>

namespace shm_multiproc
{
    class WorkerEntryFactory
    {
        private:
            OnMessage* entry_func;
            OnInit* init_func;
            OnDestroy* destroy_func;
            WorkerEntryFactory();
        public:
            void SetEntry(OnMessage* func)
            {
                printf("###Set entry %p\n", func);
                entry_func = func;
            }
            void SetInit(OnInit* func)
            {
                init_func = func;
            }
            void SetDestroy(OnDestroy* func)
            {
                destroy_func = func;
            }
            OnMessage* GetEntry()
            {
                return entry_func;
            }
            OnInit* GetInit()
            {
                return init_func;
            }
            OnDestroy* GetDestroy()
            {
                return destroy_func;
            }
            static WorkerEntryFactory& GetInstance();
    };
}



#endif /* WOKER_ENTRY_HPP_ */
