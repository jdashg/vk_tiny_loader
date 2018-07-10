#include "find_icds.h"

#include <fstream>
#include <iostream>
#include "tjson_cpp/tjson.h"

#ifdef __cpp_lib_filesystem
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <dirent.h>
#endif

#ifdef __APPLE__
#include "CoreFoundation/CoreFoundation.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#endif

#ifdef _WIN32
typedef std::wstring path_string;
#else
typedef std::string path_string;
#endif

static std::unique_ptr<std::vector<uint8_t>>
Read(const path_string& path, std::ios_base::openmode extra_flags = 0)
{
   const auto flags = std::ios_base::in | extra_flags;
   std::ifstream stream(path, flags);

   std::string err;
   auto bytes = ReadBytes(&stream, &err);

   if (err.size())
      return nullptr;
   return std::make_unique<std::vector<uint8_t>>(std::move(bytes));
}

static std::unique_ptr<std::vector<path_string>>
ListDir(const path_string& path_str)
{
   std::vector<path_string> ret;

#ifdef __cpp_lib_filesystem
   const auto path = fs::path(path_str);
   if (!fs::is_directory(path))
      return nullptr;

   const auto options = fs::directory_options::skip_permission_denied;
   for (const auto& f : fs::directory_iterator(path, options)) {
      const auto& subpath = f.path();
      ret.push_back(subpath.native());
   }
#else
   auto dir = opendir(path_str.c_str());
   if (!dir)
      return nullptr;

   while (true) {
      const auto entry = readdir(dir);
      if (!entry)
         break;
      const auto subpath = path_str + "/" + entry->d_name;
      ret.push_back(subpath);
   }
   closedir(dir);
#endif

   return std::make_unique<std::vector<path_string>>(std::move(ret));
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

   unique_ptr<RegNode> Open(const wchar_t* const subkey) const {
      HKEY subhandle;
      const auto res = RegOpenKeyExW(handle_, subkey, 0, KEY_READ, &subhandle);
      if (res != ERROR_SUCCESS)
         return nullptr;
      return new RegNode(subhandle, true);
   }

   std::vector<std::wstring> EnumSubkeys() const {
      std::vector<std::wstring> ret;
      wchar_t subkey_buff[255 + 1]; // Max key size is 255.
      for (uint32_t i; true; i++) {
         const auto res = RegEnumKeyExW(handle_, i, subkey_buff, nullptr, nullptr,
                                        nullptr, nullptr, nullptr);
         if (res != ERROR_SUCCESS)
            break;
         ret.push_back(std::wstring(subkey_buff));
      }
      return ret;
   }

   std::vector<uint8_t> GetValueBytes(const wchar_t* const subkey,
                                      const wchar_t* const name) const
   {
      uint32_t size = 0;
      auto res = RegGetValueW(handle_, subkey, name, RRF_RT_ANY, nullptr, nullptr, &size);
      if (res != ERROR_SUCCESS)
         return {};

      const auto ret = std::vector<uint8_t>(size);
      auto res = RegGetValueW(handle_, subkey, name, RRF_RT_ANY, nullptr, ret.data(), &size);
      if (res != ERROR_SUCCESS)
         return {};

      return ret;
   }
};

static const auto kRegPath = L"System\\CurrentControlSet\\Control\\Class";
static const auto kGuidDisplay = L"{4d36e968-e325-11ce-bfc1-08002be10318}";
static const auto kGuidSoftwareComponent = L"{5c4c3332-344d-483c-8739-259e934c9cc8}";
static const auto kLegacyRegPath = L"SOFTWARE\\Khronos\\Vulkan\\Drivers";

static std::vector<std::wstring>
LoadFromWindowsRegistry()
{
   const auto local_machine = RegNode(HKEY_LOCAL_MACHINE);
   std::vector<std::wstring> paths_from_reg;

   [&]() {
      const auto class_node = local_machine->Open(kRegClassPath);
      if (!class_node)
         return;

      const auto fn_for_guid = [&](const wchar_t* const guid) {
         const auto guid_node = class_node->Open(guid);
         if (!guid_node)
            return;

         const auto subkeys = guid_node->EnumSubkeys();
         for (const auto& k : subkeys) {
            if (k.length() != 4)
               continue;

            const auto bytes = guid_node->GetValueBytes(k.c_str(), L"VulkanDriverName");
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
      fn_for_guid(kGuidDisplay);
      fn_for_guid(kGuidSoftwareComponent);
   }();

   [&]() {
      const auto drivers_node = local_machine->Open(kLegacyRegPath);
      if (!drivers_node)
         return;

      std::vector<wchar_t> val_name_buff(32 * 1024);
      uint32_t val_val = -1;

      for (uint32_t i = 0; true; i++) {
         const auto res = RegEnumValueW(drivers_node.handle_, i, val_name_buff.data(),
                                        val_name_buff.size(), nullptr, nullptr, &val_val,
                                        sizeof(val_val));
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
SplitString(const std::string& str, const char delim)
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

std::vector<path_string>
EnumIcdDirs()
{
   std::vector<path_string> ret;

   // --

   [&]() {
      const auto env = getenv("VK_ICD_FILENAMES");
      if (!env)
         return;

      const auto paths = SplitString(env, ':');
      for (const auto& path : paths) {
         const auto conv = path_string(path.c_str(), path.c_str() + path.size());
         ret.push_back(conv);
      }
   }();

   // --

#ifdef _WIN32
   const auto wpaths = LoadFromWindowsRegistry();
   ret.insert(ret.end(), wpaths.begin(), wpaths.end());
#endif // _WIN32

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
      ret.push_back(path);
   }();
   ret.push_back("/etc/vulkan/icd.d");
   ret.push_back("/usr/local/share/vulkan/icd.d");


#endif // __APPLE__

#ifdef __linux__
   /* /usr/local/etc/vulkan/icd.d
    * /usr/local/share/vulkan/icd.d
    * /etc/vulkan/icd.d
    *
    * /usr/share/vulkan/icd.d
    * $HOME/.local/share/vulkan/icd.d
    */
   ret.push_back("/usr/local/etc/vulkan/icd.d");
   ret.push_back("/usr/local/share/vulkan/icd.d");
   ret.push_back("/etc/vulkan/icd.d");
#endif // __linux

#ifndef _WIN32
   ret.push_back("/usr/share/vulkan/icd.d");

   [&]() {
      const auto env = getenv("HOME");
      if (!env)
         return;

      const auto path = std::string(env) + "/.local/share/vulkan/icd.d";
      ret.push_back(path);
   }();
#endif // !_WIN32

   return ret;
}

template<typename T>
static bool
EndsWith(const T& str, const T& needle)
{
   if (str.size() < needle.size())
      return false;
   const auto tail = str.substr(str.size() - needle.size());
   return tail == needle;
}

static std::unique_ptr<IcdInfo>
ParseIcd(const std::vector<uint8_t>& bytes, std::string* const err)
{
   std::vector<std::string> errs;
   const auto json = tjson::read((const char*)bytes.data(),
                                 (const char*)bytes.data() + bytes.size(), &errs);
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

   auto ret = std::make_unique<IcdInfo>();
   ret->library_path = library_path;
   ret->api_version = SemanticVersion::Parse(api_version);
   return ret;
}

std::vector<IcdInfo>
EnumIcds()
{
   std::vector<IcdInfo> ret;

   const path_string kJsonExt(".json");
   const auto dirs = EnumIcdDirs();
   for (const auto& dir : dirs) {
      const auto files = ListDir(dir);
      if (!files)
         continue;

      for (const auto& file : *files) {
         if (!EndsWith(file, kJsonExt))
            continue;

         const auto read = Read(file, std::ios_base::binary);
         if (!read) {
            std::cout << "Failed to read " << file << "." << "\n";
            continue;
         }
         const auto& bytes = *read;

         std::string err;
         const auto info = ParseIcd(bytes, &err);
         if (!info) {
            std::cout << "Failed to parse ICD " << file << ": " << err << "\n";
            continue;
         }
         ret.push_back(*info);
      }
   }

   return ret;
}
