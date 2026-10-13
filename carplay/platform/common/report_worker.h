// Bounded diagnostic-only writer. Producers never wait for storage I/O.
#pragma once
#include "native_services.h"
#include <deque>
namespace cp {
class ReportWorker {
    struct Lock {
        CRITICAL_SECTION &mutex;
        Lock(CRITICAL_SECTION &m):mutex(m){EnterCriticalSection(&mutex);}
        ~Lock(){LeaveCriticalSection(&mutex);}
    };
    CRITICAL_SECTION mutex_;
    HANDLE wake_,idle_,thread_;
    std::deque<Value*> pending_;
    bool closing_;
    unsigned dropped_,failed_;
    void (*write_)(const Value &,void *);
    void *context_;
    ReportWorker(const ReportWorker &);
    ReportWorker &operator=(const ReportWorker &);
    static DWORD WINAPI entry(void *p){((ReportWorker*)p)->run();return 0;}
    void run(){
        for(;;){
            WaitForSingleObject(wake_,INFINITE);
            for(;;){
                Value *next=0;unsigned dropped,failed;
                {
                    Lock lock(mutex_);
                    if(pending_.empty()){
                        SetEvent(idle_);
                        if(closing_)return;
                        break;
                    }
                    next=pending_.front();pending_.pop_front();
                    dropped=dropped_;failed=failed_;
                }
                try{
                    next->dict["diagnostic_reports_dropped"]=Value::num(dropped);
                    next->dict["diagnostic_reports_failed"]=Value::num(failed);
                    write_(*next,context_);
                }catch(...){Lock lock(mutex_);++failed_;}
                delete next;
            }
        }
    }
  public:
    ReportWorker(void (*writer)(const Value &,void *),void *context):wake_(0),idle_(0),thread_(0),
        closing_(false),dropped_(0),failed_(0),write_(writer),context_(context){
        InitializeCriticalSection(&mutex_);
        wake_=CreateEventW(0,FALSE,FALSE,0);idle_=CreateEventW(0,TRUE,TRUE,0);
        if(wake_ && idle_)thread_=CreateThread(0,0,entry,this,0,0);
        // Diagnostics must not prevent media from starting if resources run out.
    }
    ~ReportWorker(){
        {Lock lock(mutex_);closing_=true;}
        if(thread_){SetEvent(wake_);WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);}
        if(wake_)CloseHandle(wake_);if(idle_)CloseHandle(idle_);
        DeleteCriticalSection(&mutex_);
    }
    bool submit(const Value &report){
        if(!thread_)return false;
        Value *next=new Value(report),*old=0;
        try{
            Lock lock(mutex_);
            pending_.push_back(next);
            if(pending_.size()>4){old=pending_.front();pending_.pop_front();++dropped_;}
            ResetEvent(idle_);SetEvent(wake_);
        }catch(...){delete next;throw;}
        delete old;return true;
    }
    // Call only after the producer has stopped. Includes the last in-flight write.
    void flush(){if(thread_)WaitForSingleObject(idle_,INFINITE);}
};
}
