#include "RenderGraph/O5MPassBuilder.h"
#include "RenderGraph/O5MRendergraph.h"

void O5MPassBuilder::addResource(ResourceInfo& handle) {
    m_graph->addResourceInfo(handle);
}
