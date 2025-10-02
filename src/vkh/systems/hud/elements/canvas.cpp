#include "canvas.hpp"

#include <format>
#include <mutex>

#include "textInput.hpp"
#include "line.hpp"

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
Canvas::Canvas(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
               decltype(Rect::imageIndex) bgImageIndex)
    : Element(view, parent, position, size) {
  modeText = addChild<hud::Text>(glm::vec2{}, "mode: Select");
  modeBg = addChild<hud::Rect>(glm::vec2{}, glm::vec2{1.f}, 0);
  modeBg->size = modeText->size;
  bg = addChild<hud::Rect>(glm::vec2{}, glm::vec2{1.f}, 0);
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

  for (int i = 1; i < children.size(); i++) {
    const auto &child = children[i];

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
void Canvas::reset() { children.erase(children.begin(), children.end() - 1); }
void Canvas::loadFromFile(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path))
    throw std::runtime_error("path does not exist");

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
bool Canvas::handleKey(int key, int scancode, int action, int mods) {
  if (action != GLFW_PRESS)
    return false;
  if (key == GLFW_KEY_ENTER) {
    if (!filePicker)
      return false;
    std::filesystem::path path = filePicker->path->content;
    if (filePicker->mode == FilePicker::Mode::Save) {
      saveToFile(path);
    } else {
      loadFromFile(path);
    }
    children.erase(children.begin());
    filePicker = nullptr;
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_O) {
    if (!filePicker) {
      filePicker =
          insertChild<FilePicker>(children.begin(), glm::vec2{.1f},
                                  glm::vec2{.8f}, FilePicker::Mode::Open);
    }
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_S) {
    if (filePicker)
      return true;
    filePicker =
        insertChild<FilePicker>(children.begin(), glm::vec2{.1f},
                                glm::vec2{.8f}, FilePicker::Mode::Save);
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_R) {
    reset();
    return true;
  }
  if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_N) {
    if (fileBtnsBeginning) {
      auto it = std::find(children.begin(), children.end(), fileBtnsBeginning);
      children.erase(it, it + fileBtnsSize);
      fileBtnsBeginning = nullptr;
      fileBtnsSize = 0;
    }
    std::once_flag once;
    for (const auto &entry : std::filesystem::directory_iterator(".")) {
      auto pathStr = std::filesystem::relative(entry.path()).string();
      auto fileBtn = addChild<Button>(
          glm::vec2{0.f, .1f * fileBtnsSize++}, glm::vec2{2.f}, 0,
          [&](int button, int action, int) {
            if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
              loadFromFile(pathStr);
            }
          },
          pathStr);
      std::call_once(once, [&]() { fileBtnsBeginning = fileBtn; });
    }
    return true;
  }
  if (key == GLFW_KEY_ESCAPE) {
    if (filePicker) {
      children.erase(children.begin());
      filePicker = nullptr;
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
  return true;
}
bool Canvas::handleMouseButton(int button, int action, int mods) {
  const auto &cursorPos = view.context.input.cursorPos;
  if (action != GLFW_PRESS || button != GLFW_MOUSE_BUTTON_LEFT || filePicker ||
      !isCursorInside())
    return false;

  switch (mode) {
  case Mode::Select:
    break;
  case Mode::Text:
    elementBeingAdded = insertChild<hud::TextInput>(
        children.begin(), (cursorPos - position) / size);
    break;
  case Mode::Rect:
    elementBeingAdded = insertChild<hud::Rect>(
        children.begin(), (cursorPos - position) / size, glm::vec2{}, 0);
    break;
  case Mode::Line:
    elementBeingAdded =
        insertChild<hud::Line>(children.begin(), (cursorPos - position) / size,
                               glm::vec2{}, glm::vec3{1.f});
    break;
  }
  return true;
}
bool Canvas::handleCursorPosition(double xpos, double ypos) {
  if (!elementBeingAdded)
    return false;
  const auto &cursorPos = view.context.input.cursorPos;
  if (!std::dynamic_pointer_cast<vkh::hud::Text>(elementBeingAdded))
    elementBeingAdded->size = cursorPos - elementBeingAdded->position;
  return true;
}
bool Canvas::handleDrop(int count, const char **paths) {
  // int i;
  // for (i = 0;  i < count;  i++)
  //     handleDTroppedFile(paths[i]);
  loadFromFile(paths[1]);
  return true;
}
} // namespace vkh::hud
