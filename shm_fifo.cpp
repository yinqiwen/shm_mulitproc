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
#include "shm_fifo.hpp"
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <fcntl.h>

#define SHMITEM_STATUS_INIT      1
#define SHMITEM_STATUS_CONSUMED  2
#define SHMITEM_STATUS_CLEANED   3

namespace shm_multiproc
{
    static const int kConsumeFail = -1;
    static const int kConsumeContinue = 0;
    static const int kConsumeExit = 1;

    static long long ustime(void)
    {
        struct timeval tv;
        long long ust;

        gettimeofday(&tv, NULL);
        ust = ((long long) tv.tv_sec) * 1000000;
        ust += tv.tv_usec;
        return ust;
    }

    long long mstime(void)
    {
        return ustime() / 1000;
    }

    static void make_nonblocking(int fd)
    {
        int opts;
        opts = fcntl(fd, F_GETFL);
        opts = opts | O_NONBLOCK;
        fcntl(fd, F_SETFL, opts);
    }

    ShmFIFORefItem::ShmFIFORefItem()
            : status(SHMITEM_STATUS_CLEANED)
    {
    }

    ShmFIFO::ShmFIFO(MMData& mm, const std::string& nm, int efd)
            : shm_data(mm), eventfd_desc(efd), data(NULL), name(nm), last_notify_write_ms(0), min_notify_interval_ms(
                    10), write_event_counter(0)
    {
        if (-1 == eventfd_desc)
        {
            eventfd_desc = eventfd(0, EFD_NONBLOCK);
        }
    }

    int ShmFIFO::consumeItem(const ConsumeDoneFunction& cb, int& counter, int max)
    {
        if (data->consume_idx >= data->size())
        {
            data->consume_idx = 0;
        }
        if (data->consume_idx >= data->size())
        {
            return kConsumeFail;
        }
        ShmFIFORefItem& item = data->at(data->consume_idx);
        if (item.status == SHMITEM_STATUS_CONSUMED)
        {
            data->consume_idx++;
            return kConsumeContinue;
        }
        if (item.status != SHMITEM_STATUS_INIT)
        {
            return kConsumeFail;
        }
        size_t data_idx = data->consume_idx;
        auto done = [this, data_idx]()
        {
            if(SHMITEM_STATUS_INIT == (this->data->at(data_idx)).status)
            {
                (this->data->at(data_idx)).status = SHMITEM_STATUS_CONSUMED;
            }
        };
        counter++;
        data->consume_idx++;
        //data->consumed_seq = item.Seq();
        cb(item.GetType(), item.Get(), done);

        if (max >= 0 && counter >= max)
        {
            return kConsumeExit;
        }
        return kConsumeContinue;
    }
    int ShmFIFO::Capacity()
    {
        return data->size();
    }
    void ShmFIFO::NotifyReader(int64_t now,bool force)
    {
        if(write_event_counter > 0 || force)
        {
            int64_t ev = 1;
            write(eventfd_desc, &ev, 8);
            write_event_counter = 0;
            if (0 == now)
            {
                now = mstime();
            }
            last_notify_write_ms = now;
        }
    }
    void ShmFIFO::TryNotifyReader()
    {
        int64_t now = mstime();
        if (write_event_counter >= 100 || 0 == min_notify_interval_ms
                || now - last_notify_write_ms >= min_notify_interval_ms)
        {
            NotifyReader(now, now - last_notify_write_ms >= min_notify_interval_ms);
        }
    }
    int ShmFIFO::OpenWrite(int maxsize, bool resize)
    {
        data = shm_data.GetNamingObject<RootObject>(name, true);
        if (0 == data->size() || resize)
        {
            data->resize(maxsize);
        }
        //printf("####[%d]OpenWrite at %d\n", getpid(), data->produce_idx);
        return 0;
    }
    int ShmFIFO::OpenRead()
    {
        data = shm_data.GetNamingObject<RootObject>(name, false);
        if (NULL == data)
        {
            return -1;
        }
        size_t idx = data->cleaned_idx;
        while (idx != data->consume_idx)
        {
            ShmFIFORefItem& item = data->at(idx);
            if (item.status == SHMITEM_STATUS_INIT)
            {
                data->consume_idx = idx;
                break;
            }
            idx++;
            if (idx >= data->size())
            {
                idx = 0;
            }
        }
        return 0;
    }

    int ShmFIFO::DataStatus(size_t idx)
    {
        if (NULL == data || idx >= data->size())
        {
            return -1;
        }
        ShmFIFORefItem& item = data->at(idx);
        return (int) item.status;
    }
    void ShmFIFO::tryCleanConsumedItems()
    {
        while (1)
        {
            if (data->cleaned_idx >= data->size())
            {
                data->cleaned_idx = 0;
            }
            ShmFIFORefItem& item = data->at(data->cleaned_idx);
            if (item.status != SHMITEM_STATUS_CONSUMED)
            {
                return;
            }
            if (0 == item.DecRef())
            {
                shm_data.Delete(item.val);
            }
            item.val = NULL;
            item.status = SHMITEM_STATUS_CLEANED;
            data->cleaned_idx++;
        }
    }
    int ShmFIFO::Offer(TypeRefItemPtr val)
    {
        tryCleanConsumedItems();
        if (data->produce_idx >= data->size())
        {
            data->produce_idx = 0;
        }
        ShmFIFORefItem& item = data->at(data->produce_idx);
        if (item.status != SHMITEM_STATUS_CLEANED)
        {
            TryNotifyReader();
            return ERR_SHMFIFO_OVERLOAD;
        }
        item.val = val;
        item.status = SHMITEM_STATUS_INIT;
        write_event_counter++;
        //printf("###[%d] offer offset  %llu  %s\n", getpid(), item.val.get_offset(), item.val->type.c_str());
        TryNotifyReader();
        data->produce_idx++;

        return 0;
    }
    int ShmFIFO::TakeOne(const ConsumeDoneFunction& cb, int timeout)
    {
        return Take(cb, 1, timeout);
    }
    int ShmFIFO::Take(const ConsumeDoneFunction& cb, int max, int timeout)
    {
        if (NULL == data)
        {
            OpenRead();
        }
        if (NULL == data)
        {
            return 0;
        }
        int consumeCounter = 0;
        while (1)
        {
            int status = consumeItem(cb, consumeCounter, max);
            switch (status)
            {
                case kConsumeFail:
                {
                    if (0 == timeout)
                    {
                        return consumeCounter;
                    }
                    fd_set fset;
                    FD_ZERO(&fset); /* clear the set */
                    FD_SET(eventfd_desc, &fset); /* add our file descriptor to the set */
                    int rv = -1;
                    if (timeout >= 0)
                    {
                        struct timeval tv;
                        tv.tv_sec = timeout / 1000;
                        tv.tv_usec = timeout % 1000 * 1000;
                        rv = select(eventfd_desc + 1, &fset, NULL, NULL, &tv);
                    }
                    else
                    {
                        rv = select(eventfd_desc + 1, &fset, NULL, NULL, NULL);
                    }

                    if (1 == rv)
                    {
                        int64_t counter;
                        read(eventfd_desc, &counter, 8);
                        break;
                    }
                    else
                    {
                        return consumeCounter;
                    }
                    return consumeCounter;
                }
                case kConsumeExit:
                {
                    return consumeCounter;
                }
                case kConsumeContinue:
                {
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
        return consumeCounter;
    }

    ShmFIFOPoller::ShmFIFOPoller()
            : epoll_fd(-1), wake_queue(NULL), nearest_timertask_interval(0)
    {
        epoll_fd = epoll_create(10);
    }
    void ShmFIFOPoller::DoWake(bool readev)
    {
        if (readev)
        {
            uint64_t ev;
            read(wake_queue->GetEventFD(), &ev, sizeof(ev));
        }

        auto f = [](const char*, const void* data, DoneFunc done)
        {
            const WakeFunction* func = (const WakeFunction*)data;
            (*func)();
            done();
            return 0;
        };
        while (1)
        {
            int count = wake_queue->TakeOne(f, 0);
            if (count != 1)
            {
                break;
            }
        }
    }
    int ShmFIFOPoller::Init()
    {
        wake_mdata.OpenWrite();
        wake_queue = new ShmFIFO(wake_mdata, "");
        wake_queue->OpenWrite(100000);

        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_DEFAULT);
        pthread_mutex_init(&wake_queue_mutex, &attr);
        pthread_mutexattr_destroy(&attr);

        make_nonblocking(wake_queue->GetEventFD());
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = wake_queue;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, wake_queue->GetEventFD(), &ev) == -1)
        {
            perror("epoll_ctl: sockfd");
            return -1;
        }
        return 0;
    }
    int ShmFIFOPoller::Wake(const WakeFunction& func)
    {
        TypeRefItemPtr val = wake_mdata.NewTypeRefItemWithArg<WakeFunction, WakeFunction>("", func);
        pthread_mutex_lock(&wake_queue_mutex);
        wake_queue->Offer(val);
        pthread_mutex_unlock(&wake_queue_mutex);
        return 0;
    }

    int ShmFIFOPoller::Write(ShmFIFO* write_fifo, TypeRefItemPtr val)
    {
        auto func = [=]()
        {
            if(0 != write_fifo->Offer(val))
            {
                if(0 == val->DecRef())
                {
                    write_fifo->GetMMData().Delete(val);
                }
            }
        };
        Wake(func);
        return 0;
    }
    int ShmFIFOPoller::WriteAll(const ShmFIFOArrary& fifos, TypeRefItemPtr val)
    {
        for (auto fifo : fifos)
        {
            Write(fifo, val);
        }
        return 0;
    }

    void ShmFIFOPoller::AtttachReadFIFO(ShmFIFO* fifo)
    {
        auto func = [=]()
        {
            struct epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.ptr = fifo;
            if (epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, fifo->GetEventFD(), &ev) == -1)
            {
                perror("epoll_ctl: sockfd");
            }
        };
        Wake(func);
    }
    void ShmFIFOPoller::DettachReadFIFO(ShmFIFO* fifo)
    {
        epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, fifo->GetEventFD(), NULL);
    }

    struct EventFDHolder: public ShmFIFO
    {
            WakeFunction wake;
            EventFDHolder(MMData& mm, const std::string& nm, int efd)
                    : ShmFIFO(mm, nm, efd)
            {
            }
            int Take(const ConsumeDoneFunction& cb, int max = -1, int timeout = -1)
            {
                wake();
                return 0;
            }
    };

    void ShmFIFOPoller::AttachEventFD(int fd, const WakeFunction& func)
    {
        HeapMMData tmp;
        EventFDHolder* holder = new EventFDHolder(tmp, "", fd);
        holder->wake = func;
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.ptr = holder;
        if (epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1)
        {
            perror("epoll_ctl: sockfd");
        }
    }
    uint64_t ShmFIFOPoller::TriggerExpiredTimerTasks()
    {
        uint64_t now = mstime();
        while (!timer_task_queue.empty())
        {
            auto first = timer_task_queue.begin();
            if (first->first > now)
            {
                return first->first - now;
            }
            TimerTaskQueue trigger_tasks = first->second;
            timer_task_queue.erase(first);
            for (auto task : trigger_tasks)
            {
                if (!task->canceled)
                {
                    task->func();
                    if (task->repeate)
                    {
                        uint64_t next = mstime() + task->after_ms;
                        timer_task_queue[next].push_back(task);
                        continue;
                    }
                }
                timer_tasks.erase(task->id);
                delete task;
            }
        }
        return 0;
    }
    uint64_t ShmFIFOPoller::AddTimerTask(const TimerFunc& func, uint64_t after_ms, bool repeate)
    {
        uint64_t id = timer_task_id_seed.fetch_add(1);
        uint64_t next = mstime() + after_ms;
        auto f = [=]()
        {
            TimerTask* task = new TimerTask;
            task->func = func;
            task->after_ms = after_ms;
            task->repeate = repeate;
            task->id = id;
            timer_task_queue[next].push_back(task);
            timer_tasks[id] = task;
        };
        Wake(f);
        return id;
    }
    void ShmFIFOPoller::CancelTimerTask(uint64_t id)
    {
        auto f = [=]()
        {
            auto found = timer_tasks.find(id);
            if(found != timer_tasks.end())
            {
                found->second->canceled = true;
            }
        };
        Wake(f);
    }

    int ShmFIFOPoller::Poll(const ConsumeDoneFunction& func, int64_t maxwait_ms)
    {
        if (nearest_timertask_interval > 0 && maxwait_ms > nearest_timertask_interval)
        {
            maxwait_ms = nearest_timertask_interval;
        }
        int maxevents = 16;
        struct epoll_event events[maxevents];

        int nfds = epoll_wait(epoll_fd, events, maxevents, maxwait_ms);
        for (int n = 0; n < nfds; ++n)
        {
            ShmFIFO* fifo = (ShmFIFO*) (events[n].data.ptr);
            if (events[n].events | EPOLLIN)
            {
                if (NULL != fifo)
                {
                    if (fifo == wake_queue)
                    {
                        DoWake(true);
                    }
                    else
                    {
                        int64_t counter;
                        read(fifo->GetEventFD(), &counter, 8);
                        fifo->Take(func, -1, 0);
                    }
                }
            }
            else
            {

            }
        }
        if (0 == nfds)
        {
            DoWake(false);
        }
        nearest_timertask_interval = TriggerExpiredTimerTasks();
        return nfds;
    }

}

