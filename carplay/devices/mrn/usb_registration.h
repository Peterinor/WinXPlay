// Included by the CE USB registration adapter; applies only to MRN F3.21.
static BOOL falseStub(FARPROC function){
    ULONG address=(ULONG)function;
    if(!address)return FALSE;
    if(address&1){const WORD* code=(const WORD*)(address&~1UL);return code[0]==0x2000&&code[1]==0x4770;}
    const DWORD* code=(const DWORD*)address;
    return code[0]==0xe3a00000&&code[1]==0xe12fff1e;
}
static BOOL deviceUsbRegistrationFallback(FARPROC id,FARPROC settings){
    return falseStub(id)&&falseStub(settings);
}
