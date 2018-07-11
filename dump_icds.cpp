#include "find_icds.h"

#include <cstdio>
#include <fstream>

int
main(const int argc, const char* const argv[])
{
   const auto icds = EnumIcds();
   for (const auto& icd : icds) {
      printf("%s:\n   Vulkan %s\n", icd.library_path.c_str(), icd.vk_api_version.str().c_str());
   }
   return 0;
}
