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
    flushSizeInOwner();
    return *this;
  }
  AutoUpdateString &operator=(const char *newStr) {
    data = newStr;
    flushSizeInOwner();
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
    flushSizeInOwner();
    return data[i];
  }
  const char &operator[](size_t i) const { return data[i]; }
  char &at(size_t i) {
    flushSizeInOwner();
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
    flushSizeInOwner();
  }
  void push_back(char c) {
    data.push_back(c);
    flushSizeInOwner();
  }
  void pop_back() {
    data.pop_back();
    flushSizeInOwner();
  }
  AutoUpdateString &operator+=(const std::string &rhs) {
    data += rhs;
    flushSizeInOwner();
    return *this;
  }
  AutoUpdateString &operator+=(const char *rhs) {
    data += rhs;
    flushSizeInOwner();
    return *this;
  }
  AutoUpdateString &operator+=(char c) {
    data += c;
    flushSizeInOwner();
    return *this;
  }

  // Forward read-only operations directly
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
    flushSizeInOwner();
    return *this;
  }

private:
  void flushSizeInOwner() {
    if (owner)
      owner->flushSize();
  }

  Owner *owner;
  std::string data;
};

class Text : public Element {
public:
  Text(View &view, Element *parent, glm::vec2 position,
       const std::string &content = "")
      : Element(view, parent, position, {}), content{this, content} {
    flushSize();
  }
  AutoUpdateString<Text> content;

protected:
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override {
    glm::vec2 cursor = position;
    float maxX = cursor.x;

    float maxSizeY = TextSys::glyphRange.maxSizeY;
    for (auto &c : content) {
      if (c == '\n') {
        cursor.x = position.x;
        cursor.y += maxSizeY;
        continue;
      }
      if (!TextSys::glyphRange.glyphs.count(c))
        continue;
      uint32_t baseIndex = static_cast<uint32_t>(drawInfo.textVertices.size());
      auto &ch = TextSys::glyphRange.glyphs[c];

      // Calculate vertex positions for this character
      float x0 = cursor.x + ch.offset.x;
      float x1 = x0 + ch.size.x;
      float y0 = maxSizeY + cursor.y + ch.offset.y;
      float y1 = y0 + ch.size.y;

      // Add four vertices for this character
      drawInfo.textVertices.emplace_back(
          glm::vec3{x0, y0, depth}, glm::vec2{ch.uvOffset.x, ch.uvOffset.y});
      drawInfo.textVertices.emplace_back(
          glm::vec3{x1, y0, depth},
          glm::vec2{ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y});
      drawInfo.textVertices.emplace_back(
          glm::vec3{x1, y1, depth}, glm::vec2{ch.uvOffset.x + ch.uvExtent.x,
                                              ch.uvOffset.y + ch.uvExtent.y});
      drawInfo.textVertices.emplace_back(
          glm::vec3{x0, y1, depth},
          glm::vec2{ch.uvOffset.x, ch.uvOffset.y + ch.uvExtent.y});

      // Add indices for this character's quad
      drawInfo.textIndices.emplace_back(baseIndex + 0);
      drawInfo.textIndices.emplace_back(baseIndex + 1);
      drawInfo.textIndices.emplace_back(baseIndex + 2);
      drawInfo.textIndices.emplace_back(baseIndex + 0);
      drawInfo.textIndices.emplace_back(baseIndex + 2);
      drawInfo.textIndices.emplace_back(baseIndex + 3);

      // Move cursor to next character position
      cursor.x += ch.advance;
      maxX = glm::max(cursor.x, maxX);
    }
    size = glm::vec2{maxX, cursor.y + maxSizeY} - position;
  }

private:
  void flushSize() {
    float maxSizeY = TextSys::glyphRange.maxSizeY;
    size = glm::vec2{0.f, maxSizeY};
    float maxX = 0.f;

    for (auto &c : content) {
      if (c == '\n') {
        maxX = glm::max(size.x, maxX);
        size.x = 0.f;
        size.y += maxSizeY;
        continue;
      }
      if (!TextSys::glyphRange.glyphs.count(c))
        continue;
      auto &ch = TextSys::glyphRange.glyphs[c];

      size.x += ch.advance;
    }
    size.x = glm::max(size.x, maxX);
  }
  friend AutoUpdateString<Text>;
};
} // namespace hud
} // namespace vkh
