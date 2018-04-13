/*
 * multiproc.h
 *
 *  Created on: 2018��4��11��
 *      Author: qiyingwang
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
#include "so_script.hpp"

namespace shm_multiproc
{
    struct WokerSoScript
    {
            std::string path;
            std::string compiler_flag;
            std::vector<std::string> incs;
            KCFG_DEFINE_FIELDS(path, compiler_flag, incs)
    };

    struct WorkerOptions
    {
            std::string name;
            int count;
            int shm_size;
            int shm_fifo_maxsize;
            std::vector<std::string> start_args;
            WokerSoScript so_script;
            std::string so;

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
            WorkerNameTable workers;
            WorkerPIDTable pid_workers;
            WokerRestartQueue restart_queue;

            void RestartDeadWorkers();
            void CreateWorker(const WorkerOptions& option, int idx);
            void DestoryWorker(WorkerProcess* w);
            WorkerProcess* GetWorker(pid_t pid);
            WorkerProcess* GetWorker(const WorkerId& id);
            void GetWriters(const std::vector<WorkerId>& workers, ShmFIFOArrary& writers);

        public:
            ShmData& GetMainShm()
            {
                return main_shm;
            }
            int UpdateOptions(const MultiProcOptions& options);
            int Start(int argc, const char** argcv, const MultiProcOptions& options);
            void StopWorker(const WorkerId& worker, int sig = 3);
            void RestartWorker(pid_t pid, int after_ms = 1000);
            int WriteToWorker(const WorkerId& worker,google::protobuf::Message* msg);
            int WriteToWorkers(const std::vector<WorkerId>& workers, google::protobuf::Message* msg);
            int Routine(const ConsumeFunction& func);

    };
    class Worker
    {
        private:
            ShmData reader_shm;
            ShmData writer_shm;
            ShmFIFO* reader;
            ShmFIFO* writer;
            OnMessage* entry_func;
            void* so_handler;
            uint64_t last_check_parent_ms;
        public:
            Worker();
            int Start(int argc, const char** argcv);
            int Routine(int maxwait = 5);
    };


}

#endif /* MULTIPROC_H_ */