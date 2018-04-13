/*
 * worker.h
 *
 *  Created on: 2018Äê4ÔÂ12ÈÕ
 *      Author: qiyingwang
 */

#ifndef WORKER_HPP_
#define WORKER_HPP_
#include "shm_fifo.hpp"

namespace shm_multiproc
{
    typedef int OnMessage(shm_multiproc::ShmFIFO& writer, const char* type, const void* data);
    class EntryFuncRegister
    {
        public:
            EntryFuncRegister(const char* name, OnMessage* func);
    };
}

#define DEFINE_ENTRY(NAME, writer, type, data)   static int Entry##NAME##Execute(shm_multiproc::ShmFIFO& writer, const char* type, const void* data);\
                       static shm_multiproc::EntryFuncRegister instance(#NAME, Entry##NAME##Execute);\
                       static int Entry##NAME##Execute(shm_multiproc::ShmFIFO& writer, const char* type, const void* data)


#endif /* WORKER_H_ */
