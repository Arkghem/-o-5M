#ifndef __O5MWINDOW__H
#define __O5MWINDOW__H

// GLFW window wrapper (Phase 2 scaffold, AI-owned):
//   - owns the GLFW window and its lifetime (glfwInit/terminate)
//   - translates GLFW callbacks into Events-module events (queue drained by
//     the frame loop each tick)
//   - latches a "framebuffer resized" flag for swapchain rebuild
//   - creates the Vulkan surface (glfwCreateWindowSurface)
// GLFW types never leak into this header (opaque forward declaration), so
// consumers of core/Inc do not need GLFW on their include path.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "Events/O5MEvent.h"

struct GLFWwindow; // opaque

class O5MWindow {
public:
    O5MWindow(const std::string& title, uint32_t width, uint32_t height);
    ~O5MWindow(void);

    O5MWindow(const O5MWindow&) = delete;
    O5MWindow& operator=(const O5MWindow&) = delete;

    // instance extensions required for surface creation on this platform.
    // Must be called AFTER a window exists (glfwGetRequiredInstanceExtensions).
    static std::vector<const char*> getRequiredInstanceExtensions(void);

    bool shouldClose(void) const;
    void pollEvents(void);                       // pump GLFW -> event queue

    std::pair<uint32_t, uint32_t> getFramebufferSize(void) const;

    // one-shot latch: true exactly once per resize event
    bool takeResizeFlag(void);

    // move all queued events out; empty if nothing happened this frame
    std::vector<std::unique_ptr<O5MEvent>> drainEvents(void);

    // creates a surface bound to this window; the raii handle keeps the
    // raii::Instance alive by reference -- destroy it before the instance.
    vk::raii::SurfaceKHR createSurface(const vk::raii::Instance& instance) const;

private:
    // static GLFW trampolines -> member handlers (user pointer = this)
    static void keyCallback(GLFWwindow* window, int key, int scancode,
                            int action, int mods);
    static void cursorCallback(GLFWwindow* window, double x, double y);
    static void scrollCallback(GLFWwindow* window, double x, double y);
    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);

    void onKey(int key, int action);
    void onMouseMoved(double x, double y);
    void onMouseScrolled(double x, double y);

    GLFWwindow* m_window = nullptr;
    std::vector<std::unique_ptr<O5MEvent>> m_eventQueue;
    bool m_resized = false;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool m_hasLastMouse = false;
};

#endif // !__O5MWINDOW__H
