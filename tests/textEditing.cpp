#include "../src/UI/textEditing.hpp"
#include <cassert>
#include <iostream>

int main() {
  using namespace UI::editing;
  const auto advance = [](char) { return 10.f; };
  assert(hit("", 0, 0, 20, advance) == 0);
  assert(hit("abc", 4, 0, 20, advance) == 0);
  assert(hit("abc", 6, 0, 20, advance) == 1);
  assert(hit("abc", 100, 0, 20, advance) == 3);
  assert(hit("abc\nde", 16, 25, 20, advance) == 6);
  assert(hit("abc\n\nde", 10, 21, 20, advance) == 4);
  assert(hit("abc\n", 10, 40, 20, advance) == 4);
  assert(hit("abc", -10, -10, 20, advance) == 0);
  assert(lineStart("abc\nde", 4) == 4);
  assert(lineEnd("abc\nde", 4) == 6);
  assert(lineStart("", 0) == 0);
  assert(previousWord("hello world", 11) == 6);
  assert(previousWord("hello \n", 7) == 0);
  assert(previousWord("hello", 5) == 0);
  // Same caret hit after uniform resize of font metrics and cursor offsets.
  assert(hit("abc\nde", 8, 12.5f, 10, [](char) { return 5.f; }) == 6);
  std::cout << "Text editing checks passed\n";
}
