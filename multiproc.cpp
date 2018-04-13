/*
 * multiproc.cpp
 *
 *  Created on: 2018��4��11��
 *      Author: qiyingwang
 */
#include "multiproc.hpp"
#include "shm_proto.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <sstream>
#include "worker_entry.hpp"

namespace shm_multiproc
{
    static Master* g_master = NULL;
    struct WorkerStartArgs
    {
            std::string exe_path;
            std::string name;
            int reader_eventfd;
            int writer_eventfd;
            std::string read_key_path;
            std::string write_key_path;
            std::string so;
            WokerSoScript so_script;
            std::vector<std::string> start_args;KCFG_DEFINE_FIELDS(exe_path, name, reader_eventfd,writer_eventfd,read_key_path,write_key_path,so,so_script,start_args)
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

    static pid_t createWorkerProcess(const std::string& dir, const WorkerStartArgs& args)
    {
        pid_t pid = fork();
        if (0 == pid)
        {
            std::string confile = dir + "/" + args.name + ".json";
            kcfg::WriteToJsonFile(args, confile);

            char* start_args[args.start_args.size() + 3];
            start_args[0] = (char*) (args.exe_path.c_str());
            for (size_t i = 0; i < args.start_args.size(); i++)
            {
                start_args[i + 1] = (char*) (args.start_args[i].c_str());
            }
            start_args[args.start_args.size() + 1] = (char*) (confile.c_str());
            start_args[args.start_args.size() + 2] = NULL;
            int ret = execv(args.exe_path.c_str(), start_args);
            if (0 != ret)
            {
                int err = errno;
                printf("Exec error %s\n", strerror(err));
            }
            exit(0);
        }
        return pid;
    }

    void Master::DestoryWorker(WorkerProcess* w)
    {
        poller.DeleteReadFIFO(w->reader);
        pid_workers.erase(w->pid);
        workers.erase(w->id);
        close(w->reader->GetEventFD());
        close(w->writer->GetEventFD());
        delete w->writer;
        delete w->reader;
        delete w;
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

    void Master::RestartDeadWorkers()
    {
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
    }

    void Master::CreateWorker(const WorkerOptions& option, int idx)
    {
        std::stringstream name_ss;
        name_ss << option.name << "_" << idx;
        WorkerStartArgs args;
        args.name = name_ss.str();
        args.exe_path = current_process;
        args.read_key_path = multiproc_options.home;
        args.write_key_path = multiproc_options.worker_home + "/" + args.name;
        args.so = option.so;
        args.so_script = option.so_script;
        args.start_args = option.start_args;
        mkdir(multiproc_options.worker_home.c_str(), 0755);
        mkdir(args.write_key_path.c_str(), 0755);
        WorkerProcess* worker = new WorkerProcess;
        worker->options = option;
        worker->id.name = option.name;
        worker->id.idx = idx;
        worker->shm = new ShmData;
        ShmOpenOptions wshm_options;
        wshm_options.recreate = true;
        wshm_options.size = option.shm_size;
        if (0 != worker->shm->OpenShm(args.write_key_path, wshm_options))
        {
            printf("OpenShm Error:%s\n", worker->shm->LastError().c_str());
            exit(-1);
        }
        ShmFIFO remote_writer(*(worker->shm), args.name);
        remote_writer.OpenWrite(option.shm_fifo_maxsize);
        worker->reader = poller.NewReadFIFO(*(worker->shm), args.name, remote_writer.GetEventFD());
        worker->writer = new ShmFIFO(main_shm, args.name);
        worker->writer->OpenWrite(option.shm_fifo_maxsize);
        args.reader_eventfd = worker->writer->GetEventFD();
        args.writer_eventfd = remote_writer.GetEventFD();
        worker->pid = createWorkerProcess(args.write_key_path, args);

        pid_workers[worker->pid] = worker;
        workers[worker->id] = worker;
    }
    void Master::GetWriters(const std::vector<WorkerId>& workers, ShmFIFOArrary& writers)
    {
        for (const auto& id : workers)
        {
            WorkerProcess* w = GetWorker(id);
            if (NULL != w)
            {
                writers.push_back(w->writer);
            }
        }
    }

    int Master::WriteToWorkers(const std::vector<WorkerId>& workers, google::protobuf::Message* msg)
    {
        const shm_proto::ShmProtoFunctors* funcs = shm_proto::ShmProtoFactory::GetInstance().GetShmFunctors(
                msg->GetTypeName());
        auto write_func =
                [=]()
                {
                    ShmFIFOArrary writers;
                    GetWriters(workers, writers);
                    TypeRefItemPtr ref = main_shm.NewTypeRefItem(msg->GetTypeName(), funcs->Create, funcs->Destroy, workers.size());
                    funcs->Read(ref.get()->val.get(), msg);
                    delete msg;
                    for(auto writer:writers)
                    {
                        if(0 != writer->Offer(ref))
                        {
                            printf("####Writer to worker failed.\n");
                            if(0 == ref->DecRef())
                            {
                                main_shm.Delete(ref);
                            }
                        }
                    }

                };
        poller.Wake(write_func);
        return 0;
    }

    void Master::StopWorker(const WorkerId& id, int sig)
    {
        WorkerProcess* w = GetWorker(id);
        if (NULL != w)
        {

            kill(w->pid, sig);
        }
    }

    int Master::WriteToWorker(const WorkerId& worker, google::protobuf::Message* msg)
    {
        return WriteToWorkers(std::vector<WorkerId>(1, worker), msg);
    }

    int Master::UpdateOptions(const MultiProcOptions& options)
    {
        multiproc_options = options;
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
                int idx = w->id.idx;
                DestoryWorker(w);
                WorkerRestartOptions r;
                r.opt = options;
                r.idx = idx;
                r.start_time = mstime() + after_ms;
                restart_queue.push_back(r);
            }
        };
        poller.Wake(func);
    }

    int Master::Start(int argc, const char** argv, const MultiProcOptions& options)
    {
        g_master = this;
        current_process = argv[0];
        UpdateOptions(options);
        ShmOpenOptions main_options;
        main_options.recreate = true;
        main_options.size = options.main_shm_size;
        if (0 != main_shm.OpenShm(options.home.c_str(), main_options))
        {
            printf("###OpenShm:%s Error:%s\n", options.home.c_str(), main_shm.LastError().c_str());
            return -1;
        }
        poller.Init();

        printf("####Init poller\n");

        struct sigaction action;
        action.sa_sigaction = handle_worker_exit;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGCHLD, &action, NULL);

        for (const auto& worker : options.workers)
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
        int n = poller.Poll(func, multiproc_options.max_waitms);
        if (0 == n)
        {
            RestartDeadWorkers();
        }
        return n;
    }

    Worker::Worker()
            : reader(NULL), writer(NULL), entry_func(NULL), so_handler(NULL), last_check_parent_ms(0)
    {

    }

    int Worker::Start(int argc, const char** argv)
    {
        WorkerStartArgs args;
        if (!kcfg::ParseFromJsonFile(argv[argc - 1], args))
        {
            printf("ParseFromJsonFile Error:%s\n", argv[argc - 1]);
            return -1;
        }

        if (args.so.empty())
        {
            so_script::Script script(false);
            script.AddCompileFlag(args.so_script.compiler_flag);
            for (const auto& inc : args.so_script.incs)
            {
                script.AddInclude(inc);
            }
            script.SetWorkDir(args.write_key_path);
            if (0 != script.Build(args.so_script.path))
            {
                printf("Build Error:%s ###%s %s\n", script.GetBuildError().c_str(), argv[argc - 1],
                        args.so_script.path.c_str());
                return -1;
            }
            so_handler = script.GetHandler();
        }
        else
        {
            so_handler = dlopen(args.so.c_str(), RTLD_NOW);
        }

        entry_func = WorkerEntryFactory::GetInstance().First();

        ShmOpenOptions reader_shm_options;
        reader_shm_options.readonly = true;
        reader_shm_options.recreate = false;
        if (0 != reader_shm.OpenShm(args.read_key_path, reader_shm_options))
        {
            printf("Error:%s\n", reader_shm.LastError().c_str());
            exit(-1);
        }
        reader = new ShmFIFO(reader_shm, args.name, args.reader_eventfd);
        reader->OpenRead();

        ShmOpenOptions wshm_options;
        wshm_options.readonly = false;
        wshm_options.recreate = false;
        if (0 != writer_shm.OpenShm(args.write_key_path, wshm_options))
        {
            printf("Error:%s\n", writer_shm.LastError().c_str());
            exit(-1);
        }
        writer = new ShmFIFO(writer_shm, args.name, args.writer_eventfd);
        writer->OpenWrite(0, false);
        printf("Worker shm size:%d\n", writer->Capacity());
        return 0;
    }

    int Worker::Routine(int maxwait)
    {
        auto consume = [this](const char* type, const void* data)
        {
            entry_func(*writer, type, data);
            return 0;
        };
        while (1)
        {
            reader->TakeOne(consume, maxwait);
            uint64_t now = mstime();
            if (now - last_check_parent_ms >= 1000)
            {
                last_check_parent_ms = now;
                if (getppid() == 1)
                {
                    exit(1);
                }
            }
        }
        return 0;
    }
}

