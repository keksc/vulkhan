#pragma once

#include "element.hpp"
#include "eventListener.hpp"
#include "filePicker.hpp"
#include "line.hpp"
#include "rect.hpp"
#include "textInput.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

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

namespace vkh {
namespace hud {
class Canvas
    : public Element,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton>,
      public EventListener<&EngineContext::InputCallbackSystem::cursorPosition>,
      public EventListener<&EngineContext::InputCallbackSystem::key> {
public:
  Canvas(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         glm::vec3 bgColor)
      : Element(view, parent, position, size),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view,
            [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            view, [this](double xpos,
                         double ypos) { cursorPositionCallback(xpos, ypos); }),
        EventListener<&EngineContext::InputCallbackSystem::key>(
            view, [this](int key, int scancode, int action, int mods) {
              keyCallback(key, scancode, action, mods);
            }) {
    bg = addChild<hud::Rect>(glm::vec2{}, glm::vec2{1.f}, bgColor);
  }

  std::string serializeContent() {
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

    for (int i = 1; i < children.size(); i++) {
      const auto &child = children[i];

      glm::vec2 localPos = (child->position - position) / size;
      glm::vec2 localSize = child->size / size;

      if (auto textInput =
              std::dynamic_pointer_cast<vkh::hud::TextInput>(child)) {
        content.push_back('T');
        writeVec2(localPos);
        writeString(textInput->content);
      } else if (auto rect = std::dynamic_pointer_cast<vkh::hud::Rect>(child)) {
        content.push_back('R');
        writeVec2(localPos);
        writeVec2(localSize);
        writeVec3(rect->color);
      } else if (auto line = std::dynamic_pointer_cast<vkh::hud::Line>(child)) {
        content.push_back('L');
        writeVec2(localPos);
        writeVec2(localSize);
        writeVec3(line->color);
      }
    }

    return content;
  }

  void reset() { children.erase(children.begin(), children.end() - 1); }

  void deserializeContent(const std::string &data) {
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

    reset();

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
        glm::vec3 color = readVec3();
        addChild<hud::Rect>(pos, size, color);
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
  std::shared_ptr<FilePicker> filePicker;

private:
  void keyCallback(int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS)
      return;
    if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_O) {
      // std::println("{}", serializeContent());
      if (!filePicker) {
        filePicker =
            insertChild<FilePicker>(children.begin(), glm::vec2{.1f},
                                    glm::vec2{.8f}, FilePicker::Mode::Open);
      }
      return;
    }
    if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_S) {
      // std::println("{}", serializeContent());
      if (filePicker)
        return;
      filePicker =
          insertChild<FilePicker>(children.begin(), glm::vec2{.1f},
                                  glm::vec2{.8f}, FilePicker::Mode::Save);
      return;
    }
    if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_R) {
      reset();
    }
    // TODO: make the canvas unusable when the file picker is open
    if (key == GLFW_KEY_ENTER) {
      if (!filePicker)
        return;
      std::filesystem::path path = filePicker->path->content;
      if (filePicker->mode == FilePicker::Mode::Save) {
        std::ofstream out(path, std::ofstream::out | std::ofstream::trunc |
                                    std::ios::binary);
        out << serializeContent();
        out.close();
      } else {
        if (!std::filesystem::exists(path))
          throw std::runtime_error("path does not exist");
        std::ifstream in(path, std::ios::binary);
        std::string buffer((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
        deserializeContent(buffer);
        in.close();
      }
      children.erase(children.begin());
      filePicker = nullptr;
      return;
    }
    if (key == GLFW_KEY_ESCAPE) {
      if (!filePicker)
        return;
      children.erase(children.begin());
      filePicker = nullptr;
      return;
    }
    if (key == GLFW_KEY_ESCAPE)
      mode = Mode::Select;
    if (key == GLFW_KEY_T)
      mode = Mode::Text;
    if (key == GLFW_KEY_R)
      mode = Mode::Rect;
    if (key == GLFW_KEY_L)
      mode = Mode::Line;
  }
  void mouseButtonCallback(int button, int action, int mods) {
    const auto &cursorPos = view.context.input.cursorPos;
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);

    if (button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    if (action == GLFW_RELEASE) {
      elementBeingAdded = nullptr;
      return;
    }
    if (action != GLFW_PRESS)
      return;

    if (!(glm::all(glm::greaterThanEqual(cursorPos, min)) &&
          glm::all(glm::lessThanEqual(cursorPos, max))))
      return;

    switch (mode) {
    case Mode::Select:
      break;
    case Mode::Text:
      elementBeingAdded = insertChild<hud::TextInput>(
          children.begin(), (cursorPos - position) / size);
      break;
    case Mode::Rect:
      elementBeingAdded = insertChild<hud::Rect>(children.begin(),
                                                 (cursorPos - position) / size,
                                                 glm::vec2{}, glm::vec3{1.f});
      break;
    case Mode::Line:
      elementBeingAdded = insertChild<hud::Line>(children.begin(),
                                                 (cursorPos - position) / size,
                                                 glm::vec2{}, glm::vec3{1.f});
      break;
    }
  }
  void cursorPositionCallback(double xpos, double ypos) {
    if (!elementBeingAdded)
      return;
    const auto &cursorPos = view.context.input.cursorPos;

    elementBeingAdded->size = cursorPos - elementBeingAdded->position;
  }
  std::shared_ptr<Element> elementBeingAdded;
  std::shared_ptr<hud::Rect> bg;
  enum Mode { Select, Text, Rect, Line } mode{Mode::Select};
};
} // namespace hud
} // namespace vkh
