#pragma once
#include <array>
#include <string>
#include <string_view>

constexpr std::string_view blank{"blank"};

class AnnParseState {
 public:
  int ParseBlank(const std::string &line, int line_number);
  int ParseText(const std::string &line, int line_number);
  std::array<int, 2> xy() const { return {x_, y_}; }
  int page_{-1};
  int x_{-1};
  int y_{-1};
  int width_{-1};
  int height_{-1};
  std::string font_name_;
  int font_size_{-1};
  std::string lang_{"en"};
  std::string dir_{"ltr"}; // or rtl
 private:
  bool iget(int &v, const std::string &line, size_t &i);
  bool sget(std::string &v, const std::string &line, size_t &i);
  void IfFailMessage(int line_number, const std::string &line);
  int rc_{0};
};
