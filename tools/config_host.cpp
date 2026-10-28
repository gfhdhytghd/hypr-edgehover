// Offline config lifecycle host. No Lua state or compositor is loaded.
#include <cassert>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <variant>
#include <vector>
template<class T> using SP=std::shared_ptr<T>;
using HANDLE=void*;
static int cacheBindings=0;
namespace Config {
using INTEGER=long;
using STRING=std::string;
namespace Values {
struct IValue {virtual ~IValue()=default;virtual const char* name()const=0;virtual void commence()=0;};
template<class T>struct Value:IValue {
 const char* key;T def;
 Value(const char* k,const char*,T v):key(k),def(v){}
 const char* name()const override{return key;}
 void commence()override{++cacheBindings;}
};
using CIntValue=Value<INTEGER>;using CStringValue=Value<STRING>;
}
struct Reply {void* const* dataptr=nullptr;const std::type_info* type=nullptr;};
struct Result {bool ok;explicit operator bool()const{return ok;}};
struct Manager {
 struct Node {std::variant<INTEGER,STRING> value;void* ptr=nullptr;HANDLE owner=nullptr;};
 std::map<std::string,Node> nodes;int registrations=0;
 Result registerPluginValue(HANDLE handle,SP<Values::IValue> value) {
  if(nodes.contains(value->name()))return {false};
  Node n;n.owner=handle;
  if(auto i=std::dynamic_pointer_cast<Values::CIntValue>(value))n.value=i->def;
  else if(auto s=std::dynamic_pointer_cast<Values::CStringValue>(value))n.value=s->def;
  else return {false};
  nodes.emplace(value->name(),std::move(n));++registrations;return {true};
 }
 Reply getConfigValue(const std::string& name) {
  if(!nodes.contains(name))return {};
  auto& n=nodes.at(name);
  return std::visit([&](auto& v){n.ptr=&v;return Reply{&n.ptr,&typeid(v)};},n.value);
 }
 void onPluginUnload(HANDLE handle){std::erase_if(nodes,[&](const auto& n){return n.second.owner==handle;});}
};
static auto manager=std::make_unique<Manager>();
static auto& mgr(){return manager;}
}
namespace HyprlandAPI {
static bool addConfigValueV2(HANDLE handle,SP<Config::Values::IValue> value) {
 if(!Config::mgr()->registerPluginValue(handle,value))return false;
 value->commence();return true;
}
}
// PRODUCTION_CONFIG_ADAPTER
int main() {
 std::signal(SIGABRT,[](int){std::_Exit(86);});
 using namespace hypr_edgehover;
 PluginConfig config;auto handle=reinterpret_cast<HANDLE>(1);
 assert(config.add(handle,std::make_shared<PluginValue<Config::Values::CIntValue>>("plugin:test:enabled","",1)));
 assert(config.add(handle,std::make_shared<PluginValue<Config::Values::CStringValue>>("plugin:test:edges","",std::string("lrtb"))));
 assert(cacheBindings==0);
 PluginConfigValue<Config::INTEGER> enabled("plugin:test:enabled");
 PluginConfigValue<std::string> edges("plugin:test:edges");
 assert(*enabled==1 && *edges=="lrtb");
 Config::mgr()->nodes.at("plugin:test:enabled").value=Config::INTEGER{3};
 assert(*enabled==3);
 int registrations=Config::mgr()->registrations;
 assert(config.restore() && Config::mgr()->registrations==registrations);
 // A new config manager has no plugin options before preReload.
 Config::mgr()=std::make_unique<Config::Manager>();
 assert(*enabled==0 && (*edges).empty());
 assert(config.restore());
 assert(*enabled==1 && *edges=="lrtb" && Config::mgr()->registrations==2);
 assert(config.restore() && Config::mgr()->registrations==2);
 // Presence checks also recover when the manager address is unchanged.
 auto* sameAddress=Config::mgr().get();Config::mgr()->nodes.clear();
 assert(Config::mgr().get()==sameAddress && config.restore());
 assert(*enabled==1 && *edges=="lrtb");
 Config::mgr()->onPluginUnload(handle);config.clear();
 assert(Config::mgr()->nodes.empty() && config.restore());
 std::cout<<"config lifecycle: uncached descriptors, fresh manager reads, reset restoration, no normal-reload collisions and unload PASS\n";
}
