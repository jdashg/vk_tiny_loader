#include "vulkan/vulkan.h"
#include "find_icds.h"
#include "tjson_cpp/tjson.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#else
#include <dlfcn.h>
#endif

// -

class PlatformLib
{
public:
   static std::unique_ptr<PlatformLib> Load(const std::string& path);
   virtual PFN_vkVoidFunction GetProcAddress(const std::string& name) const = 0;
};

#ifdef _WIN32
class WindowsLib : public PlatformLib
{
   friend class PlatformLib;
   const HMODULE lib_;

public:
   PFN_vkVoidFunction GetProcAddress(const std::string& name) const override {
      return GetProcAddress(lib_, name.c_str());
   }
};

/*static*/
std::unique_ptr<PlatformLib>
PlatformLib::Load(const std::string& path)
{
   std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, wchar_t> utf8_to_utf16;
   const auto wpath = utf8_to_utf16.from_bytes(path);
   const auto lib = LoadLibraryW(wpath.c_str());
   if (!lib)
      return nullptr;
   return as_unique(new WindowsLib{lib});
}
#else
class UnixLib : public PlatformLib
{
   friend class PlatformLib;
   void* const lib_;

public:
   PFN_vkVoidFunction GetProcAddress(const std::string& name) const override {
      return dlsym(lib_, name.c_str());
   }
};

/*static*/
std::unique_ptr<PlatformLib>
PlatformLib::Load(const std::string& path)
{
   const auto lib = dlopen(path.c_str());
   if (!lib)
      return nullptr;
   return as_unique(new UnixLib{lib});
}
#endif

// -

class IcdLib final
{
public:
   const IcdInfo info_;
   const std::unique_ptr<PlatformLib> lib_;

   VKAPI_ATTR VkResult VKAPI_CALL (*pfnIcdNegotiate)(uint32_t* pSupportedVersion);
   PFN_vkGetInstanceProcAddr pfnIcdGetInstanceProcAddr;
   PFN_vkGetDeviceProcAddr pfnIcdGetDeviceProcAddr;

   const auto negotiate = (fnNegotiate*)lib->GetProcAddress("vk_icdNegotiateLoaderICDInterfaceVersion");

   typedef VKAPI_ATTR VkResult VKAPI_CALL fnNegotiate(uint32_t* pSupportedVersion);
   const auto get_inst_sym = (PFN_vkGetInstanceProcAddr)lib->GetProcAddress("vk_icdNegotiateLoaderICDInterfaceVersion");


   static std::unique_ptr<IcdLib> Load(const IcdInfo& info) {
      // TODO: Need to allow loading from a relpath from the json dir.
      auto lib = PlatformLib::Load(info.library_path);
      if (!lib)
         return nullptr;

      const auto icd_iface_version = [&]() {
         if (!negotiate)
            return 0;
      }();

      return as_unique(new IcdLib{ info, std::move(lib) });
   }

private:
   IcdLib(std::unique_ptr<PlatformLib> lib)
      : lib_(std::move(lib))
   {
      pfnIcdNegotiate =
   }
};

class Loader final
{
   std::unordered_map<std::string, std::unique_ptr<IcdLib>> libs_by_path_;

   // Enumerables:
   std::unordered_map<std::string, std::vector<VkExtensionProperties>> ext_props_by_layer_;
   std::vector<VkLayerProperties> layer_props_;

   static std::unique_ptr<Loader> s_loader;
public:
   static Loader& Get() {
      if (!s_loader) {
         s_loader.reset(new Loader);
      }
      return *s_loader;
   }

private:
   Loader() {
   }

public:
   const auto& libs() {
      const auto icds = EnumIcds();
      for (const auto& new_icd : icds) {
         bool is_new = true;
         const auto res = libs_by_path_.insert({ new_icd.library_path, nullptr });
         const bool& did_insert = res.second;
         if (!did_insert)
            continue;
         const auto& itr = res.first;

         itr->second = IcdLib::Load(icd);
         if (!itr->second) {
            libs_by_path_.erase(itr);
         }
      }
      return libs_by_path_;
   }

   const auto& ext_props_by_layer(const std::string& layer_name) {
      auto& ret = ext_props_by_layer_[layer_name];
      ret.clear();

      const auto& libs = libs();
      for (const auto& kv : libs) {
         const auto& lib = kv.second;
         std::vector<VkExtensionProperties> cur;
         while (true) {
            uint32_t count = 0;
            (void)lib->vkEnumerateInstanceExtensionProperties(layer_name.c_str(), &count, nullptr);
            cur.reserve(count);
            const auto res = lib->vkEnumerateInstanceExtensionProperties(layer_name.c_str(), &count, cur.data());
            if (res != VK_INCOMPLETE)
               break;
         }
         ret.insert(ret.end(), cur.begin(), cur.end());
      }
      return ret;
   }

   const auto& layer_props() {
      return layer_props_;
   }
};

extern "C" {

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vkGetInstanceProcAddr(const VkInstance instance, const char* const name)
{

}

template<typename T>
static VkResult
vk_copy_meme(const std::vector<T>& src, uint32_t* const count, T* const dest)
{
   if (!dest) {
      *count = src.size();
      return VK_SUCCESS;
   }

   const auto to_write = std::min<size_t>(*count, src.size());
   (void)std::copy_n(src.data(), to_write, dest);
   auto ret = VK_SUCCESS;
   if (*count < src.size()) {
      ret = VK_INCOMPLETE;
   }
   *count = to_write; // You would think it'd be useful to always return src.size() here, but yet...
   return ret;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties)
{
   // The list of available layers may change at any time due to actions outside
   // of the Vulkan implementation, so two calls to
   // vkEnumerateInstanceLayerProperties with the same parameters may return
   // different results, or retrieve different pPropertyCount values or
   // pProperties contents.
   auto& loader = Loader::Get();
   const auto& props = loader.layer_props();
   return vk_copy_meme(props, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties)
{
   auto& loader = Loader::Get();
   const auto& props = loader.ext_props_by_layer(pLayerName);
   return vk_copy_meme(props, pPropertyCount, pProperties);
}


VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo* const info,
                 const VkAllocationCallbacks* const alloc,
                 VkInstance* const out)
{
}




VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties);

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance                                  instance,
    const VkAllocationCallbacks*                pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance                                  instance,
    uint32_t*                                   pPhysicalDeviceCount,
    VkPhysicalDevice*                           pPhysicalDevices);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceFeatures*                   pFeatures);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties);

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          flags,
    VkImageFormatProperties*                    pImageFormatProperties);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceProperties*                 pProperties);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pQueueFamilyPropertyCount,
    VkQueueFamilyProperties*                    pQueueFamilyProperties);

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice                            physicalDevice,
    VkPhysicalDeviceMemoryProperties*           pMemoryProperties);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice                                    device,
    const char*                                 pName);

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice                            physicalDevice,
    const VkDeviceCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDevice*                                   pDevice);

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice                                    device,
    const VkAllocationCallbacks*                pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice                            physicalDevice,
    const char*                                 pLayerName,
    uint32_t*                                   pPropertyCount,
    VkExtensionProperties*                      pProperties);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties);

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice                            physicalDevice,
    uint32_t*                                   pPropertyCount,
    VkLayerProperties*                          pProperties);


} // extern "C"
