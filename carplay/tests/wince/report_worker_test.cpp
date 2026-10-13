#include "../../platform/common/report_worker.h"
#include <cstdio>
using namespace cp;
static HANDLE entered,releaseWrite;
static std::vector<Value> written;
static void check(bool ok){if(!ok){fprintf(stderr,"Report worker assertion failed\n");ExitProcess(1);}}
static void writeReport(const Value &v,void *){
    unsigned n=unsigned(v.at("sequence").integer);
    if(n==1){SetEvent(entered);check(WaitForSingleObject(releaseWrite,3000)==WAIT_OBJECT_0);}
    if(n==8)throw std::runtime_error("simulated storage failure");
    written.push_back(v);
}
static Value report(unsigned n){Value v=Value::dictionary();v.dict["sequence"]=Value::num(n);return v;}
int main(){
    entered=CreateEventW(0,TRUE,FALSE,0);releaseWrite=CreateEventW(0,TRUE,FALSE,0);
    {
        ReportWorker writer(writeReport,0);check(writer.submit(report(1)));
        check(WaitForSingleObject(entered,3000)==WAIT_OBJECT_0);
        // The disk callback remains blocked while every producer call returns.
        for(unsigned i=2;i<=10;++i)check(writer.submit(report(i)));
        SetEvent(releaseWrite);writer.flush();
        check(written.size()==4);
        check(written[0].at("sequence").integer==1 && written[1].at("sequence").integer==7);
        check(written[2].at("sequence").integer==9 && written[3].at("sequence").integer==10);
        check(written.back().at("diagnostic_reports_dropped").integer==5);
        check(written.back().at("diagnostic_reports_failed").integer==1);
        check(writer.submit(report(11)));writer.flush();
        check(written.back().at("sequence").integer==11);
        check(writer.submit(report(12))); // destructor must drain final report
    }
    check(written.back().at("sequence").integer==12);
    CloseHandle(entered);CloseHandle(releaseWrite);
    puts("Report worker: blocked storage leaves producer free; bounded queue, overflow accounting, error recovery, flush and shutdown drain passed.");
}
