#include "find_icds.h"

#include <cstdio>
#include <fstream>

int
main(const int argc, const char* const argv[])
{
   const auto icd_paths = enum_icd_paths();
   for (const auto& path : icd_paths) {
      printf("%s:\n", path.c_str());

      std::string err;
      const auto icd = IcdInfo::from(path, &err);
      if (!icd) {
         printf("   Error: %s\n", err.c_str());
         continue;
      }
      printf("   Vulkan %s\n", icd->vk_api_version.c_str());
      printf("   %s\n", icd->library_path.c_str());
   }
   return 0;
}
