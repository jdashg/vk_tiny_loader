#include "utils.h"

#include <codecvt>
#include <fstream>
#include <locale>

size_t
next_pot(const size_t x)
{
   if (x < 2)
      return x + 1;

   const auto v = x - 1;
   return (~v) & (v << 1);
}

std::unique_ptr<std::vector<uint8_t>>
read_bytes(std::istream* const in, std::string* const out_err)
{
   std::vector<uint8_t> ret(1000*1000);
   uint64_t pos = 0;
   while (true) {
      in->read((char*)ret.data() + pos, ret.size() - pos);
      pos += in->gcount();
      if (in->good()) {
         ret.resize(ret.size() * 2);
         continue;
      }

      if (in->eof()) {
         ret.resize(pos);
         return std::make_unique<std::vector<uint8_t>>(std::move(ret));
      }

      *out_err = std::string("rdstate: ") + std::to_string(in->rdstate());
      return {};
   }
}

std::unique_ptr<std::vector<uint8_t>>
read_bytes(const std::string& path, std::string* const out_err,
           uint32_t extra_flags)
{
   const auto flags = std::ios_base::in | extra_flags;
   std::ifstream stream(path, flags);

   return read_bytes(&stream, out_err);
}

std::wstring
to_wstring(const std::string& in)
{
   std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
   return convert.from_bytes(in);
}

std::string
to_string(const std::wstring& in)
{
   std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
   return convert.to_bytes(in);
}

// -

bool
ends_with(const std::string& str, const std::string& needle)
{
   const auto pos = str.rfind(needle);
   return pos == str.size() - needle.size();
}

// -

static const char PATH_SEP =
#ifdef _WIN32
                             '\\';
#else
                             '/';
#endif

std::string
path_parent(const std::string& path)
{
   const auto sep = path.find_last_of(PATH_SEP);
   if (sep == std::string::npos)
      return "";

   return path.substr(0, sep);
}

std::string
path_concat(const std::string& a, const std::string& b)
{
   return a + PATH_SEP + b;
}
