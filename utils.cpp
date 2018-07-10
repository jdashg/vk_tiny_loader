#include "utils.h"

#include <codecvt>
#include <iostream>
#include <locale>

size_t
NextPOT(const size_t x)
{
   if (x < 2)
      return x + 1;

   const auto v = x - 1;
   return (~v) & (v << 1);
}

std::vector<uint8_t>
ReadBytes(std::istream* const in, std::string* const out_err)
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
         return ret;
      }

      *out_err = std::string("rdstate: ") + std::to_string(in->rdstate());
      return {};
   }
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
