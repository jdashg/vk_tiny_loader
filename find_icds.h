#ifndef FIND_ICDS_H
#define FIND_ICDS_H

#include <memory>
#include <string>
#include <vector>

#include "utils.h"

struct IcdInfo final
{
   std::string json_path;
   std::string library_path;
   std::string vk_api_version;

   static std::unique_ptr<IcdInfo> from(const std::string& json_path,
                                        std::string* out_err);
};

std::vector<std::string> enum_icd_paths();

#endif // FIND_ICDS_H
