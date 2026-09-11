# stb — header-only utilities (stb_image.h)
#
# Vendored single-header library. No sources to compile; consumers must
# define STB_IMAGE_IMPLEMENTATION in exactly one .cpp before including
# <stb_image.h> (done in core/Src/O5MTextureResource.cpp).
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
