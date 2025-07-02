#pragma once

#include "element.hpp"
#include "eventListener.hpp"
#include "filePicker.hpp"
#include "rect.hpp"
#include "textInput.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <print>

namespace vkh {
namespace hud {
class Canvas
    : public Element,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton>,
      public EventListener<&EngineContext::InputCallbackSystem::cursorPosition>,
      public EventListener<&EngineContext::InputCallbackSystem::doubleClick>,
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
        EventListener<&EngineContext::InputCallbackSystem::doubleClick>(
            view, [this]() { doubleClickCallback(); }),
        EventListener<&EngineContext::InputCallbackSystem::key>(
            view, [this](int key, int scancode, int action, int mods) {
              keyCallback(key, scancode, action, mods);
            }) {
    bg = addChild<Rect>(glm::vec2{}, glm::vec2{1.f}, bgColor);
  }

  std::string serializeContent() {
    std::string content;
    for (int i = 1; i < children.size(); i++) {
      const auto &child = children[i];
      if (auto textInput =
              std::dynamic_pointer_cast<vkh::hud::TextInput>(child)) {
        content += std::format("a{}\0", textInput->content);
      } else if (auto text = std::dynamic_pointer_cast<vkh::hud::Text>(child)) {
        content += std::format("b{}\0", text->content);
      }
      // else {
      //   std::println("couldnt serialize an element in the canvas,
      //   skipping.");
      // }
    }
    return content;
  }

  std::shared_ptr<FilePicker> filePicker;

private:
  void reorderBgToBack() {
    auto it = std::find(children.begin(), children.end(), bg);
    if (it != children.end() && it != children.end() - 1) {
      std::iter_swap(it, children.end() - 1);
    }
  }
  void keyCallback(int key, int scancode, int action, int mods) {
    if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_O && action == GLFW_PRESS) {
      // std::println("{}", serializeContent());
      if (!filePicker) {
        filePicker = addChild<FilePicker>(glm::vec2{.1f}, glm::vec2{.9f},
                                          FilePicker::Mode::Open);
        reorderBgToBack();
      }
    }
    if (mods == GLFW_MOD_CONTROL && key == GLFW_KEY_S && action == GLFW_PRESS) {
      // std::println("{}", serializeContent());
      if (!filePicker) {
        filePicker = addChild<FilePicker>(glm::vec2{.1f}, glm::vec2{.9f},
                                          FilePicker::Mode::Save);
        reorderBgToBack();
      }
    }
    if (key == GLFW_KEY_ENTER && action == GLFW_PRESS) {
      if (!filePicker)
        return;
      std::filesystem::path path = filePicker->path->content;
      std::ofstream out(path, std::ofstream::out | std::ofstream::trunc);
      out << serializeContent();
      out.close();
      children.erase(std::find(children.begin(), children.end(), filePicker));
      filePicker = nullptr;
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
      if (!filePicker)
        return;
      children.erase(std::find(children.begin(), children.end(), filePicker));
      filePicker = nullptr;
    }
  }
  void mouseButtonCallback(int button, int action, int mods) {
    const auto &cursorPos = view.context.input.cursorPos;
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);

    selected = false;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    if (action != GLFW_PRESS)
      return;
    if (!(glm::all(glm::greaterThanEqual(cursorPos, min)) &&
          glm::all(glm::lessThanEqual(cursorPos, max))))
      return;
    selected = true;

    // fix it creating size=0 lines on double clicks
  }
  void cursorPositionCallback(double xpos, double ypos) {
    // const auto &cursorPos = view.context.input.cursorPos;
    //
    // elementBeingAdded->size = cursorPos - elementBeingAdded->position;
  }
  void doubleClickCallback() {
    const auto &cursorPos = view.context.input.cursorPos;

    addChild<TextInput>((cursorPos - position));
  }
  bool selected{};
  std::shared_ptr<Element> elementBeingAdded;
  std::shared_ptr<Rect> bg;
};
} // namespace hud
} // namespace vkh
