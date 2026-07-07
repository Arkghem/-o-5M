# GLAD — OpenGL 4.6 Core Profile loader
# Auto-generated via: python -m glad --api gl=4.6 --profile core --generator c
# See: https://glad.dav1d.de/

add_library(glad STATIC src/glad.c)
target_include_directories(glad PUBLIC include)

# GLAD uses platform-specific GL loader functions at runtime.
# On Linux, we need dl for dlsym; on macOS, the OpenGL framework provides them.
if(UNIX AND NOT APPLE)
    target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})
endif()

# Suppress warnings in generated code.
target_compile_options(glad PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w>
    $<$<C_COMPILER_ID:MSVC>:/w>
)
