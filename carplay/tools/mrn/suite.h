#pragma once
#include "../../platform/common/native_services.h"
namespace cp {
// Command-line mode overrides the small field-test configuration. Old module
// configurations (test.plist / network.plist) remain module-specific.
inline Value suiteConfig() {
    std::wstring path=executableDirectory()+L"/suite.plist";
    if(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES)return Value::dictionary();
    Value v=parsePlist(readFile(path,65536));
    require(v.type==Value::Dict,"suite.plist must be a dictionary");return v;
}
inline std::string suiteMode(LPWSTR command,const Value &v,const char *fallback) {
    if(command && *command)return utf8(command);
    if(!v.dict.count("mode"))return fallback;
    require(v.at("mode").type==Value::String,"Invalid suite mode");return v.at("mode").string;
}
inline unsigned suiteNumber(const Value &v,const char *key,unsigned fallback,unsigned maximum) {
    if(!v.dict.count(key))return fallback;
    const Value &n=v.at(key);require(n.type==Value::Integer&&!n.negative&&n.integer<=maximum,"Invalid suite number");
    return unsigned(n.integer);
}
inline void suiteResult(const char *group,const std::string &mode,int code,const std::string &error) {
    Value v=Value::dictionary();v.dict["group"]=Value::str(group);v.dict["mode"]=Value::str(mode);
    v.dict["exit_code"]=Value::num(code);v.dict["completed"]=Value::flag(code==0);
    v.dict["physical_effect_verified"]=Value::flag(false);
    if(!error.empty())v.dict["error"]=Value::str(error);
    writeFile(executableDirectory()+L"/suite-result.plist",xmlPlist(v));
}
}
