#include "ttp_maki.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <stdexcept>
#include <memory>
#include <cerrno>
#include <limits>
#include <objbase.h>
namespace {
using Bytes=std::vector<uint8_t>;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Range(size_t size,size_t pos,size_t count){Require(pos<=size && count<=size-pos,"MAKI bounds");}
uint32_t U32(const uint8_t* p){uint32_t n;std::memcpy(&n,p,4);return n;}
std::wstring Wide(const std::string& s){
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
 Require(n>0 || s.empty(),"MAKI UTF-8");std::wstring out(n,0);
 MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);return out;
}
std::wstring Lower(std::wstring s){for(auto& c:s)c=towlower(c);return s;}
using Value=std::variant<std::monostate,double,std::wstring,void*>;
inline double Number(const Value& v) {if(auto p=std::get_if<double>(&v)){Require(std::isfinite(*p),"nonfinite MAKI value");return *p;}throw std::runtime_error("expected numeric value");}
inline const std::wstring& String(const Value& v) {if(auto p=std::get_if<std::wstring>(&v))return *p;throw std::runtime_error("expected string");}
inline void* Object(const Value& v) {const auto* p=std::get_if<void*>(&v);Require(p && *p,"null/invalid object receiver");return *p;}
// VCPUassign/SOM::assign preserve the destination's declared type. The public
// ABI uses doubles for numbers, but that must not turn MAKI int/float/bool vars
// into doubles when native methods or arithmetic assign to them.
double MakeDouble(const Value& value) {
 if(std::holds_alternative<double>(value))return Number(value);
 if(auto text=std::get_if<std::wstring>(&value)){const double n=wcstod(text->c_str(),nullptr);Require(std::isfinite(n),"MAKI numeric conversion");return n;}
 return 0;
}
double MakeInt(const Value& value) {
 if(auto text=std::get_if<std::wstring>(&value)){errno=0;const auto n=wcstol(text->c_str(),nullptr,10);Require(errno!=ERANGE,"MAKI integer conversion");return double(n);}
 const double n=std::trunc(MakeDouble(value));
 Require(n>=double(INT32_MIN) && n<=double(INT32_MAX),"MAKI integer range");return n;
}
bool MakeBoolean(const Value& value) {
 if(auto text=std::get_if<std::wstring>(&value)){const auto s=Lower(*text);if(s==L"true" || s==L"t")return true;if(s==L"false" || s==L"f")return false;return MakeInt(value)!=0;}
 if(auto object=std::get_if<void*>(&value))return *object!=nullptr;
 return MakeDouble(value)!=0;
}
Value Convert(uint32_t type,const Value& value) {
 switch(type){
 case 2:return MakeInt(value);
 case 3:{const double n=MakeDouble(value);Require(std::abs(n)<=std::numeric_limits<float>::max(),"MAKI float range");return double(float(n));}
 case 4:return MakeDouble(value);
 case 5:return double(MakeBoolean(value));
 case 6:return String(value);
 default:if(auto object=std::get_if<void*>(&value))return *object;
  Require(std::holds_alternative<double>(value) && Number(value)==0,"MAKI object assignment");return static_cast<void*>(nullptr);
 }
}
struct Ref {
 Value value;int variable{-1};uint32_t type{};
 Ref(Value v,int index=-1,uint32_t declared=0):value(std::move(v)),variable(index),type(declared) {
  if(!type)type=std::holds_alternative<double>(value)?4:std::holds_alternative<std::wstring>(value)?6:std::holds_alternative<void*>(value)?7:0;
 }
};
bool Compare(uint8_t op,const Ref& left,const Ref& right) {
 if(left.type>=7){const auto a=std::get_if<void*>(&left.value),b=std::get_if<void*>(&right.value);
  const bool equal=a && b && *a==*b;return op==8?equal:op==9?!equal:false;}
 const auto b=Convert(left.type,right.value);int order{};
 if(left.type==6)order=String(left.value).compare(String(b));
 else {const auto a=Number(Convert(left.type,left.value)),n=Number(b);order=a<n?-1:a>n?1:0;}
 switch(op){case 8:return order==0;case 9:return order!=0;case 10:return order>0;case 11:return order>=0;case 12:return order<0;default:return order<=0;}
}
struct Reader {
    const Bytes& bytes;size_t pos{};
    template<class T> T Read(){Range(bytes.size(),pos,sizeof(T));T n{};std::memcpy(&n,bytes.data()+pos,sizeof(T));pos+=sizeof(T);return n;}
    uint32_t Count(){auto n=Read<uint32_t>();Require(n<100000,"MAKI table limit");return n;}
    std::wstring Text(){auto n=Read<uint16_t>();Range(bytes.size(),pos,n);auto s=Wide(std::string(reinterpret_cast<const char*>(bytes.data()+pos),n));pos+=n;return s;}
};
struct Program {
    struct Var {uint32_t type{};Value value;bool global{},statik{};};
    struct Method {uint32_t type{};std::wstring name;int arity{-1};};
    struct Event {uint32_t variable{},method{},address{};};
    struct Instruction {uint8_t op{};int32_t arg{};int argc{-1};size_t next{};};
    bool complete{};std::wstring name;std::vector<GUID> types;std::vector<Method> methods;std::vector<Var> vars;std::vector<Event> events;
    std::map<size_t,Instruction> instructions;size_t codeSize{},executed{},eventRuns{};
    std::map<std::wstring,size_t> called;void* group{};void* system{};size_t* budget{};
    std::function<Value(Program&,void*,const Method&,const std::vector<Value>&)> call;
    std::function<void*(const GUID&)> construct;
    std::function<void(void*)> release;
    Program(std::wstring filename,const Bytes& data):name(std::move(filename)) {
        Reader r{data};Require(r.Read<uint32_t>()==0x04034746 && r.Read<uint32_t>()==0x17,"MAKI requires 0x17 format");
        for(auto n=r.Count();n;--n)types.push_back(r.Read<GUID>());
        for(auto n=r.Count();n;--n){const auto type=r.Read<uint32_t>();Require(type>=0x100,"MAKI import type");methods.push_back({type-0x100,r.Text()});}
        for(auto n=r.Count();n;--n){Var v;v.type=r.Read<uint32_t>();const auto raw=r.Read<uint64_t>();
            uint32_t low=uint32_t(raw);float f{};std::memcpy(&f,&low,4);
            v.value=v.type==6?Value(std::wstring{}):v.type>=0x100?Value(static_cast<void*>(nullptr)):Value(v.type==3 || v.type==4?double(f):double(int32_t(low)));
            if(v.type==5)v.value=double(Number(v.value)!=0);
            v.global=r.Read<uint8_t>()!=0;v.statik=r.Read<uint8_t>()!=0;vars.push_back(std::move(v));}
        for(auto n=r.Count();n;--n){auto id=r.Read<uint32_t>();Require(id<vars.size() && vars[id].type==6,"invalid string variable");vars[id].value=r.Text();}
        for(auto n=r.Count();n;--n)events.push_back({r.Read<uint32_t>(),r.Read<uint32_t>(),r.Read<uint32_t>()});
        for(const auto& m:methods) Require(m.type<types.size(),"MAKI import class");
        for(const auto& v:vars) Require(v.type==2 || v.type==3 || v.type==4 || v.type==5 || v.type==6 || (v.type>=0x100 && v.type-0x100<types.size()),"unsupported MAKI variable class/alias");
        codeSize=r.Count();Range(data.size(),r.pos,codeSize);Bytes code(data.begin()+r.pos,data.begin()+r.pos+codeSize);Require(r.pos+codeSize==data.size(),"unsupported MAKI trailer");Reader c{code};
        while(c.pos<code.size()) {
            const auto pos=c.pos;Instruction i;i.op=c.Read<uint8_t>();
            switch(i.op) {
            case 1:case 3:case 0x10:case 0x11:case 0x12:case 0x18:case 0x19:case 0x60:case 0x70:
                i.arg=c.Read<int32_t>();
                if(i.op==0x70)i.argc=c.Read<uint8_t>();
                else if(i.op==0x18 && c.pos+4<=code.size() && (U32(code.data()+c.pos)&0xffff0000)==0xffff0000)i.argc=int(c.Read<uint32_t>()&0xffff);
                break;
            case 0x28:case 0:case 2:case 8:case 9:case 10:case 11:case 12:case 13:case 0x21:case 0x30:case 0x40:case 0x41:case 0x42:case 0x43:case 0x44:case 0x4a:case 0x4c:case 0x50:case 0x51:case 0x61:break;
            default:throw std::runtime_error("unsupported MAKI opcode "+std::to_string(i.op)+" at "+std::to_string(pos));
            }
            i.next=c.pos;instructions.emplace(pos,i);
            if(i.op==1 || i.op==3)Require(i.arg>=0 && size_t(i.arg)<vars.size(),"MAKI variable bounds");
            if(i.op==0x18 || i.op==0x70)Require(i.arg>=0 && size_t(i.arg)<methods.size(),"MAKI method bounds");
            if(i.op==0x60)Require(i.arg>=0 && size_t(i.arg)<types.size(),"MAKI NEW class bounds");
        }
        for(const auto& [pos,i]:instructions)if(i.op==0x10 || i.op==0x11 || i.op==0x12 || i.op==0x19)
            Require(instructions.contains(size_t(int64_t(i.next)+i.arg)),"MAKI branch target");
        for(const auto& e:events)Require(e.variable<vars.size() && e.method<methods.size() && instructions.contains(e.address),"MAKI event bounds");
    }
    void BindSystem(){GUID systemGuid{};CLSIDFromString(L"{D6F50F64-93FA-49b7-93F1-BA66EFAE3E98}",&systemGuid);
        for(auto& v:vars)if(v.statik){Require(v.type>=0x100 && v.type-0x100<types.size() && IsEqualGUID(types[v.type-0x100],systemGuid),"unsupported static MAKI class");v.value=system;}}
    Value Run(size_t ip,const std::vector<Value>& args) {
        std::vector<Ref> stack;for(auto i=args.rbegin();i!=args.rend();++i)stack.push_back({*i});
        std::vector<size_t> returns;const auto pop=[&](){Require(!stack.empty(),"MAKI stack underflow");auto v=stack.back();stack.pop_back();return v;};
        for(size_t step=0;;++step){Require(step<20000 && budget && *budget>0 && stack.size()<4096,"MAKI execution budget");--*budget;++executed;
            const auto i=instructions.at(ip);ip=i.next;
            switch(i.op) {
            case 0:break;case 0x28:complete=true;break;
            case 0x60:stack.push_back({construct(types[i.arg]),-1,7});break;
            case 0x61:{auto object=pop();auto ptr=std::get_if<void*>(&object.value);Require(ptr,"MAKI DELETE type");
                if(*ptr){release(*ptr);for(auto& v:vars)if(auto p=std::get_if<void*>(&v.value);p && *p==*ptr)v.value=static_cast<void*>(nullptr);}
                stack.push_back({static_cast<void*>(nullptr),-1,7});break;}
            case 1:stack.push_back({vars[i.arg].value,i.arg,vars[i.arg].type});break;
            case 2:pop();break;
            case 3:vars[i.arg].value=Convert(vars[i.arg].type,pop().value);break;
            case 0x30:{auto value=pop();auto target=pop();Require(target.variable>=0,"MAKI assignment target");auto& var=vars[target.variable];var.value=Convert(var.type,value.value);stack.push_back({var.value,-1,var.type});break;}
            case 0x18:case 0x70:{const auto& method=methods[i.arg];const int n=method.arity;Require(n>=0 && n<=16 && (i.argc==-1 || n==i.argc),"MAKI signature mismatch");
                std::vector<Value> values;for(int a=0;a<n;++a)values.push_back(pop().value);auto* receiver=Object(pop().value);++called[method.name];stack.push_back({call(*this,receiver,method,values)});break;}
            case 0x10:case 0x11:case 0x12:{bool jump=true;if(i.op!=0x12)jump=MakeBoolean(pop().value)==(i.op==0x11);if(jump)ip=size_t(int64_t(ip)+i.arg);break;}
            case 0x19:Require(returns.size()<256,"MAKI call depth");returns.push_back(ip);ip=size_t(int64_t(ip)+i.arg);break;
            case 0x21:if(returns.empty())return stack.empty()?Value{}:stack.back().value;else {ip=returns.back();returns.pop_back();}break;
            case 0x4a:{auto v=pop();const bool truth=v.type==6?!String(v.value).empty():MakeBoolean(v.value);stack.push_back({double(!truth),-1,5});break;}
            case 0x4c:{auto v=pop();Require(v.type>=2 && v.type<=5,"MAKI negation type");if(v.type!=5)v.value=Convert(v.type,-Number(v.value));stack.push_back(std::move(v));break;}
            default:{const auto right=pop(),left=pop();const auto& b=right.value;const auto& a=left.value;Value out;uint32_t type{};
                switch(i.op){case 8:case 9:case 10:case 11:case 12:case 13:out=double(Compare(i.op,left,right));type=5;break;
                case 0x40:if(std::holds_alternative<std::wstring>(a) || std::holds_alternative<std::wstring>(b)){
                    auto first=String(a),second=String(b);Require(first.size()+second.size()<=65536,"MAKI string limit");out=first+second;
                }else out=MakeDouble(a)+MakeDouble(b);break;
                case 0x41:out=MakeDouble(a)-MakeDouble(b);break;
                case 0x42:out=MakeDouble(a)*MakeDouble(b);break;
                case 0x43:Require(MakeDouble(b)!=0,"MAKI division by zero");out=MakeDouble(a)/MakeDouble(b);break;
                case 0x44:{const auto divisor=int32_t(MakeInt(b)),number=int32_t(MakeInt(a));Require(divisor!=0,"MAKI modulo by zero");out=double(int64_t(number)%int64_t(divisor));type=2;break;}
                case 0x50:out=double(MakeBoolean(a) && MakeBoolean(b));type=5;break;case 0x51:out=double(MakeBoolean(a) || MakeBoolean(b));type=5;break;
                default:throw std::runtime_error("unsupported execution opcode");}if(std::holds_alternative<double>(out))Number(out);stack.push_back({out,-1,type});break;}
            }
        }
    }
    Value EventOn(void* node,const std::wstring& event,const std::vector<Value>& args={}) {Value out;
        for(const auto& e:events)if(vars[e.variable].value==Value(node) && Lower(methods[e.method].name)==Lower(event)){
            Require(args.size()==size_t(methods[e.method].arity),"MAKI event argument count");++eventRuns;out=Run(e.address,args);
            if(complete)break;
        }return out;}
};
Value FromAbi(const TtpMakiValue& v) {
 switch(v.type){case TTP_MAKI_VOID:return {};case TTP_MAKI_NUMBER:Require(std::isfinite(v.number),"nonfinite MAKI value");return v.number;
 case TTP_MAKI_OBJECT:return v.object;case TTP_MAKI_STRING:
 Require(v.text && wcsnlen_s(v.text,65537)<=65536,"MAKI string limit");return std::wstring(v.text);}
 throw std::runtime_error("invalid MAKI value");
}
TtpMakiValue ToAbi(const Value& v) {
 TtpMakiValue out{};
 if(auto number=std::get_if<double>(&v)){out.type=TTP_MAKI_NUMBER;out.number=*number;}
 else if(auto text=std::get_if<std::wstring>(&v)){out.type=TTP_MAKI_STRING;out.text=text->c_str();}
 else if(auto object=std::get_if<void*>(&v)){out.type=TTP_MAKI_OBJECT;out.object=*object;}
 return out;
}
// XP cannot initialize compiler static TLS in DLLs loaded after process startup.
// Explicit Win32 TLS points only to an outer event's stack-owned budget.
struct TlsSlot {
 DWORD index=TlsAlloc();
 ~TlsSlot(){if(index!=TLS_OUT_OF_INDEXES)TlsFree(index);}
} eventTls;
struct EventState {size_t depth{},budget{100000};};
struct EventScope {
 EventState local;EventState* state{};bool outer{};
 EventScope(){
  Require(eventTls.index!=TLS_OUT_OF_INDEXES,"MAKI TLS");
  state=static_cast<EventState*>(TlsGetValue(eventTls.index));outer=!state;
  if(outer){state=&local;Require(TlsSetValue(eventTls.index,state)!=FALSE,"MAKI TLS set");}
  if(state->depth>=32)throw std::runtime_error("MAKI event depth");
  ++state->depth;
 }
 ~EventScope(){--state->depth;if(outer)TlsSetValue(eventTls.index,nullptr);}
};
struct Instance {
 Program program;TtpMakiHost host{};Value result;DWORD thread=GetCurrentThreadId();bool failed{};std::vector<void*> owned;
 Instance(const Bytes& bytes,const TtpMakiHost& h,void* system):program(L"",bytes) {
  std::memcpy(&host,&h,std::min<size_t>(h.size,sizeof(host)));
  if(h.size<sizeof(host)){host.construct=nullptr;host.release=nullptr;}
  for(auto& m:program.methods){
   const auto n=host.resolve(host.context,&program.types[m.type],m.name.c_str());
   Require(n>=0 && n<=16,"unsupported MAKI import");
   m.arity=n;
  }
  for(const auto& [pc,ins]:program.instructions)if(ins.op==0x18 || ins.op==0x70)
   Require(ins.argc<0 || ins.argc==program.methods[ins.arg].arity,"MAKI argument count");
  for(const auto& [pc,ins]:program.instructions)if(ins.op==0x60 || ins.op==0x61)Require(host.construct && host.release,"host does not support MAKI NEW/DELETE");
  program.construct=[this](const GUID& type) {Require(owned.size()<1024,"MAKI object limit");void* result{};
   Require(SUCCEEDED(host.construct(host.context,&type,&result)) && result,"MAKI object construction failed");
   try{owned.push_back(result);}catch(...){host.release(host.context,result);throw;}return result;};
  program.release=[this](void* object){auto it=std::find(owned.begin(),owned.end(),object);Require(it!=owned.end(),"DELETE requires a script-owned object");
   owned.erase(it);host.release(host.context,object);};
  program.system=system;program.BindSystem();
  program.call=[this](Program& p,void* object,const Program::Method& method,const std::vector<Value>& args)->Value {
   std::vector<TtpMakiValue> input;for(const auto& v:args)input.push_back(ToAbi(v));TtpMakiValue output{};
   Require(SUCCEEDED(host.invoke(host.context,&p.types[method.type],object,method.name.c_str(),input.data(),uint32_t(input.size()),&output)),"MAKI native call failed");
   return FromAbi(output);
  };
 }
 ~Instance(){for(auto* object:owned)host.release(host.context,object);}
};
HRESULT WINAPI Create(const uint8_t* bytes,uint32_t size,const TtpMakiHost* host,void* system,void** out) {
 if(!out)return E_POINTER;*out=nullptr;
 if(!bytes || !size || size>4*1024*1024 || !host || host->size<TTP_MAKI_HOST_V1_SIZE || !host->resolve || !host->invoke || !system)return E_INVALIDARG;
 try{*out=new Instance(Bytes(bytes,bytes+size),*host,system);return S_OK;}
 catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);}
}
HRESULT WINAPI CreateChecked(const uint8_t* bytes,uint32_t size,const TtpMakiHost* host,void* system,void** out,wchar_t* message,uint32_t count) {
 if(message && count)message[0]=0;if(!out)return E_POINTER;*out=nullptr;
 if(!bytes || !size || size>4*1024*1024 || !host || host->size<TTP_MAKI_HOST_V1_SIZE || !host->resolve || !host->invoke || !system || (!message && count))return E_INVALIDARG;
 try{*out=new Instance(Bytes(bytes,bytes+size),*host,system);return S_OK;}
 catch(const std::exception& e){if(message && count){const auto text=Wide(e.what());wcsncpy_s(message,count,text.c_str(),_TRUNCATE);}return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);}
 catch(...){return E_FAIL;}
}
void WINAPI Destroy(void* value){delete static_cast<Instance*>(value);}
HRESULT WINAPI Event(void* value,void* object,const wchar_t* name,const TtpMakiValue* args,uint32_t count,TtpMakiValue* result,BOOL* complete) {
 if(result)*result={};if(complete)*complete=FALSE;
 auto* p=static_cast<Instance*>(value);
 if(!p || !object || !name || count>16 || (count && !args) || p->thread!=GetCurrentThreadId())return E_INVALIDARG;
 if(p->failed)return E_FAIL;
 try{EventScope scope;p->program.budget=&scope.state->budget;
  std::vector<Value> input;for(uint32_t i=0;i<count;++i)input.push_back(FromAbi(args[i]));
  p->program.complete=false;p->result=p->program.EventOn(object,name,input);
  if(result)*result=ToAbi(p->result);if(complete)*complete=p->program.complete?TRUE:FALSE;
  return S_OK;
 }catch(...){p->failed=true;return E_FAIL;}
}
}
extern "C" HRESULT WINAPI ttpGetMakiVM(uint32_t version,TtpMakiVM* output){
 if(version!=TTP_MAKI_ABI || !output || output->size<TTP_MAKI_VM_V1_SIZE)return E_INVALIDARG;
 const auto size=uint32_t(std::min<size_t>(output->size,sizeof(*output)));
 const TtpMakiVM api{size,TTP_MAKI_ABI,Create,Destroy,Event,CreateChecked};std::memcpy(output,&api,size);return S_OK;
}
