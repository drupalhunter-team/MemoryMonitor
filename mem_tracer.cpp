/* ************************************************************************
	> File Name: mem_tracer.cpp
	> Author: lnmcc
	> Mail: lnmcc@hotmail.com 
	> Blog: lnmcc.net 
	> Created Time: 2014年04月11日 星期五 15时08分46秒
 *********************************************************************** */

#include <iostream>
#include <stdio.h>
#include <string.h>
#include <map>
#include <list>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <pwd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <mqueue.h>
#include <new>

#include "mem_tracer.h"

using namespace std;

#ifdef  MEM_TRACE

#define INNEROP 0x0001
#define OUTEROP 0x0002

MemTracer g_memTracer;
SemMutex g_lock;
char DEL_FILE[FILENAME_LEN] = {0};
int DEL_LINE = 0;
stack<DelInfo> g_delInfoStack;

void buildStack() {
	DelInfo lastDel = {"", 0};
	strcpy(lastDel.fileName, DEL_FILE);
	lastDel.lineNum = DEL_LINE;
	g_delInfoStack.push(lastDel);
}

void* operator new(size_t size) throw(bad_alloc) {
    if(size == 0)
        size = 1;

	void* address = NULL;
    size_t *p = NULL;
    
    while(true) {
        p = (size_t*)malloc(size + 2 * sizeof(size_t));
        if(p)
            break;
        new_handler oldHandler = set_new_handler(0);
        set_new_handler(oldHandler);
        if(oldHandler)
            (*oldHandler)();
        else 
            throw bad_alloc();
    }
    
    p[0] = size;
    p[1] = INNEROP;
    address = (void*)(&p[2]);
	
	return address;
}

void* operator new[](size_t size) throw(bad_alloc) {
    if(size == 0)
        size = 1;

	void* address = NULL;
    size_t *p = NULL;
    
    while(true) {
        p = (size_t*)malloc(size + 2 * sizeof(size_t));
        if(p)
            break;
        new_handler oldHandler = set_new_handler(0);
        set_new_handler(oldHandler);
        if(oldHandler)
            (*oldHandler)();
        else
            throw bad_alloc();
    }
    p[0] = size;
    p[1] = INNEROP;
    address = (void*)(&p[2]);
	
	return address;
}

void* operator new(size_t size, const char* fileName, const int lineNum) throw(bad_alloc) {
    if(size == 0)
        size = 1;

	Operation OP;
	void* address = NULL;
    size_t *p = NULL;
    
    while(true) {
        p = (size_t*)malloc(size + 2 * sizeof(size_t));
        if(p)
            break;
        new_handler oldHandler = set_new_handler(0);
        set_new_handler(oldHandler);
        if(oldHandler)
            (*oldHandler)();
        else 
            throw bad_alloc();
    }

    p[0] = size;
    p[1] = OUTEROP;
    address = (void*)(&p[2]);
	
	if (address) {
        memset(&OP, 0x0, sizeof(Operation));
		strncpy(OP.fileName, fileName, FILENAME_LEN - 1);
		OP.lineNum = lineNum;
		OP.size = size;
		OP.type = SINGLE_NEW;
		OP.address = address;
		g_memTracer.insert(address, &OP);
	}
	
	return address;
}

void* operator new[](size_t size, const char* fileName, const int lineNum) throw(bad_alloc) {
    if(size == 0)
        size = 1;

	Operation OP;
	void *address;
    size_t* p = NULL;
    
    while(true) {
        p = (size_t*)malloc(size + 2 * sizeof(size_t));
         if(p)
            break;
        new_handler oldHandler = set_new_handler(0);
        set_new_handler(oldHandler);
        if(oldHandler)
            (*oldHandler)();
        else 
            throw bad_alloc();
    }

    p[0] = size;
    p[1] = OUTEROP;
    address = (void*)(&p[2]);
	
	if (address) {
        memset(&OP, 0x0, sizeof(Operation));
		strncpy(OP.fileName, fileName, FILENAME_LEN - 1);
		OP.lineNum = lineNum;
		OP.size = size;
		OP.type = ARRAY_NEW;
		OP.address = address;
		g_memTracer.insert(address, &OP);
	}

	return address;
}

void operator delete(void* address) {
	if (NULL == address) {
        g_lock.unlock();
		return;
	}

    size_t* p = (size_t*)address;
    void* p2 = (void*)(&p[-2]); 

    if(p[-1] == INNEROP) {
        g_lock.unlock();
        free(p2);
        return;
    }

    Operation OP;
    memset(&OP, 0x0, sizeof(Operation));
	strncpy(OP.fileName, DEL_FILE, FILENAME_LEN - 1);
	OP.lineNum = DEL_LINE;
	OP.size = p[-2]; 
	OP.type = SINGLE_DELETE;
	OP.address = address;

    memset(DEL_FILE, 0x0, sizeof(DEL_FILE));
	DEL_LINE = 0;

    g_lock.unlock();
		
	g_memTracer.erase(address, &OP);
	free(p2);
}

void operator delete[](void* address) {
	if (NULL == address) {
        g_lock.unlock();
		return;
	}

    size_t* p =(size_t*)address;
    void* p2 = (void*)(&p[-2]);

    if(p[-1] == INNEROP) {
        g_lock.unlock();
        free(p2);
        return;
    }

    Operation OP;
    memset(&OP, 0x0, sizeof(Operation));
	strncpy(OP.fileName, DEL_FILE, FILENAME_LEN - 1);
	OP.lineNum = DEL_LINE;
	OP.size = p[-2]; 
	OP.type = ARRAY_DELETE;
	OP.address = address;

    memset(DEL_FILE, 0x0, sizeof(DEL_FILE));
	DEL_LINE = 0;

    g_lock.unlock();
		
	g_memTracer.erase(address, &OP);
	free(p2);
}

MemTracer::MemTracer() {	
	key_t key;
	m_fp = NULL;
	m_msgPath = "/tmp/mem_tracer";

	if((m_fp = fopen(m_msgPath, "a")) == NULL) {
		cerr << "MemTracer: Cannot open file: " <<  m_msgPath << endl; 
		exit(1);
	}

	if((key = ftok(m_msgPath, 'a')) == -1 ) {
		cerr << "MemTracer: Cannot create KEY for message queue" << endl;
        fclose(m_fp);
		exit(1);
	}

	if((m_msgQueue = msgget(key, 0)) == -1) {
        parseError(__FILE__, __LINE__, errno);
        cerr << "MemTracer: Message queue error" << endl;
        fclose(m_fp);
        exit(1);
	}
}

MemTracer::~MemTracer() {
    if(m_fp)
        fclose(m_fp);
}

void MemTracer::parseError(const char* file, const int lineNum, const int err) {
    cerr << "Error: " << file << ": " << lineNum << ": ";
	switch(err) {
	case EAGAIN:
		cerr << "---EAGAIN---" << endl;
		break;
	case EACCES:
		cerr << "---EACCES---" << endl;
		break;
	case EFAULT:
		cerr << "---EFAULT---" << endl;
		break;
	case EIDRM:
		cerr << "---EIDRM---" << endl;
		break;
	case EINTR:
		cerr << "---EINTR---" << endl;
		break;
	case EINVAL:
		cerr << "---EINVAL---" << endl;
		break;
	case ENOMEM:
		cerr << "---ENOMEM---" << endl;
		break;
	case EEXIST:
		cerr << "---EEXIST---" << endl;
		break;
	default:
	    cerr << "---Undefined Error---" << endl;
		break;	
	}
}

void MemTracer::insert(void* address, Operation* OP) {
	MsgEntity sendMsg;
    struct msqid_ds	msgQueInfo;

	if(!address || !OP) {
        cerr << "MemTracer: Error address or operation" << endl;
		return; 
	}
	memcpy((void*) &sendMsg.OP, OP, sizeof(Operation));
	
	m_mutex.lock();

	if(m_msgQueue != -1) {
		
		if(0 == msgctl(m_msgQueue, IPC_STAT, &msgQueInfo)) {
			if(msgQueInfo.msg_qbytes > (msgQueInfo.msg_cbytes + sizeof(Operation))) {
				sendMsg.type = MSG_TYPE;
				if(msgsnd(m_msgQueue, &sendMsg, sizeof(sendMsg.OP), IPC_NOWAIT) == -1) 
					parseError(__FILE__, __LINE__, errno);
            } else {
                cerr << "MemTracer: Cannot not send message, no space" << endl;
            }
		} else 
			parseError(__FILE__, __LINE__, errno);
	}

	m_mutex.unlock();
}

bool MemTracer::erase(void* address, Operation* OP) {
	MsgEntity sendMsg;
	struct msqid_ds	msgQueInfo;

	if (!address || !OP) {
        cerr << "MemTracer: you want to erase a error address or operation";
		return false;
    }

	if(!strcmp(OP->fileName, "") && OP->lineNum == 0) {
		if(!g_delInfoStack.empty()) {
			DelInfo del = g_delInfoStack.top();
			strcpy(OP->fileName, del.fileName);
			OP->lineNum = del.lineNum;
			g_delInfoStack.pop();
		} 
	}
	
	m_mutex.lock();

	if(m_msgQueue != -1) {
		if(msgctl(m_msgQueue, IPC_STAT, &msgQueInfo) == 0) {
			if(msgQueInfo.msg_qbytes > (msgQueInfo.msg_cbytes + sizeof(Operation))) {
				memcpy((void *)&sendMsg.OP, OP, sizeof(Operation));
				sendMsg.type = MSG_TYPE;
                sendMsg.OP.size = OP->size;
				if(msgsnd(m_msgQueue, &sendMsg, sizeof(sendMsg.OP), IPC_NOWAIT) == -1)
					parseError(__FILE__, __LINE__, errno);
            }	
		} else 
			parseError(__FILE__, __LINE__, errno);
	}

	m_mutex.unlock();
    return true;
}

#endif
