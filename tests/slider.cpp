#include "../src/UI/sliderMath.hpp"
#include <cassert>
#include <iostream>
int main() {
  using UI::sliderValue;
  assert(sliderValue(0, 70, 120, 1) == 70);
  assert(sliderValue(1, 70, 120, 1) == 120);
  assert(sliderValue(.46f, 70, 120, 1) == 93);
  assert(sliderValue(-10, 70, 120, 1) == 70);
  assert(sliderValue(10, 70, 120, 1) == 120);
  assert(sliderValue(.45f, 0, 150, 1) == 68);
  assert(sliderValue(.5f, 10, 10, 1) == 10);
  assert(sliderValue(.53f, 0, 100, 10) == 50);
  assert(sliderValue(.375f, 0, 1, 0) == .375f);
  std::cout << "Slider range, stepping and drag endpoint checks passed\n";
}
