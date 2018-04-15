#-------------------------------------------------------------------------------
# Copyright (c) 2017-2018, yinqiwen <yinqiwen@gmail.com>
# All rights reserved.
#  
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#  
#   * Redistributions of source code must retain the above copyright notice,
#     this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#   * Neither the name of Redis nor the names of its contributors may be used
#     to endorse or promote products derived from this software without
#     specific prior written permission.
#  
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS 
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
# THE POSSIBILITY OF SUCH DAMAGE.
#-------------------------------------------------------------------------------
#Makefile
#
# Created on: 2015-5-19
#     Author: yinqiwen

CXX=g++
CC=gcc

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

LIB_PATH=$(PWD)/../deps

DIST_PATH=$(PWD)/../dist

OPTIMIZATION?=-O0
OPT=$(OPTIMIZATION)

CXXFLAGS=-Wall -g ${OPT} -std=c++11 -fPIC -D__STDC_FORMAT_MACROS 
CCFLAGS=-Wall -std=gnu99 ${OPT} -fPIC -pedantic -g -D__STDC_FORMAT_MACROS 
LDFLAGS=-g -rdynamic

BOOST_INC?=/usr/include
INCS=-I./ -I${LIB_PATH}  -I${BOOST_INC} -I../kcfg -I../mmdata/src -I../shm_proto 

LIBS= ../mmdata/src/libmmdata.a  -lprotobuf  -lpthread -ldl

%.o : %.cpp
	${CXX} -c ${CXXFLAGS} ${INCS} $< -o $@

%.o : %.cc
	${CXX} -c ${CXXFLAGS} ${INCS} $< -o $@

%.o : %.c
	${CC} -c ${CCFLAGS} ${INCS} $< -o $@


COMMON_OBJECTS := multiproc.o shm_fifo.o worker_entry.o worker.o ../shm_proto/shm_proto.o 

TEST1OBJ :=  ./example/test1.o ./example/hello.pb.shm.o ./example/hello.pb.o

all:  lib_test1_worker.so test1 

lib_test1_worker.so:./example/test1_worker.o
	${CXX} -shard -shared -rdynamic -o $@ $^

test1:  ${COMMON_OBJECTS}  ${TEST1OBJ}
	${CXX} -o test1 ${LDFLAGS} ${TEST1OBJ} $(COMMON_OBJECTS) ${LIBS}
	
	
clean_test:
	rm -f   ${TEST1OBJ} test1 lib_test1_worker.so
	
clean: clean_test
	rm -f  ${COMMON_OBJECTS}
	

