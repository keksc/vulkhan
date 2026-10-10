#pragma once
#include <algorithm>
#include <cmath>
#include <string_view>

namespace UI::editing {
inline size_t lineStart(std::string_view text, size_t pos) {
  pos = std::min(pos, text.size());
  const auto previous = pos ? text.rfind('\n', pos - 1) : text.npos;
  return previous == text.npos ? 0 : previous + 1;
}
inline size_t lineEnd(std::string_view text, size_t pos) {
  const auto next = text.find('\n', std::min(pos, text.size()));
  return next == text.npos ? text.size() : next;
}
inline size_t previousWord(std::string_view text, size_t pos) {
  pos = std::min(pos, text.size());
  const auto space = [](char c) { return c == ' ' || c == '\n' || c == '\t'; };
  while (pos && space(text[pos - 1]))
    --pos;
  while (pos && !space(text[pos - 1]))
    --pos;
  return pos;
}
template <typename Advance>
size_t hit(std::string_view text, float x, float y, float height,
           Advance advance) {
  size_t start = 0;
  int line =
      std::max(0, static_cast<int>(std::floor(y / std::max(height, .00001f))));
  while (line-- > 0) {
    const auto end = lineEnd(text, start);
    if (end == text.size())
      return end;
    start = end + 1;
  }
  float offset = 0;
  const auto end = lineEnd(text, start);
  for (size_t i = start; i < end; ++i) {
    const float width = advance(text[i]);
    if (x < offset + width / 2.f)
      return i;
    offset += width;
  }
  return end;
}
} // namespace UI::editing
