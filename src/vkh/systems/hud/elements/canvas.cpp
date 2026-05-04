#include "canvas.hpp"

#include <stb/stb_image.h>

#include "../hud.hpp"
#include "button.hpp"
#include "clipboardImage.hpp"
#include "emptyRect.hpp"
#include "filePicker.hpp"
#include "line.hpp"
#include "text.hpp"
#include "textInput.hpp"
#include "view.hpp"

#include "polyLine.hpp"

#include <ranges>

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
  modeBg = addChild<hud::RectImg>(glm::vec2{}, glm::vec2{1.f}, 0);
  modeText = addChild<hud::Text>(glm::vec2{}, "mode: Select");
  modeBg->size = modeText->size;
}
Canvas::Canvas(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
               decltype(RectImg::imageIndex) bgImageIndex)
    : hud::RectImg(view, parent, position, size, bgImageIndex) {
  initBaseElements();
}
void Canvas::reset() {
  children.clear();
  initBaseElements();
}
bool Canvas::handleKey(int key, int scancode, int action, int mods) {
  if (action != GLFW_PRESS)
    return false;
  if (key == GLFW_KEY_V && mods == GLFW_MOD_CONTROL) {
    // Try to get an image from the system clipboard
    std::vector<unsigned char> pngData = GetClipboardImagePNGData();
    int w, h, texChannels;

    if (stbi_info_from_memory(pngData.data(), pngData.size(), &w, &h,
                              &texChannels)) {
      // TODO: use hashes to prevent loading the same image twice
      auto texIndex = view.hudSys.solidColorSys.addTextureFromPNGMemory(
          pngData.data(), pngData.size());
      const auto &cursorPos = view.context.input.cursorPos;
      glm::vec2 newPos = (cursorPos - position) / size;
      glm::vec2 newSize{glm::vec2{w, h} /
                        static_cast<glm::vec2>(view.context.window.size)};
      addChild<hud::RectImg>(newPos, newSize, texIndex);
    } else {
      const auto &cursorPos = view.context.input.cursorPos;
      glm::vec2 newPos = (cursorPos - position) / size;
      addChild<hud::TextInput>(newPos, glfwGetClipboardString(NULL), false);
    }
    return true;
  }
  if (key == GLFW_KEY_DELETE) {
    if (selectedElement) {
      auto it = std::find(children.begin(), children.end(), selectedElement);
      if (it != children.end()) {
        children.erase(it);
      }
      if (selectIndicator) {
        auto sit = std::find(children.begin(), children.end(), selectIndicator);
        if (sit != children.end()) {
          children.erase(sit);
        }
        selectIndicator = nullptr;
      }
      selectedElement = nullptr;
      return true;
    }
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
  if (key == input::keybinds[input::Action::PlaceText]) {
    mode = Mode::Text;
    modeText->content = "mode: Text";
    modeBg->size = modeText->size;
    return true;
  }
  if (key == input::keybinds[input::Action::PlaceRect]) {
    mode = Mode::Rect;
    modeText->content = "mode: Rect";
    modeBg->size = modeText->size;
    return true;
  }
  if (key == input::keybinds[input::Action::PlaceLine]) {
    mode = Mode::Line;
    modeText->content = "mode: Line";
    modeBg->size = modeText->size;
    return true;
  }
  if (key == input::keybinds[input::Action::PlaceFreehand]) {
    mode = Mode::Freehand;
    modeText->content = "mode: Freehand";
    modeBg->size = modeText->size;
    return true;
  }
  if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
    std::vector<glm::vec3> colors = {
        {1.f, 1.f, 1.f}, {1.f, 0.f, 0.f},    {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f}, {1.f, 1.f, 0.f},    {1.f, 0.f, 1.f},
        {0.f, 1.f, 1.f}, {0.5f, 0.5f, 0.5f}, {0.f, 0.f, 0.f}};
    currentColor = colors[key - GLFW_KEY_1];
    modeBg->imageIndex = 0; // Assuming 0 is solid color
    return true;
  }
  if (key == GLFW_KEY_RIGHT_BRACKET) { // Bring to front
    auto it = std::find(children.begin(), children.end(), selectedElement);
    if (it != children.end()) {
      std::rotate(it, it + 1, children.end());
      // Ensure selectIndicator stays on top
      auto sit = std::find(children.begin(), children.end(), selectIndicator);
      if (sit != children.end()) {
        std::rotate(sit, sit + 1, children.end());
      }
    }
    return true;
  }
  if (key == GLFW_KEY_LEFT_BRACKET) { // Send to back
    auto it = std::find(children.begin(), children.end(), selectedElement);
    if (it != children.end()) {
      // Don't move behind modeBg and modeText (first 2 elements)
      if (it > children.begin() + 2) {
        std::rotate(children.begin() + 2, it, it + 1);
      }
    }
    return true;
  }
  return false;
}
void Canvas::removeFileBtns() {
  children.erase(std::remove_if(children.begin(), children.end(),
                                [&](const auto &child) {
                                  return std::find_if(
                                             fileBtns.begin(), fileBtns.end(),
                                             [&](const auto &btn) {
                                               return btn.get() == child.get();
                                             }) != fileBtns.end();
                                }),
                 children.end());
  fileBtns.clear();
}
bool Canvas::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || !isCursorInside())
    return false;

  const auto &cursorPos = view.context.input.cursorPos;

  if (action == GLFW_PRESS) {
    glm::vec2 localPos = (cursorPos - position) / size;
    glm::vec2 newSize{};
    switch (mode) {
    case Mode::Select:
      selectedElement = nullptr;
      interaction = Interaction::None;
      for (auto &child : children | std::views::reverse) {
        if (child == modeBg || child == modeText || child == selectIndicator)
          continue;
        if (child->isCursorInside()) {
          selectedElement = child;
          // Check if we are near the bottom-right corner for resizing
          glm::vec2 br = selectedElement->position + selectedElement->size;
          if (glm::length(cursorPos - br) < 0.05f) {
            interaction = Interaction::Resizing;
          } else {
            interaction = Interaction::Moving;
            dragOffset = cursorPos - selectedElement->position;
          }
          break;
        }
      }
      break;
    case Mode::Text:
      selectedElement = addChild<hud::TextInput>(localPos, "", true);
      interaction = Interaction::Creating;
      break;
    case Mode::Rect:
      selectedElement = addChild<hud::RectImg>(localPos, newSize, 0);
      interaction = Interaction::Creating;
      break;
    case Mode::Line:
      selectedElement = addChild<hud::Line>(localPos, newSize, currentColor);
      interaction = Interaction::Creating;
      break;
    case Mode::Freehand:
      selectedElement = addChild<hud::PolyLine>(localPos, currentColor);
      interaction = Interaction::Creating;
      dragOffset = cursorPos; // Store last pos for thresholding
      break;
    }

    if (selectedElement) {
      if (!selectIndicator) {
        glm::vec2 localPos = (selectedElement->position - position) / size;
        glm::vec2 localSize = selectedElement->size / size;
        selectIndicator = addChild<hud::EmptyRect>(localPos, localSize);
      } else {
        auto it = std::find(children.begin(), children.end(), selectIndicator);
        if (it != children.end()) {
          std::rotate(it, it + 1, children.end());
        }
        selectIndicator->position = selectedElement->position;
        selectIndicator->size = selectedElement->size;
      }
    } else if (selectIndicator) {
      auto it = std::find(children.begin(), children.end(), selectIndicator);
      if (it != children.end()) {
        children.erase(it);
      }
      selectIndicator = nullptr;
    }
    return true;
  } else if (action == GLFW_RELEASE) {
    interaction = Interaction::None;
    // Don't deselect text input immediately
    if (mode != Mode::Select &&
        !std::dynamic_pointer_cast<vkh::hud::TextInput>(selectedElement)) {
      selectedElement = nullptr;
    }
    return true;
  }
  return false;
}
bool Canvas::handleCursorPosition(double xpos, double ypos) {
  if (!isCursorInside())
    return false;

  const auto &cursorPos = view.context.input.cursorPos;

  if (interaction == Interaction::Creating) {
    if (mode == Mode::Freehand && selectedElement) {
      if (glm::length(cursorPos - dragOffset) > 0.01f) {
        glm::vec2 localPos = (cursorPos - position) / size;
        if (auto poly = std::dynamic_pointer_cast<vkh::hud::PolyLine>(
                selectedElement)) {
          poly->addPoint(localPos);
          dragOffset = cursorPos;
          selectIndicator->position = poly->position;
          selectIndicator->size = poly->size;
        }
      }
    } else if (selectedElement &&
               !std::dynamic_pointer_cast<vkh::hud::TextInput>(
                   selectedElement)) {
      selectedElement->size = cursorPos - selectedElement->position;
      selectIndicator->position = selectedElement->position;
      selectIndicator->size = selectedElement->size;
    }
  } else if (interaction == Interaction::Moving && selectedElement) {
    glm::vec2 oldPos = selectedElement->position;
    selectedElement->position = cursorPos - dragOffset;
    glm::vec2 delta = selectedElement->position - oldPos;

    if (auto poly =
            std::dynamic_pointer_cast<vkh::hud::PolyLine>(selectedElement)) {
      for (auto &p : poly->points) {
        p += delta / size;
      }
    }

    if (selectIndicator) {
      selectIndicator->position = selectedElement->position;
      selectIndicator->size = selectedElement->size;
    }
    return true;
  } else if (interaction == Interaction::Resizing && selectedElement) {
    glm::vec2 oldSize = selectedElement->size;
    selectedElement->size = cursorPos - selectedElement->position;

    if (auto poly =
            std::dynamic_pointer_cast<vkh::hud::PolyLine>(selectedElement)) {
      if (glm::abs(oldSize.x) > 1e-5f && glm::abs(oldSize.y) > 1e-5f) {
        glm::vec2 scale = selectedElement->size / oldSize;
        glm::vec2 relPos = (selectedElement->position - position) / size;
        for (auto &p : poly->points) {
          p = relPos + (p - relPos) * scale;
        }
      }
    }

    if (selectIndicator) {
      selectIndicator->position = selectedElement->position;
      selectIndicator->size = selectedElement->size;
    }
    return true;
  }
  // Hover effect: find the element under the cursor
  std::shared_ptr<Element> elementToHighlight = selectedElement;
  if (interaction == Interaction::None) {
    elementToHighlight = nullptr;
    for (auto &child : children | std::views::reverse) {
      if (child == modeBg || child == modeText || child == selectIndicator)
        continue;
      if (child->isCursorInside()) {
        elementToHighlight = child;
        break;
      }
    }
  }

  // Update selection indicator visibility
  if (elementToHighlight) {
    if (!selectIndicator) {
      glm::vec2 localPos = (elementToHighlight->position - position) / size;
      glm::vec2 localSize = elementToHighlight->size / size;
      selectIndicator = addChild<hud::EmptyRect>(localPos, localSize);
    } else {
      // Ensure selectIndicator is drawn on top
      auto it = std::find(children.begin(), children.end(), selectIndicator);
      if (it != children.end()) {
        std::rotate(it, it + 1, children.end());
      }
      selectIndicator->position = elementToHighlight->position;
      selectIndicator->size = elementToHighlight->size;
    }
  } else if (selectIndicator) {
    auto it = std::find(children.begin(), children.end(), selectIndicator);
    if (it != children.end()) {
      children.erase(it);
    }
    selectIndicator = nullptr;
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

  // --- Texture Deduplication ---
  std::unordered_map<unsigned short, size_t> textureToSavedIndex;
  std::vector<vkh::Image *> savedImages;

  for (const auto &child : children) {
    if (child == modeBg || child == modeText || child == selectIndicator)
      continue;

    if (auto rect = std::dynamic_pointer_cast<vkh::hud::RectImg>(child)) {
      if (rect->imageIndex > 0) {
        if (textureToSavedIndex.find(rect->imageIndex) ==
            textureToSavedIndex.end()) {
          auto &img = view.hudSys.solidColorSys.images[rect->imageIndex];
          savedImages.push_back(&img);
          textureToSavedIndex[rect->imageIndex] = savedImages.size() - 1;
        }
      }
    }
  }

  // --- Write Texture Count ---
  uint32_t textureCount = static_cast<uint32_t>(savedImages.size());
  writeBinary(textureCount);

  // --- Write Each Texture as PNG ---
  for (auto *img : savedImages) {
    auto pngData =
        img->downloadAndSerializeToPNG(); // returns std::vector<uint8_t>
    uint32_t pngSize = static_cast<uint32_t>(pngData.size());
    writeBinary(pngSize);
    content.append(reinterpret_cast<const char *>(pngData.data()), pngSize);
  }

  // --- Write Elements ---
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
    } else if (auto rect = std::dynamic_pointer_cast<vkh::hud::RectImg>(child)) {
      content.push_back('R');
      writeVec2(localPos);
      writeVec2(localSize);
      unsigned short savedTexIndex = 0;
      auto it = textureToSavedIndex.find(rect->imageIndex);
      if (it != textureToSavedIndex.end()) {
        savedTexIndex =
            static_cast<unsigned short>(it->second + 1); // 0 = no texture
      }
      writeBinary(savedTexIndex);
    } else if (auto line = std::dynamic_pointer_cast<vkh::hud::Line>(child)) {
      content.push_back('L');
      writeVec2(localPos);
      writeVec2(localSize);
      writeVec3(line->color);
    } else if (auto poly =
                   std::dynamic_pointer_cast<vkh::hud::PolyLine>(child)) {
      content.push_back('P');
      writeVec3(poly->color);
      uint32_t pointCount = static_cast<uint32_t>(poly->points.size());
      writeBinary(pointCount);
      for (const auto &p : poly->points) {
        writeVec2(p);
      }
    }
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(content.data(), content.size());
  out.close();
}

void Canvas::loadFromFile(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
    throw std::runtime_error("Invalid file path");

  reset();

  std::ifstream in(path, std::ios::binary);
  std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
  in.close();

  size_t offset = 0;

  auto readBinary = [&]<typename T>() -> T {
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(T));
    offset += sizeof(T);
    return value;
  };

  auto readVec2 = [&]() -> glm::vec2 {
    return glm::vec2{readBinary.operator()<float>(),
                     readBinary.operator()<float>()};
  };

  auto readVec3 = [&]() -> glm::vec3 {
    return glm::vec3{readBinary.operator()<float>(),
                     readBinary.operator()<float>(),
                     readBinary.operator()<float>()};
  };

  auto readString = [&]() -> std::string {
    size_t start = offset;
    while (offset < data.size() && data[offset] != '\0')
      ++offset;
    if (offset >= data.size())
      throw std::runtime_error("String not null-terminated");
    std::string str(data.data() + start, offset - start);
    ++offset;
    return str;
  };

  uint32_t textureCount = readBinary.operator()<uint32_t>();
  std::vector<unsigned short> loadedTextureIndices;
  loadedTextureIndices.reserve(textureCount);

  for (uint32_t i = 0; i < textureCount; ++i) {
    uint32_t pngSize = readBinary.operator()<uint32_t>();
    if (offset + pngSize > data.size())
      throw std::runtime_error("PNG data exceeds file");

    void *pngData = data.data() + offset;
    offset += pngSize;

    unsigned short texIndex =
        view.hudSys.solidColorSys.addTextureFromPNGMemory(pngData, pngSize);
    loadedTextureIndices.push_back(texIndex);
  }

  while (offset < data.size()) {
    char type = data[offset++];
    switch (type) {
    case 'T': {
      glm::vec2 pos = readVec2();
      std::string text = readString();
      addChild<hud::TextInput>(pos, text);
      break;
    }
    case 'R': {
      glm::vec2 pos = readVec2();
      glm::vec2 size = readVec2();
      unsigned short savedTexIndex = readBinary.operator()<unsigned short>();
      unsigned short actualTexIndex = 0;
      if (savedTexIndex > 0 && savedTexIndex <= textureCount) {
        actualTexIndex = loadedTextureIndices[savedTexIndex - 1];
      }
      addChild<hud::RectImg>(pos, size, actualTexIndex);
      break;
    }
    case 'L': {
      glm::vec2 pos = readVec2();
      glm::vec2 size = readVec2();
      glm::vec3 color = readVec3();
      addChild<hud::Line>(pos, size, color);
      break;
    }
    case 'P': {
      glm::vec3 color = readVec3();
      uint32_t pointCount = readBinary.operator()<uint32_t>();
      if (pointCount > 0) {
        glm::vec2 firstPoint = readVec2();
        auto poly = addChild<hud::PolyLine>(firstPoint, color);
        for (uint32_t i = 1; i < pointCount; ++i) {
          poly->addPoint(readVec2());
        }
      }
      break;
    }
    default:
      std::println("Unknown element type '{}', skipping", type);
      return;
    }
  }
}
} // namespace vkh::hud
