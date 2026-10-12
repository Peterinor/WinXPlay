#pragma once
enum { MAX_PROCESS_JOBS=1024, MAX_PROCESS_CONFIG_BYTES=512*1024 };
struct ProcessJob { DWORD atMs,waitMs,retryMs; WCHAR name[64]; };
static BOOL Same(const char* a,const char* b) {
    while(*a&&*a==*b){++a;++b;}return *a==*b;
}
static BOOL Number(const char* p,DWORD max,DWORD* value) {
    if(!*p)return FALSE;DWORD n=0;
    for(;*p;++p){if(*p<'0'||*p>'9')return FALSE;
        DWORD digit=*p-'0';if(n>max/10||(n==max/10&&digit>max%10))return FALSE;n=n*10+digit;}
    *value=n;return TRUE;
}
// Validate the whole file before opening processes. ASCII / UTF-8 BOM only.
extern "C" __declspec(dllexport) DWORD ParseProcessConfig(const BYTE* data,DWORD size,ProcessJob* jobs,DWORD* count) {
    *count=0;if(!data||!size||size>MAX_PROCESS_CONFIG_BYTES)return 0;
    DWORD pos=0,n=0;BOOL header=FALSE;
    if(size>=3&&data[0]==0xef&&data[1]==0xbb&&data[2]==0xbf)pos=3;
    while(pos<size){
        char line[256];DWORD len=0;
        while(pos<size&&data[pos]!='\n'){
            BYTE ch=data[pos++];if(len>=255||!ch||ch>126||(ch<32&&ch!='\r'&&ch!='\t'))return 0;
            line[len++]=(char)ch;
        }
        if(pos<size)++pos;
        if(len&&line[len-1]=='\r')--len;line[len]=0;
        char* p=line;while(*p==' '||*p=='\t')++p;
        if(!*p||*p=='#')continue;
        if(!header){if(!Same(p,"PROCESSOPS_V1")&&!Same(p,"MRN_PROCESSOPS_V1")&&!Same(p,"WINCE_PROCESSOPS_V1"))return 0;header=TRUE;continue;}
        if(n>=MAX_PROCESS_JOBS)return 0;
        char* fields[5];fields[0]=p;DWORD parts=1;
        for(char* q=p;*q;++q)if(*q=='|'){if(parts>=5)return 0;*q=0;fields[parts++]=q+1;}
        if(parts!=5||!Same(fields[0],"KILL"))return 0;
        DWORD at,wait,retry;
        if(!Number(fields[1],3600,&at)||!Number(fields[3],120,&wait)||!Number(fields[4],30,&retry)||!retry)return 0;
        if(n&&at*1000<jobs[n-1].atMs)return 0;
        DWORD k=0;for(const char* s=fields[2];*s;++s){
            char c=*s;if(k>=63||!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return 0;
            jobs[n].name[k++]=(WCHAR)c;
        }
        if(k<5||fields[2][0]=='.')return 0;
        const char* ext=fields[2]+k-4;
        if(ext[0]!='.'||(ext[1]!='e'&&ext[1]!='E')||(ext[2]!='x'&&ext[2]!='X')||(ext[3]!='e'&&ext[3]!='E'))return 0;
        jobs[n].name[k]=0;jobs[n].atMs=at*1000;jobs[n].waitMs=wait*1000;jobs[n].retryMs=retry*1000;++n;
    }
    if(!header||!n)return 0;*count=n;return 1;
}
