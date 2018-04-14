/*
 *Copyright (c) 2017-2018, yinqiwen <yinqiwen@gmail.com>
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

#ifndef SRC_SHM_FIFO_HPP_
#define SRC_SHM_FIFO_HPP_

#include "mmdata.hpp"
#include "common_types.hpp"
#include <unistd.h>
#include <functional>
#include <string.h>

#define ERR_SHMFIFO_OVERLOAD -1000

using namespace mmdata;

namespace shm_multiproc
{
    struct ShmFIFORefItem
    {
            TypeRefItemPtr val;
            volatile int8_t status;
            ShmFIFORefItem();
            const char* GetType()
            {
                if (NULL == val.get())
                {
                    return NULL;
                }
                TypeRefItem* rp = val.get();
                return rp->type.c_str();
            }
            const void* Get()
            {
                if (NULL == val.get())
                {
                    return NULL;
                }
                TypeRefItem* rp = val.get();
                return rp->val.get();
            }
            uint32_t DecRef()
            {
                if (NULL == val.get())
                {
                    return 0;
                }
                return val.get()->DecRef();
            }
    };
    typedef std::function<int(const char*, const void*)> ConsumeFunction;
    class ShmFIFO
    {
        private:
            MMData& shm_data;
            int eventfd_desc;
            typedef typename SHMVector<ShmFIFORefItem>::Type DataVector;
            typedef boost::interprocess::offset_ptr<DataVector> DataVectorPtr;
            DataVector* data;
            size_t consume_offset;
            size_t produce_offset;
            std::string name;
            int64_t last_notify_write_ms;
            int64_t min_notify_interval_ms;
        private:
            int consumeItem(const ConsumeFunction& cb, int& counter, int max);
            void notifyReader(int64_t now = 0);
        public:
            typedef DataVector RootObject;
            ShmFIFO(MMData& mm, const std::string& nm, int efd = -1);
            int GetEventFD()
            {
                return eventfd_desc;
            }
            void SetMinNotifyIntervalMS(int64_t ms)
            {
                min_notify_interval_ms = ms;
            }
            int64_t GetMinNotifyIntervalMS()
            {
                return min_notify_interval_ms;
            }
            MMData& GetMMData()
            {
                return shm_data;
            }
            int OpenWrite(int maxsize, bool resize = true);
            int OpenRead();
            int Offer(TypeRefItemPtr val);
            int Take(const ConsumeFunction& cb, int max = -1, int timeout = -1);
            int TakeOne(const ConsumeFunction& cb, int timeout = -1);
            int Capacity();

            template<typename T>
            T* New()
            {
                return shm_data.New<T>();
            }
            template<typename T>
            void Offer(T* v)
            {
                TypeRefItemPtr ptr = shm_data.New<TypeRefItem>();
                ptr->destroy = SHMDataDestructor<T>::Free;
                ptr->val = v;
                ptr->ref = 1;
                const char* type_name = T::GetTypeName();
                ptr->type.assign(type_name, strlen(type_name));
                if(0 != Offer(ptr))
                {
                    printf("###Offer failed\n");
                    shm_data.Delete(ptr);
                }
            }

    };

    typedef std::vector<ShmFIFO*> ShmFIFOArrary;
    typedef std::function<void(void)> WakeFunction;

    class ShmFIFOPoller
    {
        private:
            int epoll_fd;
            HeapMMData wake_mdata;
            ShmFIFO* wake_queue;
            void DoWake(bool readev);
        public:
            ShmFIFOPoller();
            int Init();
            int Poll(const ConsumeFunction& func, int64_t maxwait_ms = 5);
            int Wake(const WakeFunction& func);
            ShmFIFO* NewReadFIFO(MMData& mdata, const std::string& name, int evfd);
            int Write(ShmFIFO* write_fifo, TypeRefItemPtr val);
            int WriteAll(const ShmFIFOArrary& fifos, TypeRefItemPtr val);
            int DeleteReadFIFO(ShmFIFO* f);
    };

    long long mstime(void);
}

#endif /* SRC_SHM_FIFO_HPP_ */
