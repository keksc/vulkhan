#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
template <typename Owner> class AutoUpdateString {
public:
  // Constructors
  AutoUpdateString(Owner *owner) : owner(owner) {}
  AutoUpdateString(Owner *owner, const std::string &str)
      : owner(owner), data(str) {}
  AutoUpdateString(Owner *owner, const char *str) : owner(owner), data(str) {}
  AutoUpdateString(Owner *owner, size_t count, char ch)
      : owner(owner), data(count, ch) {}
  template <typename InputIt>
  AutoUpdateString(Owner *owner, InputIt first, InputIt last)
      : owner(owner), data(first, last) {}

  // Assignment
  AutoUpdateString &operator=(const std::string &newStr) {
    data = newStr;
    updateOwner();
    return *this;
  }
  AutoUpdateString &operator=(const char *newStr) {
    data = newStr;
    updateOwner();
    return *this;
  }
  operator std::string_view() const noexcept { return data; }
  operator std::string() const noexcept { return data; }
  operator std::filesystem::path() const noexcept { return data; }

  // Iterators
  auto begin() noexcept { return data.begin(); }
  auto end() noexcept { return data.end(); }
  auto begin() const noexcept { return data.begin(); }
  auto end() const noexcept { return data.end(); }
  auto cbegin() const noexcept { return data.cbegin(); }
  auto cend() const noexcept { return data.cend(); }
  auto rbegin() noexcept { return data.rbegin(); }
  auto rend() noexcept { return data.rend(); }
  auto rbegin() const noexcept { return data.rbegin(); }
  auto rend() const noexcept { return data.rend(); }
  auto crbegin() const noexcept { return data.crbegin(); }
  auto crend() const noexcept { return data.crend(); }

  // Access
  char &operator[](size_t i) {
    updateOwner();
    return data[i];
  }
  const char &operator[](size_t i) const { return data[i]; }
  char &at(size_t i) {
    updateOwner();
    return data.at(i);
  }
  const char &at(size_t i) const { return data.at(i); }

  const char *c_str() const noexcept { return data.c_str(); }
  const std::string &str() const { return data; }

  // Capacity
  size_t size() const noexcept { return data.size(); }
  bool empty() const noexcept { return data.empty(); }

  // Modifiers
  void clear() {
    data.clear();
    updateOwner();
  }
  void push_back(char c) {
    data.push_back(c);
    updateOwner();
  }
  void pop_back() {
    data.pop_back();
    updateOwner();
  }
  AutoUpdateString &operator+=(const std::string &rhs) {
    data += rhs;
    updateOwner();
    return *this;
  }
  AutoUpdateString &operator+=(const char *rhs) {
    data += rhs;
    updateOwner();
    return *this;
  }
  AutoUpdateString &operator+=(char c) {
    data += c;
    updateOwner();
    return *this;
  }

  AutoUpdateString &erase(size_t pos = 0, size_t len = std::string::npos) {
    data.erase(pos, len);
    updateOwner();
    return *this;
  }

  size_t find_last_of(const std::string &str,
                      size_t pos = std::string::npos) const noexcept {
    return data.find_last_of(str, pos);
  }

  size_t find_last_of(const char *s, size_t pos = std::string::npos) const {
    return data.find_last_of(s, pos);
  }

  size_t find_last_of(char c, size_t pos = std::string::npos) const noexcept {
    return data.find_last_of(c, pos);
  }

  size_t find(const std::string &s, size_t pos = 0) const {
    return data.find(s, pos);
  }
  size_t find(const char *s, size_t pos = 0) const { return data.find(s, pos); }
  size_t find(char c, size_t pos = 0) const { return data.find(c, pos); }
  std::string substr(size_t pos = 0, size_t len = std::string::npos) const {
    return data.substr(pos, len);
  }

  // Mutating example: replace
  AutoUpdateString &replace(size_t pos, size_t count, const std::string &str) {
    data.replace(pos, count, str);
    updateOwner();
    return *this;
  }

private:
  void updateOwner() {
    if (owner)
      owner->update();
  }

  Owner *owner;
  std::string data;
};

class Text : public Element {
public:
  Text(View &view, Element *parent, glm::vec2 position,
       const std::string &content = "");
  AutoUpdateString<Text> content;

protected:
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override;

private:
  void flushSize();
  void update();
  friend AutoUpdateString<Text>;
};
} // namespace hud
} // namespace vkh
