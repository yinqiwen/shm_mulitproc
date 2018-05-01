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
#include "shm_proto.hpp"
#include "worker_entry.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <signal.h>
#include <sstream>
#include <stdlib.h>
#include <dirent.h>

namespace shm_multiproc
{
    static Master* g_master = NULL;
    struct WorkerStartArgs
    {
            std::string exe_path;
            std::string name;
            int idx;
            std::string name_idx;
            int reader_eventfd;
            int writer_eventfd;
            int64_t shm_size;
            int fifo_maxsize;
            std::string read_key_path;
            std::string write_key_path;
            std::string so_path;

            KCFG_DEFINE_FIELDS(exe_path, name,idx, name_idx, reader_eventfd,writer_eventfd,read_key_path,write_key_path,so_path,fifo_maxsize,shm_size)
    };

    struct WorkerProcess
    {
            WorkerOptions options;
            WorkerId id;
            pid_t pid;
            ShmData* shm;
            ShmFIFO* writer;
            ShmFIFO* reader;
            WorkerProcess()
                    : pid(0), shm(NULL), writer(NULL), reader(NULL)
            {
            }
    };

    static pid_t createWorkerProcess(const std::string& dir, const WorkerStartArgs& args,
            const std::vector<std::string>& cmd_args, const std::vector<std::string>& envs)
    {
        pid_t pid = fork();
        if (0 == pid)
        {
            std::string confile = dir + "/" + args.name + ".json";
            kcfg::WriteToJsonFile(args, confile);

            char* start_args[cmd_args.size() + 3];
            start_args[0] = (char*) (args.exe_path.c_str());
            for (size_t i = 0; i < cmd_args.size(); i++)
            {
                start_args[i + 1] = (char*) (cmd_args[i].c_str());
            }
            start_args[cmd_args.size() + 1] = (char*) (confile.c_str());
            start_args[cmd_args.size() + 2] = NULL;

            char* start_envs[envs.size() + 1];
            for (size_t i = 0; i < envs.size(); i++)
            {
                start_envs[i] = (char*) (envs[i].c_str());
            }
            start_envs[envs.size()] = NULL;
            int ret = execvpe(args.exe_path.c_str(), start_args, start_envs);
            if (0 != ret)
            {
                int err = errno;
                printf("Exec error %s\n", strerror(err));
            }
            exit(0);
        }
        return pid;
    }

    Master::Master()
            : last_check_restart_ms(0)
    {

    }

    void Master::DestoryWorker(WorkerProcess* w)
    {
        poller.DettachReadFIFO(w->reader);
        pid_workers.erase(w->pid);
        /*
         * Recreate worker shm next time
         */
        close(w->reader->GetEventFD());
        delete w->reader;
        delete w->shm;
        w->reader = NULL;
        w->shm = NULL;
        //delete w;
    }
    WorkerProcess* Master::GetWorker(pid_t pid)
    {
        auto found = pid_workers.find(pid);
        if (found != pid_workers.end())
        {
            return found->second;
        }
        return NULL;
    }
    WorkerProcess* Master::GetWorker(const WorkerId& id)
    {
        auto found = workers.find(id);
        if (found != workers.end())
        {
            return found->second;
        }
        return NULL;
    }
    pid_t Master::GetWorkerPid(const WorkerId& id)
    {
        WorkerProcess* w = GetWorker(id);
        if (w == NULL)
        {
            return -1;
        }
        return w->pid;
    }

    void Master::RestartWorkers()
    {
        uint64_t now = mstime();
        if (now - last_check_restart_ms < 1000)
        {
            return;
        }
        last_check_restart_ms = now;
        if (!restart_queue.empty())
        {
            WokerRestartQueue next_queue;
            for (auto& opt : restart_queue)
            {
                if (mstime() > opt.start_time)
                {
                    CreateWorker(opt.opt, opt.idx);
                }
                else
                {
                    next_queue.push_back(opt);
                }
            }
            if (restart_queue.size() != next_queue.size())
            {
                restart_queue = next_queue;
            }
        }
        for (const auto& worker : multiproc_options.workers)
        {
            for (int i = 0; i < worker.count; i++)
            {
                WorkerId id;
                id.name = worker.name;
                id.idx = i;
                if (NULL == GetWorker(id))
                {
                    CreateWorker(worker, i);
                }
            }
        }
    }

    void Master::CreateWorker(const WorkerOptions& option, int idx)
    {
        std::stringstream name_ss;
        name_ss << option.name << "_" << idx;

        WorkerStartArgs args;
        args.name = option.name;
        args.name_idx = name_ss.str();
        args.idx = idx;
        args.shm_size = option.shm_size;
        args.fifo_maxsize = option.shm_fifo_maxsize;
        args.exe_path = current_process;
        args.read_key_path = multiproc_options.home;
        args.write_key_path = multiproc_options.worker_home + "/" + args.name_idx;
        args.so_path = option.so_path;
        mkdir(multiproc_options.worker_home.c_str(), 0755);
        mkdir(args.write_key_path.c_str(), 0755);

        WorkerId id;
        id.idx = idx;
        id.name = option.name;
        WorkerProcess* worker = GetWorker(id);
        bool create_worker = false;
        if (NULL == worker)
        {
            create_worker = true;
            worker = new WorkerProcess;
            worker->options = option;
            worker->id = id;
            worker->writer = new ShmFIFO(main_shm, args.name_idx);
            worker->writer->OpenWrite(option.shm_fifo_maxsize);
        }
        if (NULL == worker->shm)
        {
            worker->shm = new ShmData;
            ShmOpenOptions wshm_options;
            wshm_options.recreate = true;
            wshm_options.size = option.shm_size;
            if (0 != worker->shm->OpenShm(args.write_key_path, wshm_options))
            {
                delete worker->shm;
                worker->shm = NULL;
                printf("OpenShm worker Error:%s\n", worker->shm->LastError().c_str());
                if (create_worker)
                {
                    delete worker->writer;
                    delete worker;
                }
                return;
            }
            worker->reader = new ShmFIFO(*(worker->shm), args.name_idx);
            poller.AtttachReadFIFO(worker->reader);
        }

        args.reader_eventfd = worker->writer->GetEventFD();
        args.writer_eventfd = worker->reader->GetEventFD();
        worker->pid = createWorkerProcess(args.write_key_path, args, option.start_args, option.envs);

        pid_workers[worker->pid] = worker;
        if(0 == workers.count(worker->id))
        {
            workers[worker->id] = worker;
        }
    }

    int Master::WriteToWorkers(const std::vector<WorkerId>& workers, google::protobuf::Message* msg,
            const WriteCallback& callback)
    {
        if (workers.empty())
        {
            delete msg;
            return -1;
        }
        auto write_func =
                [this, msg, workers, callback]()
                {
                    const shm_proto::ShmProtoFunctors* funcs = shm_proto::ShmProtoFactory::GetInstance().GetShmFunctors(
                            msg->GetTypeName());
                    TypeRefItemPtr ref = main_shm.NewTypeRefItem(msg->GetTypeName(), funcs->Create, funcs->Destroy, workers.size());
                    funcs->Read(ref.get()->val.get(), msg);
                    delete msg;
                    for(auto worker:workers)
                    {
                        int err = 0;
                        WorkerProcess* w = GetWorker(worker);
                        if(NULL == w || NULL == w->writer)
                        {
                            err = -1;
                        }
                        else
                        {
                            err = w->writer->Offer(ref);
                        }
                        if(0 != err)
                        {
                            //printf("####Writer to worker failed.\n");
                            if(0 == ref->DecRef())
                            {
                                main_shm.Delete(ref);
                            }
                        }
                        callback(worker, err);
                    }

                };
        int err = poller.Wake(write_func);
        return err;
    }

    int Master::WriteToWorker(const WorkerId& worker, google::protobuf::Message* msg, const WriteCallback& callback)
    {
        return WriteToWorkers(std::vector<WorkerId>(1, worker), msg, callback);
    }

    static std::string real_path(const std::string& path)
    {
        char buf[PATH_MAX + 1];
        char* tmp = realpath(path.c_str(), buf);
        if (NULL != tmp)
        {
            return tmp;
        }
        return path;
    }

    int Master::UpdateOptions(const MultiProcOptions& options)
    {
        multiproc_options = options;
        multiproc_options.home = real_path(options.home);
        multiproc_options.worker_home = real_path(options.worker_home);
        return -1;
    }

    static void handle_worker_exit(int signo, siginfo_t* info, void* ctx)
    {
        if (signo == SIGCHLD)
        {
            int status;
            pid_t child = waitpid(-1, &status, WNOHANG);
            if (0 == child || -1 == child)
            {
                return;
            }
            g_master->RestartWorker(child, 1000);
        }
    }

    void Master::RestartWorker(pid_t pid, int after_ms)
    {
        auto func = [=]()
        {
            WorkerProcess* w = GetWorker(pid);
            if(NULL != w)
            {
                WorkerOptions options = w->options;
                WorkerId wid = w->id;
                int idx = w->id.idx;
                DestoryWorker(w);
                if(workers.count(wid) == 0)
                {
                    close(w->writer->GetEventFD());
                    delete w->writer;
                    delete w;
                    return;
                }
                WorkerRestartOptions r;
                r.opt = options;
                r.idx = idx;
                r.start_time = mstime() + after_ms;
                restart_queue.push_back(r);
            }
        };
        poller.Wake(func);
    }
    ShmFIFO* Master::GetWorkerWriter(const WorkerId& id)
    {
        WorkerProcess* w = GetWorker(id);
        if(NULL != w)
        {
            return w->writer;
        }
        return NULL;
    }
    int Master::Kill(const WorkerId& id, int sig, bool restart)
    {
        auto func = [=]()
        {
            WorkerProcess* w = GetWorker(id);
            if(NULL != w)
            {
                if(0 != sig && !restart)
                {
                    TAFSVR_DEBUG_ENDL("erase worker:" << id.name << " from list");
                    workers.erase(id);
                }
                kill(w->pid, sig);
            }
        };
        poller.Wake(func);
        return 0;
    }

    int Master::Start(int argc, const char** argv, const MultiProcOptions& options)
    {
        g_master = this;
        current_process = argv[0];
        UpdateOptions(options);

        ShmOpenOptions main_options;
        main_options.recreate = true;
        main_options.size = multiproc_options.main_shm_size;
        if (0 != main_shm.OpenShm(multiproc_options.home.c_str(), main_options))
        {
            error_reason = "Open main shm error:" + main_shm.LastError();
            return -1;
        }
        poller.Init();

        struct sigaction action;
        action.sa_sigaction = handle_worker_exit;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGCHLD, &action, NULL);

        for (const auto& worker : multiproc_options.workers)
        {
            for (int i = 0; i < worker.count; i++)
            {
                CreateWorker(worker, i);
            }
        }
        return 0;
    }
    int Master::Routine(const ConsumeFunction& func)
    {
        auto func_with_done = [=](const char* type, const void* data, DoneFunc done)
        {
            func(type, data);
            done();
            return 0;
        };
        return Routine(func_with_done);
    }
    int Master::Routine(const ConsumeDoneFunction& func)
    {
        int n = poller.Poll(func, multiproc_options.max_waitms);
        ShmFIFO* wake_fifo = poller.GetWakeQueue();
        if(NULL != wake_fifo)
        {
        	wake_fifo->TryNotifyReader();
        }
        RestartWorkers();
        return n;
    }

    Worker::Worker()
            : reader(NULL), writer(NULL), entry_func(NULL), init_func(NULL), destroy_func(NULL), so_handler(NULL), last_check_parent_ms(
                    0), last_check_so(0)
    {

    }
    void Worker::CheckParent(uint64_t now)
    {
        if (now - last_check_parent_ms >= 1000)
        {
            last_check_parent_ms = now;
            if (getppid() == 1)
            {
                fprintf(stderr, "Exit since parent exit.\n");
                exit(1);
            }
        }
    }

    int Worker::Start(int argc, const char** argv)
    {
        error_reason.clear();
        WorkerStartArgs args;
        if (!kcfg::ParseFromJsonFile(argv[argc - 1], args))
        {
            error_reason.append("ParseFromJsonFile Error:").append(argv[argc - 1]);
            return -1;
        }
        if (args.so_path.empty())
        {
            return -1;
        }
        id.name = args.name;
        id.idx = args.idx;
        so_path = args.so_path;

        if (so_path.empty())
        {
            error_reason.append("No so found in so_path:").append(args.so_path);
            return -1;
        }
        so_handler = dlopen(so_path.c_str(), RTLD_NOW);
        if (NULL == so_handler)
        {
            error_reason.append("dlopen error:").append(dlerror()).append(" ").append(args.so_path);
            return -1;
        }

        entry_func = WorkerEntryFactory::GetInstance().GetEntry();
        if (NULL == entry_func)
        {
            std::stringstream name_ss;
            name_ss << "'OnMessageEntry' entry func expected, but got nothing.";
            error_reason = name_ss.str();
            return -1;
        }
        init_func = (OnInit*) WorkerEntryFactory::GetInstance().GetInit();
        destroy_func = (OnInit*) WorkerEntryFactory::GetInstance().GetDestroy();

        ShmOpenOptions reader_shm_options;
        reader_shm_options.readonly = true;
        reader_shm_options.recreate = false;
        if (0 != reader_shm.OpenShm(args.read_key_path, reader_shm_options))
        {
            error_reason = "Open worker read shm  error:" + reader_shm.LastError();
            return -1;
        }
        reader = new ShmFIFO(reader_shm, args.name_idx, args.reader_eventfd);
        reader->OpenRead();

        ShmOpenOptions wshm_options;
        wshm_options.readonly = false;
        wshm_options.recreate = false;
        wshm_options.size = args.shm_size;
        if (0 != writer_shm.OpenShm(args.write_key_path, wshm_options))
        {
            error_reason = "Open worker write shm  error:" + writer_shm.LastError();
            return -1;
        }
        poller.Init();
        poller.AtttachReadFIFO(reader);
        writer = new ShmFIFO(writer_shm, args.name_idx, args.writer_eventfd);
        writer->OpenWrite(args.fifo_maxsize, true);
        writer->NotifyReader();

        if (NULL != init_func)
        {
            WorkerObj w(id, *writer, poller);
            int ret = init_func(w);
            if (0 != ret)
            {
                return ret;
            }
        }
        //printf("Worker shm size:%d\n", writer->Capacity());
        return 0;
    }

    int Worker::Routine(const ConsumeFunction& func, int64_t maxwait_ms)
    {
        auto func_with_done = [=](const char* type, const void* data, DoneFunc done)
        {
            func(type, data);
            done();
            return 0;
        };
        return Routine(func_with_done, maxwait_ms);
    }

    int Worker::Routine(const ConsumeDoneFunction& func, int maxwait)
    {
        poller.Poll(func, maxwait);
        uint64_t now = mstime();
        writer->TryNotifyReader(now);
        CheckParent(now);
        //CheckLatestLib(now);
        return 0;
    }

    int Worker::Routine(int maxwait)
    {
        auto consume = [this](const char* type, const void* data, DoneFunc done)
        {
            WorkerObj w(id, *writer, poller);
            entry_func(w, type, data);
            done();
            return 0;
        };
        return Routine(consume, maxwait);
    }
    int Worker::Stop()
    {
        if (NULL != destroy_func)
        {
            WorkerObj w(id, *writer, poller);
            return destroy_func(w);
        }
        return 0;
    }
}

