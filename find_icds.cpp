#include "find_icds.h"

#include <fstream>
#include <iostream>
#include "tjson_cpp/tjson.h"

#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#else
#include <dirent.h>
#endif

// -

static std::unique_ptr<std::vector<std::string>>
list_dir(const std::string& dir_path)
{
   std::vector<std::string> ret;

#ifdef _WIN32
   const auto dir_wpath = to_wstring(dir_path);
   WIN32_FIND_DATAW data;
   const auto search_str = dir_wpath + L"\\*";
   printf("%ls:\n", search_str.c_str());
   const auto handle = FindFirstFileW(search_str.c_str(), &data);
   if (handle == INVALID_HANDLE_VALUE)
      return nullptr;

   while (true) {
      const auto file_name = to_string(data.cFileName);
      auto subpath = path_concat(dir_path, file_name);
      printf("   %s\n", subpath.c_str());
      ret.push_back(std::move(subpath));
      if (!FindNextFileW(handle, &data))
         break;
   }
   FindClose(handle);

#else
   auto dir = opendir(dir_path.c_str());
   if (!dir)
      return nullptr;

   while (true) {
      const auto entry = readdir(dir);
      if (!entry)
         break;
      auto subpath = path_concat(dir_path, entry->d_name);
      ret.push_back(std::move(subpath));
   }
   closedir(dir);
#endif

   return std::make_unique<std::vector<std::string>>(std::move(ret));
}

#ifdef _WIN32

class RegNode final
{
public:
   const HKEY handle_;
   const bool should_close_;

public:
   explicit RegNode(HKEY handle, bool should_close=false)
      : handle_(handle)
      , should_close_(false)
   { }

   ~RegNode() {
      if (should_close_) {
         RegCloseKey(handle_);
      }
   }

   std::unique_ptr<RegNode> open(const wchar_t* const subkey) const {
      HKEY subhandle;
      const auto res = RegOpenKeyExW(handle_, subkey, 0, KEY_READ, &subhandle);
      if (res != ERROR_SUCCESS)
         return nullptr;
      return as_unique(new RegNode(subhandle, true));
   }

   std::vector<std::wstring> enum_subkeys() const {
      std::vector<std::wstring> ret;
      constexpr size_t buff_size = 255 + 1;
      wchar_t subkey_buff[buff_size]; // Max key size is 255.
      for (DWORD i = 0; true; i++) {
         DWORD size = buff_size;
         const auto res = RegEnumKeyExW(handle_, i, subkey_buff, &size, nullptr,
                                        nullptr, nullptr, nullptr);
         if (res != ERROR_SUCCESS)
            break;
         ret.push_back(std::wstring(subkey_buff));
      }
      return ret;
   }

   std::vector<uint8_t> value_bytes(const wchar_t* const subkey,
                                    const wchar_t* const name) const
   {
      DWORD size = 0;
      auto res = RegGetValueW(handle_, subkey, name, RRF_RT_ANY, nullptr, nullptr, &size);
      if (res != ERROR_SUCCESS)
         return {};

      auto ret = std::vector<uint8_t>(size);
      res = RegGetValueW(handle_, subkey, name, RRF_RT_ANY, nullptr, ret.data(), &size);
      if (res != ERROR_SUCCESS)
         return {};

      return ret;
   }
};

static const auto REG_PATH = L"System\\CurrentControlSet\\Control\\Class";
static const auto GUID_DISPLAY = L"{4d36e968-e325-11ce-bfc1-08002be10318}";
static const auto GUID_SOFTWARE_COMPONENT = L"{5c4c3332-344d-483c-8739-259e934c9cc8}";
static const auto LEGACY_REG_PATH = L"SOFTWARE\\Khronos\\Vulkan\\Drivers";

static std::vector<std::wstring>
load_from_registry()
{
   const auto local_machine = RegNode(HKEY_LOCAL_MACHINE);
   std::vector<std::wstring> paths_from_reg;

   [&]() {
      const auto class_node = local_machine.Open(REG_PATH);
      if (!class_node)
         return;

      const auto fn_for_guid = [&](const wchar_t* const guid) {
         const auto guid_node = class_node->open(guid);
         if (!guid_node)
            return;

         const auto subkeys = guid_node->enum_subkeys();
         for (const auto& k : subkeys) {
            if (k.length() != 4)
               continue;
            const auto bytes = guid_node->value_bytes(k.c_str(), L"VulkanDriverName");
            if (!bytes.size())
               return;

            const auto begin = (const wchar_t*)bytes.data();
            const auto end = begin + bytes.size() / sizeof(wchar_t);
            auto pos = begin;
            for (auto itr = begin; itr != end; ++itr) {
               if (!*itr) {
                  paths_from_reg.push_back(std::wstring(pos, itr));
                  pos = itr + 1;
               }
            }
         }
      };
      fn_for_guid(GUID_DISPLAY);
      fn_for_guid(GUID_SOFTWARE_COMPONENT);
   }();

   [&]() {
      const auto drivers_node = local_machine.Open(LEGACY_REG_PATH);
      if (!drivers_node)
         return;

      std::vector<wchar_t> val_name_buff(32 * 1024);
      uint32_t val_val = -1;

      for (uint32_t i = 0; true; i++) {
         DWORD name_size = val_name_buff.size();
         DWORD val_size = sizeof(val_val);
         const auto res = RegEnumValueW(drivers_node->handle_, i, val_name_buff.data(),
                                        &name_size, nullptr, nullptr, (BYTE*)&val_val,
                                        &val_size);
         if (res == ERROR_NO_MORE_ITEMS)
            break;
         if (res != ERROR_SUCCESS)
            continue;

         if (val_val == 0) {
            paths_from_reg.push_back(val_name_buff.data());
         }
      }
   }();

   return paths_from_reg;
}

#endif // _WIN32

static std::vector<std::string>
split_string(const std::string& str, const char delim)
{
   std::vector<std::string> ret;
   const auto end = str.end();
   auto pos = str.begin();
   for (auto itr = str.begin(); itr != end; ++itr) {
      if (*itr == delim) {
         ret.push_back(std::string(pos, itr));
         pos = itr + 1;
      }
   }
   ret.push_back(std::string(pos, end));
   return ret;
}

std::vector<std::string>
enum_icd_paths()
{
   std::vector<std::string> ret;

   // --

   [&]() {
      const auto env = getenv("VK_ICD_FILENAMES");
      if (!env)
         return;

      const auto paths = split_string(env, ':');
      for (const auto& path : paths) {
         ret.push_back(path);
      }
   }();

   // --

#ifdef _WIN32
   const auto wpaths = load_from_registry();
   for (const auto& wpath : wpaths) {
      auto path = to_string(wpath);
      ret.push_back(std::move(path));
   }
#endif // _WIN32

   std::vector<std::string> icd_dirs;
#ifdef __APPLE__
   /* <bundle>/Contents/Resources/vulkan/icd.d
    * /etc/vulkan/icd.d
    * /usr/local/share/vulkan/icd.d
    *
    * /usr/share/vulkan/icd.d
    * $HOME/.local/share/vulkan/icd.d
    */
   [&]() {
      const CFBundleRef main_bundle = CFBundleGetMainBundle();
      if (!main_bundle)
         return;

      const CFURLRef ref = CFBundleCopyResourcesDirectoryURL(main_bundle);
      if (!ref)
         return;

      std::vector<uint8_t> buff(1000);
      if (!CFURLGetFileSystemRepresentation(ref, true, buff.data(), buff.size()))
         return;

      const auto path = std::string((const char*)buff.data()) + "/Contents/Resources/vulkan/icd.d";
      icd_dirs.push_back(path);
   }();
   icd_dirs.push_back("/etc/vulkan/icd.d");
   icd_dirs.push_back("/usr/local/share/vulkan/icd.d");


#endif // __APPLE__

#ifdef __linux__
   /* /usr/local/etc/vulkan/icd.d
    * /usr/local/share/vulkan/icd.d
    * /etc/vulkan/icd.d
    *
    * /usr/share/vulkan/icd.d
    * $HOME/.local/share/vulkan/icd.d
    */
   icd_dirs.push_back("/usr/local/etc/vulkan/icd.d");
   icd_dirs.push_back("/usr/local/share/vulkan/icd.d");
   icd_dirs.push_back("/etc/vulkan/icd.d");
#endif // __linux

#ifndef _WIN32
   icd_dirs.push_back("/usr/share/vulkan/icd.d");

   [&]() {
      const auto env = getenv("HOME");
      if (!env)
         return;

      const auto path = std::string(env) + "/.local/share/vulkan/icd.d";
      icd_dirs.push_back(path);
   }();
#endif // !_WIN32

   static const std::string JSON_EXT = ".json";

   for (const auto& dir : icd_dirs) {
      const auto files = list_dir(dir);
      if (!files)
         continue;

      for (const auto& file : *files) {
         if (!ends_with(file, JSON_EXT))
            continue;
         ret.push_back(file);
      }
   }

   return ret;
}

/*static*/ std::unique_ptr<IcdInfo>
IcdInfo::from(const std::string& json_path, std::string* const err)
{
   const auto bytes = read_bytes(json_path, err, std::ios_base::binary);
   if (!bytes)
      return nullptr;

   const auto json = tjson::read((const char*)bytes->data(),
                                 (const char*)bytes->data() + bytes->size(), err);
   /* icd.d/<*>.json
   {
      "file_format_version": "1.0.0",
      "ICD": {
         "library_path": "/Users/jgilbert/lib/libMoltenVK.dylib",
         "api_version": "1.0.5"
      }
   }
   */
   if (!json) {
      *err = "JSON is malformed.";
      return nullptr;
   }
   const auto& root = *json;

   const auto& file_format_version = root["file_format_version"].val();
   if (file_format_version != "\"1.0.0\"") {
      *err = "Bad file_format_version.";
      return nullptr;
   }

   const auto& icd_node = root["ICD"];
   std::string library_path, api_version;
   if (!icd_node["library_path"].as_string(&library_path) ||
       !icd_node["api_version"].as_string(&api_version))
   {
      *err = "Missing library_path/api_version strings.";
      return nullptr;
   }

   if (library_path.size() && library_path[0] == '.') {
      const auto json_dir = path_parent(json_path);
      library_path = path_concat(json_dir, library_path);
   }

   auto ret = std::make_unique<IcdInfo>();
   ret->json_path = json_path;
   ret->library_path = library_path;
   ret->vk_api_version = api_version;
   return ret;
}
