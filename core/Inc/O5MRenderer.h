#ifndef __O5MRENDERER__H
#define  __O5MRENDERER__H

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "O5MRendergraph.h"

class O5MRenderer {
private:
    O5MRendergraph m_graph;
public:
    void init(void);
};

#endif //!__O5MRENDERER__H
