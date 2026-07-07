#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

void initialize(int argc, char** argv) {
    if(!glfwInit()) {
        std::cerr <<"glfw failed\n";
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    #ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    #endif

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);  
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);     
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);   
}

int main (int argc, char** argv) {
    initialize(argc, argv);
    GLFWwindow* window = glfwCreateWindow(800, 600, "o5m", NULL, NULL);
    if (!window) {
      std::cerr << "Error: GLFW window not created\n";
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Error: GLAD address not found\n"; 
        exit(EXIT_FAILURE);
    }
        
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
