#pragma once
enum { OP_COPY=1, OP_INSTALL=2, OP_WATCH=3, OP_DELETE=4, OP_MOVE=5, OP_RUN=7,
    MAX_JOBS=1024, MAX_CONFIG_BYTES=512*1024 };
static BOOL g_legacyGuards=FALSE;
static WCHAR g_toolPath[MAX_PATH]={0},g_configPath[MAX_PATH]={0};
struct FileJob { DWORD operation; WCHAR source[220]; WCHAR target[220]; };
static WCHAR Lower(WCHAR c){return c>=L'A'&&c<=L'Z'?c+32:c;}
static BOOL Prefix(const WCHAR* s,const WCHAR* p){while(*p)if(Lower(*s++)!=Lower(*p++))return FALSE;return TRUE;}
static BOOL Equal(const WCHAR* a,const WCHAR* b){while(*a||*b)if(Lower(*a++)!=Lower(*b++))return FALSE;return TRUE;}
extern "C" __declspec(dllexport) DWORD SafePath(const WCHAR* path,DWORD kind) {
    // 0/1: legacy read/install scope; 2: output leaf; 3: V2 mutation; 4: V2 read.
    if(kind>4)return 0;
    DWORD n=0;while(path[n]){if(++n>=210)return 0;}
    if(!n)return 0;
    DWORD begin=0;
    if(kind!=2){if(path[0]!=L'\\')return 0;begin=1;}
    for(DWORD i=begin;i<=n;++i){
        WCHAR c=path[i];
        if(c==0||c==L'\\'){
            if(i==begin||path[begin]==L' '||path[i-1]==L' '||path[i-1]==L'.')return 0;
            if((i-begin==1&&path[begin]==L'.')||(i-begin==2&&path[begin]==L'.'&&path[begin+1]==L'.'))return 0;
            if(kind==2&&c==L'\\')return 0;begin=i+1;
        }else if(c<32||c>126||c==L'/'||c==L':'||c==L'*'||c==L'?'||c==L'"'||c==L'<'||c==L'>'||c==L'|')return 0;
    }
    if(kind==2)return 1;
    if(kind==4)return 1;
    if(kind==3){
        if((g_toolPath[0]&&Equal(path,g_toolPath))||(g_configPath[0]&&Equal(path,g_configPath)))return 0;
        return !Equal(path,L"\\Hard Disk\\AutoLaunch\\file_ops.exe") &&
            !Equal(path,L"\\Hard Disk\\AutoLaunch\\fileops.txt") &&
            !Equal(path,L"\\Hard Disk\\AutoLaunch\\Autolaunch.txt") &&
            !Equal(path,L"\\Hard Disk\\AUTOLAUNCH_PROD\\file_ops.exe") &&
            !Equal(path,L"\\Hard Disk\\AUTOLAUNCH_PROD\\fileops.txt") &&
            !Equal(path,L"\\Hard Disk\\AUTOLAUNCH_PROD\\AutoLaunch_prod.txt");
    }
    if(kind==1){
        if(!Prefix(path,L"\\NAND_USER\\MRN_"))return 0;
        const WCHAR* p=path+15;while(*p&&*p!=L'\\')++p;
        return *p==L'\\'&&p[1]!=0;
    }
    return Prefix(path,L"\\Windows\\")||Prefix(path,L"\\NAND_USER\\")||Prefix(path,L"\\NAND_RES\\")||Prefix(path,L"\\Hard Disk\\AutoLaunch\\")||Prefix(path,L"\\Hard Disk\\AUTOLAUNCH_PROD\\");
}
extern "C" __declspec(dllexport) DWORD MutationPathAllowed(const WCHAR* path,const WCHAR* runFolder){
    if(!SafePath(path,3))return 0;
    const WCHAR* p=path;const WCHAR* f=runFolder;
    while(*f){if(Lower(*p++)!=Lower(*f++))return 1;}
    return *p!=0&&*p!=L'\\';
}
extern "C" __declspec(dllexport) DWORD ParseFileConfig(const char* input,FileJob* jobs,DWORD* count) {
    *count=0;g_legacyGuards=FALSE;DWORD at=0;BOOL header=FALSE,readSeen=FALSE,v2=FALSE;
    if((BYTE)input[0]==0xef&&(BYTE)input[1]==0xbb&&(BYTE)input[2]==0xbf)at=3;
    while(input[at]){
        WCHAR line[480];DWORD n=0;
        while(input[at]&&input[at]!='\n'){
            BYTE c=(BYTE)input[at++];if(c=='\r'){if(input[at]&&input[at]!='\n')return 0;continue;}
            if(c<32||c>126||n>=479)return 0;line[n++]=c;
        }
        if(input[at]=='\n')++at;line[n]=0;
        if(!n||line[0]==L'#')continue;
        if(!header){v2=Equal(line,L"FILEOPS_V2")||Equal(line,L"MRN_FILEOPS_V2")||Equal(line,L"WINCE_FILEOPS_V2");g_legacyGuards=Prefix(line,L"MRN_");if(!v2&&!Equal(line,L"MRN_FILEOPS_V1"))return 0;header=TRUE;continue;}
        if(*count>=MAX_JOBS)return 0;
        WCHAR* a=0;WCHAR* b=0;
        for(DWORD i=0;i<n;++i)if(line[i]==L'|'){line[i]=0;if(!a)a=line+i+1;else if(!b)b=line+i+1;else return 0;}
        if(!a)return 0;
        DWORD op=Equal(line,L"COPY")?OP_COPY:Equal(line,L"INSTALL")?OP_INSTALL:Equal(line,L"WATCH")?OP_WATCH:
            v2&&Equal(line,L"DELETE")?OP_DELETE:v2&&Equal(line,L"MOVE")?OP_MOVE:
            v2&&Equal(line,L"RUN")?OP_RUN:0;
        if(!op)return 0;
        if(op==OP_DELETE){if(b||!SafePath(a,3))return 0;b=a;while(*b)++b;}
        else if(op==OP_RUN){
            if(b||!SafePath(a,4))return 0;
            b=a;while(*b)++b;
            if(b-a<4||!Equal(b-4,L".exe"))return 0;
        }
        else if(!b)return 0;
        if(op==OP_MOVE){if(!SafePath(a,3)||!SafePath(b,3)||Equal(a,b))return 0;}
        else if(op!=OP_DELETE&&op!=OP_RUN){
            DWORD targetKind=op==OP_INSTALL?(v2?3:1):(op==OP_COPY&&v2&&b[0]==L'\\'?3:2);
            if(!SafePath(a,v2?4:0)||!SafePath(b,targetKind))return 0;
        }
        if((op==OP_INSTALL||op==OP_COPY)&&Equal(a,b))return 0;
        if(op==OP_INSTALL || op==OP_DELETE || op==OP_MOVE){
            if(!v2&&readSeen)return 0;
            if(op==OP_INSTALL&&!v2&&!Prefix(a,L"\\Hard Disk\\AutoLaunch\\")&&!Prefix(a,L"\\Hard Disk\\AUTOLAUNCH_PROD\\"))return 0;
        }else readSeen=TRUE;
        const WCHAR* affected=op==OP_DELETE?a:b;
        for(DWORD j=0;!v2&&j<*count;++j){
            const WCHAR* previous=jobs[j].operation==OP_DELETE?jobs[j].source:jobs[j].target;
            if(Equal(previous,affected))return 0;
        }
        FileJob& job=jobs[*count];job.operation=op;DWORD i=0;
        do{job.source[i]=a[i];}while(a[i++]);i=0;do{job.target[i]=b[i];}while(b[i++]);
        ++*count;
    }
    return header&&*count>0;
}
