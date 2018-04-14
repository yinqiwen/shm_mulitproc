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

//#define SHMITEM_STATUS_NOTINIT  -1
#define SHMITEM_STATUS_INIT      1
#define SHMITEM_STATUS_CONSUMED  2

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
            : status(SHMITEM_STATUS_CONSUMED)
    {
    }

    ShmFIFO::ShmFIFO(MMData& mm, const std::string& nm, int efd)
            : shm_data(mm), eventfd_desc(efd), data(NULL), consume_offset(0), produce_offset(0), name(nm), last_notify_write_ms(
                    0), min_notify_interval_ms(5)
    {
        if (-1 == eventfd_desc)
        {
            eventfd_desc = eventfd(0, EFD_NONBLOCK);
        }
    }

    int ShmFIFO::consumeItem(const ConsumeFunction& cb, int& counter, int max)
    {
        if (consume_offset >= data->size())
        {
            consume_offset = 0;
        }
        if (consume_offset >= data->size())
        {
            return kConsumeFail;
        }
        ShmFIFORefItem& item = data->at(consume_offset);
        if (item.status != SHMITEM_STATUS_INIT)
        {
            return kConsumeFail;
        }
        cb(item.GetType(), item.Get());
        item.status = SHMITEM_STATUS_CONSUMED;
        counter++;
        consume_offset++;
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
    void ShmFIFO::notifyReader(int64_t now)
    {
        int64_t e = 1;
        write(eventfd_desc, &e, 8);
        if (0 == now)
        {
            now = mstime();
        }
        last_notify_write_ms = now;
    }
    int ShmFIFO::OpenWrite(int maxsize, bool resize)
    {
        data = shm_data.GetNamingObject<RootObject>(name, true);
        if (0 == data->size() || resize)
        {
            data->resize(maxsize);
        }
        //printf("####[%d]OpenWrite %d\n", getpid(), data->size());
        for (size_t i = 0; i < data->size(); i++)
        {
            int prev = i - 1;
            if (prev < 0)
            {
                prev = data->size() - 1;
            }
            if (data->at(i).status != SHMITEM_STATUS_INIT && data->at(prev).status == SHMITEM_STATUS_INIT)
            {
                produce_offset = i;
                break;
            }
        }
        return 0;
    }
    int ShmFIFO::OpenRead()
    {
        data = shm_data.GetNamingObject<RootObject>(name, false);
        if (NULL == data)
        {
            return -1;
        }
        for (size_t i = 0; i < data->size(); i++)
        {
            int prev = i - 1;
            if (prev < 0)
            {
                prev = data->size() - 1;
            }
            if (data->at(i).status == SHMITEM_STATUS_INIT && data->at(prev).status != SHMITEM_STATUS_INIT)
            {
                consume_offset = i;
                break;
            }
        }
        //printf("OpenRead offset at %d %d\n", consume_offset, data->size());
        return 0;
    }
    int ShmFIFO::Offer(TypeRefItemPtr val)
    {
        if (produce_offset >= data->size())
        {
            produce_offset = 0;
        }
        ShmFIFORefItem& item = data->at(produce_offset);
        if (item.status == SHMITEM_STATUS_INIT)
        {
            notifyReader();
            return ERR_SHMFIFO_OVERLOAD;
        }
        if (0 == item.DecRef())
        {
            shm_data.Delete(item.val);
        }
        item.status = SHMITEM_STATUS_INIT;
        item.val = val;
        //printf("###[%d] offer offset  %llu  %s\n", getpid(), item.val.get_offset(), item.val->type.c_str());
        int64_t now = mstime();
        if (0 == min_notify_interval_ms || now - last_notify_write_ms >= min_notify_interval_ms)
        {
            notifyReader(now);
        }
        produce_offset++;
        //printf("Produce at %d\n", produce_offset- 1);
        return 0;
    }
    int ShmFIFO::TakeOne(const ConsumeFunction& cb, int timeout)
    {
        return Take(cb, 1, timeout);
    }
    int ShmFIFO::Take(const ConsumeFunction& cb, int max, int timeout)
    {
    	if(NULL == data)
    	{
    		OpenRead();
    	}
    	if(NULL == data)
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
            : epoll_fd(-1), wake_queue(NULL)
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

        auto f = [](const char*, const void* data)
        {
            const WakeFunction* func = (const WakeFunction*)data;
            (*func)();
            return 0;
        };
        while (1)
        {
            int count = wake_queue->TakeOne(f, false);
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
        wake_queue->Offer(val);
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

    ShmFIFO* ShmFIFOPoller::NewReadFIFO(MMData& mdata, const std::string& name, int evfd)
    {
        ShmFIFO* fifo = new ShmFIFO(mdata, name, evfd);
//        if (-1 == fifo->OpenRead())
//        {
//            delete fifo;
//            return NULL;
//        }
        auto func = [=]()
        {
            struct epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.ptr = fifo;
            //make_nonblocking(fifo->GetEventFD());
                if (epoll_ctl(this->epoll_fd, EPOLL_CTL_ADD, fifo->GetEventFD(), &ev) == -1)
                {
                    perror("epoll_ctl: sockfd");
                    return;
                }
            };
        Wake(func);
        return fifo;
    }
    int ShmFIFOPoller::DeleteReadFIFO(ShmFIFO* fifo)
    {
        epoll_ctl(this->epoll_fd, EPOLL_CTL_DEL, fifo->GetEventFD(), NULL);
        //delete fifo;
        return 0;
    }

    int ShmFIFOPoller::Poll(const ConsumeFunction& func, int64_t maxwait_ms)
    {
        int maxevents = 16;
        struct epoll_event events[maxevents];
        int nfds = epoll_wait(epoll_fd, events, maxevents, maxwait_ms);
        for (int n = 0; n < nfds; ++n)
        {
            ShmFIFO* fifo = (ShmFIFO*) (events[n].data.ptr);
            if (events[n].events | EPOLLIN)
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
            else
            {

            }
        }
        if (0 == nfds)
        {
            DoWake(false);
        }
        return nfds;
    }

}

