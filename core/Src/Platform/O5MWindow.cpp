#include "Platform/O5MWindow.h"

#include <stdexcept>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "Events/O5MKeyboardEvent.h"
#include "Events/O5MMouseEvent.h"

namespace {

// GLFW key code -> engine key enum. Only the keys the engine consumes are
// mapped; everything else stays Key_None (harmless no-op for the camera).
O5MKeyboardEvent::E_KeyboardKey mapKey(int key) {
    switch (key) {
        case GLFW_KEY_W: return O5MKeyboardEvent::Key_W;
        case GLFW_KEY_A: return O5MKeyboardEvent::Key_A;
        case GLFW_KEY_S: return O5MKeyboardEvent::Key_S;
        case GLFW_KEY_D: return O5MKeyboardEvent::Key_D;
        case GLFW_KEY_Q: return O5MKeyboardEvent::Key_Q;
        case GLFW_KEY_E: return O5MKeyboardEvent::Key_E;
        case GLFW_KEY_LEFT_SHIFT: return O5MKeyboardEvent::Key_None; // modifier, unmapped for now
        default: return O5MKeyboardEvent::Key_None;
    }
}

} // namespace

O5MWindow::O5MWindow(const std::string& title, uint32_t width, uint32_t height) {
    if (glfwInit() != GLFW_TRUE)
        throw std::runtime_error("glfwInit failed");

    // NO_API: we are Vulkan-only since legacy_gl retired
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
    glfwSetFramebufferSizeCallback(m_window, &O5MWindow::framebufferResizeCallback);

    // FPS-camera mode: relative mouse motion, no OS cursor
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

void O5MWindow::pollEvents(void) {
    glfwPollEvents();
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

std::vector<std::unique_ptr<O5MEvent>> O5MWindow::drainEvents(void) {
    std::vector<std::unique_ptr<O5MEvent>> events;
    events.swap(m_eventQueue);
    return events;
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

void O5MWindow::framebufferResizeCallback(GLFWwindow* window, int /*w*/, int /*h*/) {
    static_cast<O5MWindow*>(glfwGetWindowUserPointer(window))->m_resized = true;
}

void O5MWindow::onKey(int key, int action) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        return;
    }

    O5MKeyboardEvent::E_KeyboardEventType type = O5MKeyboardEvent::E_KeyboardEventType::None;
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
        type = O5MKeyboardEvent::E_KeyboardEventType::Pressed;
    else if (action == GLFW_RELEASE)
        type = O5MKeyboardEvent::E_KeyboardEventType::Released;

    m_eventQueue.push_back(std::make_unique<O5MKeyboardEvent>(type, mapKey(key)));
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
        m_eventQueue.push_back(std::make_unique<O5MMouseEvent>(dx, dy));
}

void O5MWindow::onMouseScrolled(double x, double y) {
    m_eventQueue.push_back(std::make_unique<O5MMouseEvent>(0.f, 0.f, static_cast<float>(x + y)));
}
