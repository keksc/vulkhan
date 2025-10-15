#include "canvas.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <print>
#include <ranges>

#include "line.hpp"
#include "emptyRect.hpp"
#include "textInput.hpp"

template <> struct std::formatter<glm::vec3> : std::formatter<std::string> {
  auto format(const glm::vec3 &vec3, format_context &ctx) const {
    return std::formatter<std::string>::format(
        std::format("{}, {}, {}", vec3.x, vec3.y, vec3.z), ctx);
  }
};
template <> struct std::formatter<glm::vec2> : std::formatter<std::string> {
  auto format(const glm::vec2 &vec2, format_context &ctx) const {
    return std::formatter<std::string>::format(
        std::format("{}, {}", vec2.x, vec2.y), ctx);
  }
};

namespace vkh::hud {
void Canvas::initBaseElements() {
  filePicker = nullptr;
  selectedElement = nullptr;
  modeBg = addChild<hud::Rect>(glm::vec2{}, glm::vec2{1.f}, 0);
  modeText = addChild<hud::Text>(glm::vec2{}, "mode: Select");
  modeBg->size = modeText->size;
}
Canvas::Canvas(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
               decltype(Rect::imageIndex) bgImageIndex)
    : hud::Rect(view, parent, position, size, bgImageIndex) {
  initBaseElements();
}
void Canvas::reset() {
  children.clear();
  initBaseElements();
}
bool Canvas::handleKey(int key, int scancode, int action, int mods) {
  if (action != GLFW_PRESS)
    return false;
  if (key == GLFW_KEY_DELETE) {
    auto it = std::find(children.begin(), children.end(), selectedElement);
    if (it != children.end()) {
      children.erase(it);
    }
    selectedElement = nullptr;
  }
  if (key == GLFW_KEY_ENTER) {
    if (!filePicker)
      return false;
    std::filesystem::path path = filePicker->path->content;
    if (filePicker->mode == FilePicker::Mode::Save) {
      saveToFile(path);
      auto it = std::find(children.begin(), children.end(), filePicker);
      if (it != children.end()) {
        children.erase(it);
      }
      filePicker = nullptr;
    } else {
      loadFromFile(
          path); // reset() nullptrs and removes filePicker automatically
    }
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_O) {
    if (!filePicker) {
      filePicker = addChild<FilePicker>(glm::vec2{.1f}, glm::vec2{.8f},
                                        FilePicker::Mode::Open);
    }
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_S) {
    if (filePicker)
      return false;
    filePicker = addChild<FilePicker>(glm::vec2{.1f}, glm::vec2{.8f},
                                      FilePicker::Mode::Save);
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_R) {
    reset();
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_N) {
    if (!fileBtns.empty()) {
      removeFileBtns();
      return true;
    }

    unsigned int i = 0;
    const float spacing = 0.05f;
    for (const auto &entry : std::filesystem::directory_iterator(".")) {
      if (!std::filesystem::is_regular_file(entry))
        continue;

      auto pathStr = std::filesystem::relative(entry.path()).string();

      auto fileBtn = addChild<Button>(
          glm::vec2{0.f, spacing * i++}, glm::vec2{spacing}, 0,
          [&, pathStr](int button, int action, int) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
              loadFromFile(pathStr); // will call reset()
              fileBtns.clear();
            }
          },
          pathStr);

      fileBtns.push_back(fileBtn);
    }
    return true;
  }
  if (key == GLFW_KEY_ESCAPE) {
    if (filePicker) {
      auto it = std::find(children.begin(), children.end(), filePicker);
      if (it != children.end()) {
        children.erase(it);
      }
      filePicker = nullptr;
      return true;
    }
    if (!fileBtns.empty()) {
      removeFileBtns();
      return true;
    }
    if (mode == Mode::Select) {
      return false;
    }
    mode = Mode::Select;
    modeText->content = "mode: Select";
    modeBg->size = modeText->size;
    return true;
  }
  if (key == input::keybinds[input::PlaceText]) {
    mode = Mode::Text;
    modeText->content = "mode: Text";
    modeBg->size = modeText->size;
    return true;
  }
  if (key == input::keybinds[input::PlaceRect]) {
    mode = Mode::Rect;
    modeText->content = "mode: Rect";
    modeBg->size = modeText->size;
    return true;
  }
  if (key == input::keybinds[input::PlaceLine]) {
    mode = Mode::Line;
    modeText->content = "mode: Line";
    modeBg->size = modeText->size;
    return true;
  }
  return false;
}
void Canvas::removeFileBtns() {
  children.erase(
      std::remove_if(
          children.begin(), children.end(),
          [&](const auto &child) {
            return std::find_if(fileBtns.begin(), fileBtns.end(),
                                [&](const auto &btn) {
                                  return btn.get() == child.get();
                                }) !=
                   fileBtns.end(); // Comparing smart pointers doesnt work
                                   // because fileBtns holds pointers to Buttons
                                   // and children pointers to Elements, and
                                   // sharer_ptr's operator= cares about that
          }),
      children.end());
  fileBtns.clear();
}
bool Canvas::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || !isCursorInside())
    return false;

  const auto &cursorPos = view.context.input.cursorPos;

  if (action == GLFW_PRESS) {
    // if (elementBeingAdded) {
    //   elementBeingAdded = nullptr;
    //   return true;
    // }

    glm::vec2 newPos = (cursorPos - position) / size;
    glm::vec2 newSize{};
    switch (mode) {
    case Mode::Select:
      for (auto &child : children) {
        if (child == modeBg || child == modeText || child == selectIndicator)
          continue;
        if (child->isCursorInside()) {
          selectedElement = child;
          break;
        }
      }
      break;
    case Mode::Text:
      selectedElement = addChild<hud::TextInput>(newPos, "", true);
      if (!selectIndicator)
        selectIndicator = addChild<hud::EmptyRect>(newPos, newSize);
      else {
        auto it = std::find(children.begin(), children.end(), selectIndicator);
        if (it != children.end()) {
          std::rotate(it, it + 1, children.end());
        }
        selectIndicator->position = cursorPos;
        selectIndicator->size = {};
      }
      break;
    case Mode::Rect:
      selectedElement = addChild<hud::Rect>(newPos, newSize, 0);
      if (!selectIndicator)
        selectIndicator = addChild<hud::EmptyRect>(newPos, newSize);
      else {
        auto it = std::find(children.begin(), children.end(), selectIndicator);
        if (it != children.end()) {
          std::rotate(it, it + 1, children.end());
        }
        selectIndicator->position = cursorPos;
        selectIndicator->size = {};
      }
      break;
    case Mode::Line:
      selectedElement = addChild<hud::Line>(newPos, newSize, glm::vec3{1.f});
      if (!selectIndicator)
        selectIndicator = addChild<hud::EmptyRect>(newPos, newSize);
      else {
        auto it = std::find(children.begin(), children.end(), selectIndicator);
        if (it != children.end()) {
          std::rotate(it, it + 1, children.end());
        }
        selectIndicator->position = cursorPos;
        selectIndicator->size = {};
      }
      break;
    }
    return true;
  } else if (action == GLFW_RELEASE) {
    if (selectedElement) {
      selectedElement = nullptr;
      return true;
    }
  }
  return false;
}
bool Canvas::handleCursorPosition(double xpos, double ypos) {
  if (!isCursorInside())
    return false;
  if (selectedElement) {
    const auto &cursorPos = view.context.input.cursorPos;
    if (!std::dynamic_pointer_cast<vkh::hud::TextInput>(selectedElement)) {
      selectedElement->size = cursorPos - selectedElement->position;
      selectIndicator->size = cursorPos - selectIndicator->position;
      return true;
    }
  }
  std::shared_ptr<Element> hoveredChild = nullptr;
  for (auto &child : children | std::views::reverse) {
    if (child == modeBg || child == modeText || child == selectIndicator)
      continue;
    if (child->isCursorInside()) {
      hoveredChild = child;
      break;
    }
  }
  if (hoveredChild) {
    auto it = std::find(children.begin(), children.end(), selectIndicator);
    if (it != children.end()) {
      std::rotate(it, it + 1, children.end());
    }
    selectIndicator->position = hoveredChild->position;
    selectIndicator->size = hoveredChild->size;
    return true;
  }
  return true;
}
bool Canvas::handleDrop(int count, const char **paths) {
  // int i;
  // for (i = 0;  i < count;  i++)
  //     handleDTroppedFile(paths[i]);
  loadFromFile(paths[0]);
  return true;
}
void Canvas::saveToFile(const std::filesystem::path &path) {
  std::string content;

  auto writeBinary = [&content](auto value) {
    const char *data = reinterpret_cast<const char *>(&value);
    content.append(data, sizeof(value));
  };

  auto writeVec2 = [&content, &writeBinary](const glm::vec2 &vec) {
    writeBinary(vec.x);
    writeBinary(vec.y);
  };

  auto writeVec3 = [&content, &writeBinary](const glm::vec3 &vec) {
    writeBinary(vec.x);
    writeBinary(vec.y);
    writeBinary(vec.z);
  };

  auto writeString = [&content](const std::string &str) {
    content.append(str);
    content.push_back('\0');
  };

  for (auto &child : children) {
    if (child == modeBg || child == modeText || child == selectIndicator)
      continue;

    glm::vec2 localPos = (child->position - position) / size;
    glm::vec2 localSize = child->size / size;

    if (auto textInput =
            std::dynamic_pointer_cast<vkh::hud::TextInput>(child)) {
      content.push_back('T');
      writeVec2(localPos);
      writeString(textInput->getContent());
    } else if (auto rect = std::dynamic_pointer_cast<vkh::hud::Rect>(child)) {
      content.push_back('R');
      writeVec2(localPos);
      writeVec2(localSize);
      writeBinary(rect->imageIndex);
    } else if (auto line = std::dynamic_pointer_cast<vkh::hud::Line>(child)) {
      content.push_back('L');
      writeVec2(localPos);
      writeVec2(localSize);
      writeVec3(line->color);
    }
  }

  std::ofstream out(path, std::ofstream::out | std::ofstream::trunc |
                              std::ios::binary);
  out << content;
  out.close();
}
void Canvas::loadFromFile(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path))
    throw std::runtime_error("path does not exist");
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("path is not a regular file");

  reset();

  std::ifstream in(path, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  in.close();
  size_t offset = 0;

  auto readBinary = [&]<typename T>() -> T {
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
  };

  auto readVec2 = [&]() -> glm::vec2 {
    glm::vec2 vec;
    vec.x = readBinary.operator()<float>();
    vec.y = readBinary.operator()<float>();
    return vec;
  };

  auto readVec3 = [&]() -> glm::vec3 {
    glm::vec3 vec;
    vec.x = readBinary.operator()<float>();
    vec.y = readBinary.operator()<float>();
    vec.z = readBinary.operator()<float>();
    return vec;
  };

  auto readString = [&]() -> std::string {
    size_t start = offset;
    while (offset < data.size() && data[offset] != '\0') {
      ++offset;
    }
    if (offset >= data.size()) {
      throw std::runtime_error("Null-terminated string not found");
    }
    std::string str = std::string(data.data() + start, offset - start);
    ++offset; // Skip the null terminator
    return str;
  };

  while (offset < data.size()) {
    char type = data[offset++];
    switch (type) {
    case 'T': {
      glm::vec2 pos = readVec2();
      std::string text = readString();
      auto input = addChild<hud::TextInput>(pos, text);
      break;
    }
    case 'R': {
      glm::vec2 pos = readVec2();
      glm::vec2 size = readVec2();
      using ImageIndexType = decltype(Rect::imageIndex);
      ImageIndexType imageIndex = readBinary.operator()<ImageIndexType>();
      addChild<hud::Rect>(pos, size, imageIndex);
      break;
    }
    case 'L': {
      glm::vec2 pos = readVec2();
      glm::vec2 size = readVec2();
      glm::vec3 color = readVec3();
      addChild<hud::Line>(pos, size, color);
      break;
    }
    default:
      std::println("Unknown element type '{}', skipping", type);
      return;
    }
  }
}
} // namespace vkh::hud
