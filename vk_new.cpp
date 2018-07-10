#include "vk_new.h"

// -

bool
HasInternalCallbacks(const VkAllocationCallbacks* const info)
{
   if (!info)
      return false;
   return info->pfnInternalAllocation || info->pfnInternalFree;
}
