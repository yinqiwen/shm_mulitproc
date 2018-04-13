/*
 * woker_entry.hpp
 *
 *  Created on: 2018Äê4ÔÂ13ÈÕ
 *      Author: qiyingwang
 */

#ifndef WOKER_ENTRY_HPP_
#define WOKER_ENTRY_HPP_
#include "worker.hpp"
#include <map>

namespace shm_multiproc
{
    class WorkerEntryFactory
    {
        private:
            std::map<std::string, OnMessage*> func_table;
            WorkerEntryFactory();
        public:
            void Add(const char* name, OnMessage* func);
            OnMessage* Get(const char* name);
            OnMessage* First();
            int Size();
            static WorkerEntryFactory& GetInstance();
    };
}



#endif /* WOKER_ENTRY_HPP_ */
