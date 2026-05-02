#include "annparse.hpp"
#include <format>
#include <iostream>
#include <string>
#include <unordered_set>
#include <sysexits.h>

int AnnParseState::ParseBlank(const std::string &line, int line_number) {
  int rc_ = 0;
  size_t i = blank.size();
  const int page_old = page_;
  bool unused =
    (i < line.size()) && (line[i] == ':')
    && (iget(page_, line, i)
    && iget(x_, line, i)
    && iget(y_, line, i)
    && iget(width_, line, i)
    && iget(height_, line, i));
  (void)unused;
  if ((rc_ == 0) &&
      std::unordered_set<int>{page_, x_, y_, width_, height_}.contains(-1)) {
    std::cerr <<
      std::format("page={}, x={}, y={}, width={}, height={}\n",
        page_, x_, y_, width_, height_);
    rc_ = EX_CONFIG;
  }
  if ((rc_ == 0) && (page_old > page_)) {
    std::cerr << std::format("page decrease {} -> {}\n", page_old, page_);
    rc_ = EX_CONFIG;
  }
  IfFailMessage(line_number, line);
  return rc_;
}

int AnnParseState::ParseText(const std::string &line, int line_number) {
  int rc_ = 0;
  const int page_old = page_;
  size_t i = 0;
  bool unused =
    ((i < line.size()) && (line[i] == ':')
    && iget(page_, line, i)
    && iget(x_, line, i)
    && iget(y_, line, i)
    && sget(font_name_, line, i)
    && iget(font_size_, line, i)
    && sget(lang_, line, i)
    && sget(dir_, line, i));
  (void)unused;
  if ((rc_ == 0) &&
      ((std::unordered_set<int>{page_, x_, y_, font_size_}.contains(-1))
      || font_name_.empty()
      || ((lang_ != "en") && (lang_ != "he"))
      || ((dir_ != "ltr") && (dir_ != "rtl")))) {
    std::cerr <<
      std::format("page={}, x={} y={} font_name={} font_size={} ",
        page_, x_, y_, font_name_, font_size_) <<
      std::format("lang={}, dir={}\n", lang_, dir_);
    rc_ = EX_CONFIG;
  }
  if ((rc_ == 0) && (page_old > page_)) {
    std::cerr << std::format("page decrease {} -> {}\n", page_old, page_);
    rc_ = EX_CONFIG;
  }
  IfFailMessage(line_number, line);
  return rc_;
}

bool AnnParseState::iget(int &v, const std::string &line, size_t &i) {
  bool got = false;
  size_t j = i;
  while ((j < line.size()) && (line[j] != ':')) {
    ++j;
  }
  if (i < j) {
    int new_val;
    auto data = line.data();
    auto [ptr, ec] = std::from_chars(data + i, data + j, new_val);
    if ((ec == std::errc()) && (ptr == data + j)) {
      v = new_val;
      got = true;
    } else {
      std::cerr << std::format("iget failed {}{}{}\n",
        '"', line.substr(i, j - i), '"');
      rc_ = EX_CONFIG;
    }
    i = j;
  }
  return got;
}

bool AnnParseState::sget(std::string &v, const std::string &line, size_t &i) {
  bool got = false;
  size_t j = i;
  while ((j < line.size()) && (line[j] != ':')) {
    ++j;
  }
  if (i < j) {
    v = line.substr(i, j - i);
    i = j;
  }
  return got;
}

void AnnParseState::IfFailMessage(int line_number, const std::string &line) {
  if (rc_ != 0) {
    std::cerr << std::format("Missing/bad values in [{:4d}] {}\n",
      line_number, line);
  }
}
