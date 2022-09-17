#ifndef SJEF_LIB_UTIL_UTIL_H_
#define SJEF_LIB_UTIL_UTIL_H_
#include <ostream>

namespace sjef::util {

inline std::vector<std::string> splitString(const std::string& input, char c = ' ', char quote = '\'') {
  std::vector<std::string> result;
  const char* str0 = strdup(input.c_str());
  const char* str = str0;
  do {
    while (*str == c && *str)
      ++str;
    const char* begin = str;
    while (*str && (*str != c || (*begin == quote && str > begin && *(str - 1) != quote)))
      ++str;
    if (*begin == quote && str > begin + 1 && *(str - 1) == quote)
      result.emplace_back(begin + 1, str - 1);
    else
      result.emplace_back(begin, str);
    if (result.back().empty())
      result.pop_back();
  } while (0 != *str++);
  free((void*)str0);
  return result;
}

template <class T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v) {
  for (const auto& e : v) os << e <<" ";
  return os;
}

template <class T>
std::ostream& operator<<(std::ostream& os, const std::set<T>& v) {
  for (const auto& e : v) os << e <<" ";
  return os;
}


} // namespace sjef::util
#endif // SJEF_LIB_UTIL_UTIL_H_
