#ifndef UTILS_H
#define UTILS_H

#include <iosfwd>
#include <string>
#include <vector>

size_t NextPOT(size_t x);

std::vector<uint8_t> ReadBytes(std::istream* in, std::string* out_err);

// -

class SemanticVersion final
{
   std::vector<uint64_t> parts;

public:
   static SemanticVersion Parse(const std::string& str) {
      SemanticVersion ret;

      auto itr = str.begin();
      const auto end = str.end();
      auto part_begin = itr;

      const auto fn_add = [&]() {
         const auto part = std::string(part_begin, itr);
         const auto val = stoull(part);
         ret.parts.push_back(val);
      };

      for (; itr != end; ++itr) {
         if (*itr != '.')
            continue;
         fn_add();
         part_begin = itr + 1;
      }
      fn_add();

      return ret;
   }

   int compare(const SemanticVersion& rhs) const {
      const auto size = std::max(parts.size(), rhs.parts.size());
      auto l_parts = parts;
      auto r_parts = rhs.parts;
      l_parts.resize(size, 0);
      r_parts.resize(size, 0);
      const auto mismatch = std::mismatch(l_parts.begin(), l_parts.end(),
                                          r_parts.begin());
      if (mismatch.first == l_parts.end())
         return 0;
      if (*(mismatch.first) < *(mismatch.second))
         return -1;
      else
         return 1;
   }

   bool operator<(const SemanticVersion& rhs) const {
      return compare(rhs) == -1;
   }

   bool operator==(const SemanticVersion& rhs) const {
      return compare(rhs) == 0;
   }

   std::string str() const {
      auto itr = parts.begin();
      if (itr == parts.end())
         return "";

      auto ret = std::to_string(*itr);
      ++itr;
      for (; itr != parts.end(); ++itr) {
         ret += ".";
         ret += std::to_string(*itr);
      }
      return ret;
   }
};

#endif // UTILS_H
