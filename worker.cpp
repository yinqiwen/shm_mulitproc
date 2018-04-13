/*
 * worker.cpp
 *
 *  Created on: 2018��4��13��
 *      Author: qiyingwang
 */
#include "worker.hpp"
#include "multiproc.hpp"
#include "worker_entry.hpp"

namespace shm_multiproc
{

    EntryFuncRegister::EntryFuncRegister(const char* name, OnMessage* func)
    {
        WorkerEntryFactory::GetInstance().Add(name, func);
    }
}

