/*
 * woker_entry.cpp
 *
 *  Created on: 2018Äê4ÔÂ13ÈÕ
 *      Author: qiyingwang
 */
#include "worker_entry.hpp"

namespace shm_multiproc
{
    static WorkerEntryFactory* g_instance = NULL;

    WorkerEntryFactory& WorkerEntryFactory::GetInstance()
    {
        if(NULL == g_instance)
        {
            g_instance = new WorkerEntryFactory;
        }
        return *g_instance;
    }
    WorkerEntryFactory::WorkerEntryFactory()
    {
    }
    void WorkerEntryFactory::Add(const char* name, OnMessage* func)
    {
        func_table[name] = func;
    }
    OnMessage* WorkerEntryFactory::Get(const char* name)
    {
        auto found = func_table.find(name);
        if (found != func_table.end())
        {
            return found->second;
        }
        return NULL;
    }
    OnMessage* WorkerEntryFactory::First()
    {
        auto begin = func_table.begin();
        if (begin != func_table.end())
        {
            return begin->second;
        }
        return NULL;
    }
    int WorkerEntryFactory::Size()
    {
        return func_table.size();
    }
}

