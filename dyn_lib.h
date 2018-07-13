#ifndef DYN_LIB_H
#define DYN_LIB_H

#include <memory>
#include <string>


class PlatformLib
{
public:
   typedef void (*pfn_t)();

   static std::unique_ptr<PlatformLib> load(const std::string& path);

   virtual pfn_t get_proc_address(const std::string& name) const = 0;
};

#endif // DYN_LIB_H
