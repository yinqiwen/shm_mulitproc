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

#ifndef MULTIPROC_H_
#define MULTIPROC_H_

#include <unistd.h>
#include <string>
#include <vector>
#include <map>
#include <google/protobuf/message.h>
#include "shm_fifo.hpp"
#include "kcfg.hpp"
#include "worker.hpp"

namespace shm_multiproc
{

    struct WorkerOptions
    {
            std::string name;
            int count;
            int shm_size;
            int shm_fifo_maxsize;
            std::vector<std::string> start_args;
            std::vector<std::string> envs;
            std::string so_home;

            WorkerOptions()
                    : count(1), shm_size(10 * 1024 * 1024), shm_fifo_maxsize(100000)
            {
            }
    };
    struct WorkerRestartOptions
    {
            WorkerOptions opt;
            int idx;
            uint64_t start_time;
            WorkerRestartOptions()
                    : idx(0), start_time(0)
            {
            }
    };

    struct MultiProcOptions
    {
            std::string home;
            std::string worker_home;
            std::vector<WorkerOptions> workers;
            int main_shm_size;
            int max_waitms;
            MultiProcOptions()
                    : main_shm_size(100 * 1024 * 1024), max_waitms(5)
            {
            }
    };

    typedef std::function<void(const WorkerId&, int)> WriteCallback;
    class WorkerProcess;
    class Master
    {
        private:
            std::string current_process;
            ShmData main_shm;
            ShmFIFOPoller poller;
            MultiProcOptions multiproc_options;
            typedef std::map<pid_t, WorkerProcess*> WorkerPIDTable;
            typedef std::vector<WorkerProcess*> WorkerArray;
            typedef std::map<WorkerId, WorkerProcess*> WorkerNameTable;
            typedef std::vector<WorkerRestartOptions> WokerRestartQueue;
            typedef std::map<WorkerId, ShmData*> WorkerShmTable;
            WorkerNameTable workers;
            WorkerPIDTable pid_workers;
            WokerRestartQueue restart_queue;
            WorkerShmTable worker_shms;
            std::string error_reason;
            uint64_t last_check_restart_ms;

            void RestartWorkers();
            void CreateWorker(const WorkerOptions& option, int idx);
            void DestoryWorker(WorkerProcess* w);
            WorkerProcess* GetWorker(pid_t pid);
            WorkerProcess* GetWorker(const WorkerId& id);
        public:
            Master();
            ShmData& GetMainShm()
            {
                return main_shm;
            }
            ShmFIFOPoller& GetPoller()
            {
                return poller;
            }
            int UpdateOptions(const MultiProcOptions& options);
            int Start(int argc, const char** argcv, const MultiProcOptions& options);
            void RestartWorker(pid_t pid, int after_ms = 1000);
            int WriteToWorker(const WorkerId& worker, google::protobuf::Message* msg, const WriteCallback& callback);
            int WriteToWorkers(const std::vector<WorkerId>& workers, google::protobuf::Message* msg,
                    const WriteCallback& callback);
            int Routine(const ConsumeDoneFunction& func);
            int Routine(const ConsumeFunction& func);
            int Kill(const WorkerId& id, int sig, bool restart);
            const std::string& LastError() const
            {
                return error_reason;
            }

    };
    class Worker
    {
        private:
            ShmData reader_shm;
            ShmData writer_shm;
            ShmFIFO* reader;
            ShmFIFO* writer;
            OnMessage* entry_func;
            OnInit* init_func;
            OnDestroy* destroy_func;
            void* so_handler;
            uint64_t last_check_parent_ms;
            uint64_t last_check_so;
            std::string error_reason;
            std::string so_home;
            std::string loaded_so;
            ShmFIFOPoller poller;
            WorkerId id;
            void CheckParent(uint64_t now);
            void CheckLatestLib(uint64_t now);
        public:
            Worker();
            int Start(int argc, const char** argcv);
            int Routine(int maxwait = 5);
            int Routine(const ConsumeDoneFunction& func, int maxwait = 5);
            int Routine(const ConsumeFunction& func, int64_t maxwait_ms = 5);
            OnMessage* GetEntryFunc()
            {
                return entry_func;
            }
            void SetEntryFunc(OnMessage* entry)
            {
                entry_func = entry;
            }
            const WorkerId& GetId() const
            {
                return id;
            }
            ShmFIFO& GetWriter()
            {
                return *writer;
            }
            ShmFIFOPoller& GetPoller()
            {
                return poller;
            }
            const std::string& LastError() const
            {
                return error_reason;
            }
            int Stop();
    };

}

#endif /* MULTIPROC_H_ */
