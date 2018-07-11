#ifndef FIND_ICDS_H
#define FIND_ICDS_H

#include <string>
#include <vector>

#include "utils.h"

struct IcdInfo final
{
   std::string json_path;
   std::string library_path;
   SemanticVersion vk_api_version;
};

std::vector<IcdInfo> EnumIcds();

#endif // FIND_ICDS_H
