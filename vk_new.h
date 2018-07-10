#ifndef VK_NEW_H
#define VK_NEW_H

#include "utils.h"
#include "vulkan/vulkan.h"

// -

bool HasInternalCallbacks(const VkAllocationCallbacks* info);

// -

template<T>
static T*
vk_new(const VkAllocationCallbacks* const info,
       const VkSystemAllocationScope scope = VK_SYSTEM_ALLOCATION_SCOPE_COMMAND)
{
   if (!info)
      return new T;

   const auto size = sizeof(T);
   const auto alignment = next_pot(size);

   const auto mem = info->pfnAllocation(info->pUserData, size, alignment, scope);
   return new (mem) T;
}

template<T>
static void
vk_delete(const VkAllocationCallbacks* const info, const T* const ptr)
{
   if (!info) {
      delete ptr;
      return;
   }
   if (!ptr)
      return;

   ptr->~T();
   info->pfnFree(info->pUserData, ptr);
}

// -

template<T>
struct AllocWrapper final
{
   T obj;
   const VkInternalAllocationType type = VK_INTERNAL_ALLOCATION_TYPE_EXECUTABLE; // Only option!
   const VkSystemAllocationScope scope;

   AllocWrapper(const VkSystemAllocationScope scope_)
      : scope(scope_)
   { }

   static const AllocWrapper<T>* From(const T* const ptr) {
      auto pbytes = (char*)ptr;
      return (const AllocWrapper<T>*)(pbytes - &AllocWrapper<T>::obj);
   }
};

// -

template<T>
static T*
vk_new_internal(const VkAllocationCallbacks* const info,
                const VkSystemAllocationScope scope = VK_SYSTEM_ALLOCATION_SCOPE_COMMAND)
{
   if (!has_internal_callbacks(info))
      return vk_new<T>(info, scope);

   const auto wrapper = vk_new<AllocWrapper<T>>(info, scope);
   if (!wrapper)
      return nullptr;

   const auto size = sizeof(*wrapper);
   info->pfnInternalAllocation(info->pUserData, size, wrapper->type, wrapper->scope);

   const auto ptr = &(wrapper->obj);
   return ptr;
}

template<T>
static void
vk_delete_internal(const VkAllocationCallbacks* const info, const T* const ptr)
{
   if (!has_internal_callbacks(info)) {
      vk_delete(info, ptr);
      return;
   }
   if (!ptr)
      return;

   const auto wrapper = AllocWrapper<T>::From(ptr);
   const auto type = wrapper->type;
   const auto scope = wrapper->scope;

   vk_delete(info, wrapper);

   const auto size = sizeof(*wrapper);
   info->pfnInternalFree(info->pUserData, size, type, scope);
}

// -

#endif // VK_NEW_H
