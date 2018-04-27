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
#include <map>
#include <deque>
#include <atomic>
#include <pthread.h>

#define ERR_SHMFIFO_OVERLOAD -1000

using namespace mmdata;

namespace shm_multiproc
{
    struct ShmFIFORefItem
    {
            TypeRefItemPtr val;
            volatile uint8_t status;
            //volatile int8_t status;
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
    struct ShmFIFORingBuffer: public SHMVector<ShmFIFORefItem>::Type
    {
            volatile size_t consume_idx;
            size_t produce_idx;
            size_t cleaned_idx;
            ShmFIFORingBuffer(const CharAllocator& alloc)
                    : SHMVector<ShmFIFORefItem>::Type(alloc), consume_idx(0), produce_idx(0), cleaned_idx(0)
            {

            }
    };
    class ShmFIFO;
    typedef std::function<void(void)> DoneFunc;
    typedef std::function<int(const char*, const void*)> ConsumeFunction;
    typedef std::function<int(const char*, const void*, DoneFunc)> ConsumeDoneFunction;
    class ShmFIFO
    {
        private:
            MMData& shm_data;
            int eventfd_desc;
            typedef boost::interprocess::offset_ptr<ShmFIFORingBuffer> ShmFIFORingBufferPtr;
            ShmFIFORingBuffer* data;
            std::string name;
            int64_t last_notify_write_ms;
            int64_t min_notify_interval_ms;
            int64_t write_event_counter;
        private:
            int consumeItem(const ConsumeDoneFunction& cb, int& counter, int max);
            void notifyReader(int64_t now = 0);
            void tryCleanConsumedItems();
        public:
            typedef ShmFIFORingBuffer RootObject;
            ShmFIFO(MMData& mm, const std::string& nm, int efd = -1);
            int GetEventFD()
            {
                return eventfd_desc;
            }
            void NotifyReader(int64_t now = 0);
            void TryNotifyReader();
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
            size_t WriteIdx() const
            {
                return data->produce_idx;
            }
            size_t ReadIdx() const
            {
                return data->consume_idx;
            }
            size_t CleanIdx() const
            {
                return data->cleaned_idx;
            }
            int DataStatus(size_t idx);
            int OpenWrite(int maxsize, bool resize = true);
            int OpenRead();
            int Offer(TypeRefItemPtr val);
            virtual int Take(const ConsumeDoneFunction& cb, int max = -1, int timeout = -1);
            int TakeOne(const ConsumeDoneFunction& cb, int timeout = -1);
            int Capacity();

            template<typename T>
            T* New()
            {
                return shm_data.New<T>();
            }
            template<typename T>
            int Offer(T* v)
            {
                TypeRefItemPtr ptr = shm_data.New<TypeRefItem>();
                ptr->destroy = SHMDataDestructor<T>::Free;
                ptr->val = v;
                ptr->ref = 1;
                const char* type_name = T::GetTypeName();
                ptr->type.assign(type_name, strlen(type_name));
                int err = Offer(ptr);
                if (0 != err)
                {
                    shm_data.Delete(ptr);
                    return -1;
                }
                return err;
            }
    };

    typedef std::vector<ShmFIFO*> ShmFIFOArrary;
    typedef std::function<void(void)> WakeFunction;
    typedef std::function<void(void)> TimerFunc;
    class ShmFIFOPoller
    {
        private:
            int epoll_fd;
            HeapMMData wake_mdata;
            ShmFIFO* wake_queue;
            struct TimerTask
            {
                    TimerFunc func;
                    uint64_t after_ms;
                    uint64_t id;
                    bool repeate;
                    bool canceled;
                    TimerTask()
                            : after_ms(0),id(0),repeate(false),canceled(false)
                    {
                    }
            };
            typedef std::deque<TimerTask*> TimerTaskQueue;
            typedef std::map<uint64_t, TimerTaskQueue> TimerTaskQueueTable;
            typedef std::map<uint64_t, TimerTask*> TimerTaskTable;
            TimerTaskQueueTable timer_task_queue;
            TimerTaskTable timer_tasks;
            uint64_t nearest_timertask_interval;
            std::atomic<std::uint64_t> timer_task_id_seed;
            pthread_mutex_t wake_queue_mutex;
            void DoWake(bool readev);
            uint64_t TriggerExpiredTimerTasks();
        public:
            ShmFIFOPoller();
            int Init();
            int Poll(const ConsumeDoneFunction& func, int64_t maxwait_ms = 5);

            int Wake(const WakeFunction& func);
            void AtttachReadFIFO(ShmFIFO* fifo);
            void DettachReadFIFO(ShmFIFO* fifo);
            void AttachEventFD(int fd, const WakeFunction& func);
            uint64_t AddTimerTask(const TimerFunc& func, uint64_t after_ms, bool repeate = false);
            void CancelTimerTask(uint64_t id);
            int Write(ShmFIFO* write_fifo, TypeRefItemPtr val);
            int WriteAll(const ShmFIFOArrary& fifos, TypeRefItemPtr val);
    };

    long long mstime(void);
}

#endif /* SRC_SHM_FIFO_HPP_ */
