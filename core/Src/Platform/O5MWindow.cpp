#include "Platform/O5MWindow.h"

#include <optional>
#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "Events/O5MKeyboardEvent.h"
#include "Events/O5MMouseEvent.h"

namespace {

std::optional<O5MKeyboardEvent::E_KeyboardKey> mapKey(int key) {
    switch (key) {
        case GLFW_KEY_A: return O5MKeyboardEvent::Key_A;
        case GLFW_KEY_B: return O5MKeyboardEvent::Key_B;
        case GLFW_KEY_C: return O5MKeyboardEvent::Key_C;
        case GLFW_KEY_D: return O5MKeyboardEvent::Key_D;
        case GLFW_KEY_E: return O5MKeyboardEvent::Key_E;
        case GLFW_KEY_F: return O5MKeyboardEvent::Key_F;
        case GLFW_KEY_G: return O5MKeyboardEvent::Key_G;
        case GLFW_KEY_H: return O5MKeyboardEvent::Key_H;
        case GLFW_KEY_I: return O5MKeyboardEvent::Key_I;
        case GLFW_KEY_J: return O5MKeyboardEvent::Key_J;
        case GLFW_KEY_K: return O5MKeyboardEvent::Key_K;
        case GLFW_KEY_L: return O5MKeyboardEvent::Key_L;
        case GLFW_KEY_M: return O5MKeyboardEvent::Key_M;
        case GLFW_KEY_N: return O5MKeyboardEvent::Key_N;
        case GLFW_KEY_O: return O5MKeyboardEvent::Key_O;
        case GLFW_KEY_P: return O5MKeyboardEvent::Key_P;
        case GLFW_KEY_Q: return O5MKeyboardEvent::Key_Q;
        case GLFW_KEY_R: return O5MKeyboardEvent::Key_R;
        case GLFW_KEY_S: return O5MKeyboardEvent::Key_S;
        case GLFW_KEY_T: return O5MKeyboardEvent::Key_T;
        case GLFW_KEY_U: return O5MKeyboardEvent::Key_U;
        case GLFW_KEY_V: return O5MKeyboardEvent::Key_V;
        case GLFW_KEY_W: return O5MKeyboardEvent::Key_W;
        case GLFW_KEY_X: return O5MKeyboardEvent::Key_X;
        case GLFW_KEY_Y: return O5MKeyboardEvent::Key_Y;
        case GLFW_KEY_Z: return O5MKeyboardEvent::Key_Z;
        case GLFW_KEY_0: return O5MKeyboardEvent::Key_0;
        case GLFW_KEY_1: return O5MKeyboardEvent::Key_1;
        case GLFW_KEY_2: return O5MKeyboardEvent::Key_2;
        case GLFW_KEY_3: return O5MKeyboardEvent::Key_3;
        case GLFW_KEY_4: return O5MKeyboardEvent::Key_4;
        case GLFW_KEY_5: return O5MKeyboardEvent::Key_5;
        case GLFW_KEY_6: return O5MKeyboardEvent::Key_6;
        case GLFW_KEY_7: return O5MKeyboardEvent::Key_7;
        case GLFW_KEY_8: return O5MKeyboardEvent::Key_8;
        case GLFW_KEY_9: return O5MKeyboardEvent::Key_9;
        case GLFW_KEY_ESCAPE: return O5MKeyboardEvent::Key_Escape;
        case GLFW_KEY_SPACE: return O5MKeyboardEvent::Key_Space;
        case GLFW_KEY_ENTER: return O5MKeyboardEvent::Key_Enter;
        case GLFW_KEY_TAB: return O5MKeyboardEvent::Key_Tab;
        case GLFW_KEY_BACKSPACE: return O5MKeyboardEvent::Key_Backspace;
        case GLFW_KEY_UP: return O5MKeyboardEvent::Key_Up;
        case GLFW_KEY_DOWN: return O5MKeyboardEvent::Key_Down;
        case GLFW_KEY_LEFT: return O5MKeyboardEvent::Key_Left;
        case GLFW_KEY_RIGHT: return O5MKeyboardEvent::Key_Right;
        case GLFW_KEY_LEFT_SHIFT: return O5MKeyboardEvent::Key_LeftShift;
        case GLFW_KEY_RIGHT_SHIFT: return O5MKeyboardEvent::Key_RightShift;
        case GLFW_KEY_LEFT_CONTROL: return O5MKeyboardEvent::Key_LeftControl;
        case GLFW_KEY_RIGHT_CONTROL: return O5MKeyboardEvent::Key_RightControl;
        case GLFW_KEY_LEFT_ALT: return O5MKeyboardEvent::Key_LeftAlt;
        case GLFW_KEY_RIGHT_ALT: return O5MKeyboardEvent::Key_RightAlt;
        default: return std::nullopt;
    }
}

std::optional<O5MMouseButtonEvent::E_MouseButton> mapMouseButton(int button) {
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT: return O5MMouseButtonEvent::Button_Left;
        case GLFW_MOUSE_BUTTON_RIGHT: return O5MMouseButtonEvent::Button_Right;
        case GLFW_MOUSE_BUTTON_MIDDLE: return O5MMouseButtonEvent::Button_Middle;
        default: return std::nullopt;
    }
}

} // namespace

O5MWindow::O5MWindow(const std::string& title, uint32_t width, uint32_t height) {
    if (glfwInit() != GLFW_TRUE)
        throw std::runtime_error("glfwInit failed");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    m_window = glfwCreateWindow(static_cast<int>(width),
                                static_cast<int>(height),
                                title.c_str(), nullptr, nullptr);
    if (!m_window)
        throw std::runtime_error("glfwCreateWindow failed");

    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, &O5MWindow::keyCallback);
    glfwSetCursorPosCallback(m_window, &O5MWindow::cursorCallback);
    glfwSetScrollCallback(m_window, &O5MWindow::scrollCallback);
    glfwSetMouseButtonCallback(m_window, &O5MWindow::mouseButtonCallback);
    glfwSetFramebufferSizeCallback(m_window, &O5MWindow::framebufferResizeCallback);
    glfwSetWindowFocusCallback(m_window, &O5MWindow::focusCallback);

    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

O5MWindow::~O5MWindow(void) {
    if (m_window)
        glfwDestroyWindow(m_window);
    glfwTerminate();
}

std::vector<const char*> O5MWindow::getRequiredInstanceExtensions(void) {
    uint32_t count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    if (!extensions)
        throw std::runtime_error("GLFW reports no Vulkan support (no required instance extensions)");
    return std::vector<const char*>(extensions, extensions + count);
}

bool O5MWindow::shouldClose(void) const {
    return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void O5MWindow::requestClose(void) {
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void O5MWindow::pollEvents(void) {
    glfwPollEvents();
}

bool O5MWindow::isKeyDown(O5MKeyboardEvent::E_KeyboardKey key) const {
    if (key == O5MKeyboardEvent::Key_None)
        return false;
    return m_keyDown.test(static_cast<size_t>(key));
}

std::pair<uint32_t, uint32_t> O5MWindow::getFramebufferSize(void) const {
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    return { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
}

bool O5MWindow::takeResizeFlag(void) {
    const bool resized = m_resized;
    m_resized = false;
    return resized;
}

vk::raii::SurfaceKHR O5MWindow::createSurface(const vk::raii::Instance& instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(*instance, m_window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("glfwCreateWindowSurface failed");
    return vk::raii::SurfaceKHR(instance, surface);
}

void O5MWindow::keyCallback(GLFWwindow* window, int key, int /*scancode*/,
                            int action, int /*mods*/) {
    static_cast<O5MWindow*>(glfwGetWindowUserPointer(window))->onKey(key, action);
}

void O5MWindow::cursorCallback(GLFWwindow* window, double x, double y) {
    static_cast<O5MWindow*>(glfwGetWindowUserPointer(window))->onMouseMoved(x, y);
}

void O5MWindow::scrollCallback(GLFWwindow* window, double x, double y) {
    static_cast<O5MWindow*>(glfwGetWindowUserPointer(window))->onMouseScrolled(x, y);
}

void O5MWindow::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    static_cast<O5MWindow*>(glfwGetWindowUserPointer(window))->onMouseButton(button, action);
}

void O5MWindow::framebufferResizeCallback(GLFWwindow* window, int /*w*/, int /*h*/) {
    static_cast<O5MWindow*>(glfwGetWindowUserPointer(window))->m_resized = true;
}

void O5MWindow::focusCallback(GLFWwindow* window, int focused) {
    static_cast<O5MWindow*>(glfwGetWindowUserPointer(window))->onFocus(focused);
}

void O5MWindow::onFocus(int focused) {
    if (focused) {
        return;
    }
    for (size_t i = 0; i < m_keyDown.size(); ++i) {
        if (!m_keyDown.test(i)) {
            continue;
        }
        m_keyDown.reset(i);
        m_eventBus.publishEvent(O5MKeyboardEvent(
            O5MKeyboardEvent::Released,
            static_cast<O5MKeyboardEvent::E_KeyboardKey>(i)));
    }
    m_hasLastMouse = false;
}

void O5MWindow::onKey(int key, int action) {
    if (action == GLFW_REPEAT)
        return;

    const auto mapped = mapKey(key);
    if (!mapped)
        return;

    const auto engineKey = *mapped;
    if (action == GLFW_PRESS) {
        m_keyDown.set(static_cast<size_t>(engineKey));
        m_eventBus.publishEvent(
            O5MKeyboardEvent(O5MKeyboardEvent::Pressed, engineKey));
    } else if (action == GLFW_RELEASE) {
        m_keyDown.reset(static_cast<size_t>(engineKey));
        m_eventBus.publishEvent(
            O5MKeyboardEvent(O5MKeyboardEvent::Released, engineKey));
    }
}

void O5MWindow::onMouseMoved(double x, double y) {
    // deliver RELATIVE motion (cursor-disabled FPS mode semantics);
    // the first event after startup only establishes the reference point.
    const float dx = m_hasLastMouse ? static_cast<float>(x - m_lastMouseX) : 0.f;
    const float dy = m_hasLastMouse ? static_cast<float>(y - m_lastMouseY) : 0.f;
    m_lastMouseX = x;
    m_lastMouseY = y;
    m_hasLastMouse = true;

    if (dx != 0.f || dy != 0.f)
        m_eventBus.publishEvent(O5MMouseMovedEvent(dx, dy));
}

void O5MWindow::onMouseScrolled(double x, double y) {
    m_eventBus.publishEvent(
        O5MMouseScrolledEvent(static_cast<float>(x), static_cast<float>(y)));
}

void O5MWindow::onMouseButton(int button, int action) {
    const auto mapped = mapMouseButton(button);
    if (!mapped)
        return;

    if (action == GLFW_PRESS)
        m_eventBus.publishEvent(
            O5MMouseButtonEvent(*mapped, O5MMouseButtonEvent::Pressed));
    else if (action == GLFW_RELEASE)
        m_eventBus.publishEvent(
            O5MMouseButtonEvent(*mapped, O5MMouseButtonEvent::Released));
}
