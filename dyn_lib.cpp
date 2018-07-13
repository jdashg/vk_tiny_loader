// OPTIONAL FILE!
// If your project has its own library loading primitives, subclass PlatformLib yourself!

#include "dyn_lib.h"

#include "utils.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#else
#include <dlfcn.h>
#endif

// -

#ifdef _WIN32

class WindowsLib : public PlatformLib
{
   friend class PlatformLib;
   const HMODULE lib_;

   explicit WindowsLib(const HMODULE lib)
      : lib_(lib)
   {}

public:
   pfn_t get_proc_address(const std::string& name) const override {
      return (pfn_t)GetProcAddress(lib_, name.c_str());
   }
};

/*static*/
std::unique_ptr<PlatformLib>
PlatformLib::load(const std::string& path)
{
   std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, wchar_t> utf8_to_utf16;
   const auto wpath = utf8_to_utf16.from_bytes(path);
   const auto lib = LoadLibraryW(wpath.c_str());
   if (!lib)
      return nullptr;
   return as_unique(new WindowsLib(lib));
}

#else

class UnixLib : public PlatformLib
{
   friend class PlatformLib;
   void* const lib_;

   explicit UnixLib(void* const lib)
      : lib_(lib)
   {}

public:
   pfn_t get_proc_address(const std::string& name) const override {
      return (pfn_t)dlsym(lib_, name.c_str());
   }
};

/*static*/
std::unique_ptr<PlatformLib>
PlatformLib::load(const std::string& path)
{
   const auto lib = dlopen(path.c_str(), RTLD_LAZY);
   if (!lib)
      return nullptr;
   return as_unique(new UnixLib(lib));
}

#endif
