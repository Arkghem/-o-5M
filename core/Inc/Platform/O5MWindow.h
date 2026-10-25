#ifndef __O5MWINDOW__H
#define __O5MWINDOW__H

#include <bitset>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "Events/O5MEvent.h"
#include "Events/O5MKeyboardEvent.h"

struct GLFWwindow; // opaque

class O5MWindow {
public:
    O5MWindow(const std::string& title, uint32_t width, uint32_t height);
    ~O5MWindow(void);

    O5MWindow(const O5MWindow&) = delete;
    O5MWindow& operator=(const O5MWindow&) = delete;

    static std::vector<const char*> getRequiredInstanceExtensions(void);

    bool shouldClose(void) const;
    void requestClose(void);
    void pollEvents(void);

    bool isKeyDown(O5MKeyboardEvent::E_KeyboardKey key) const;

    std::pair<uint32_t, uint32_t> getFramebufferSize(void) const;

    bool takeResizeFlag(void);

    O5MEventBus& getEventBus(void) { return m_eventBus; }

    vk::raii::SurfaceKHR createSurface(const vk::raii::Instance& instance) const;

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode,
                            int action, int mods);
    static void cursorCallback(GLFWwindow* window, double x, double y);
    static void scrollCallback(GLFWwindow* window, double x, double y);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);
    static void focusCallback(GLFWwindow* window, int focused);

    void onKey(int key, int action);
    void onMouseMoved(double x, double y);
    void onMouseScrolled(double x, double y);
    void onMouseButton(int button, int action);
    void onFocus(int focused);

    GLFWwindow* m_window = nullptr;
    O5MEventBus m_eventBus;
    std::bitset<O5MKeyboardEvent::Key_Count> m_keyDown;
    bool m_resized = false;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool m_hasLastMouse = false;
};

#endif // !__O5MWINDOW__H
