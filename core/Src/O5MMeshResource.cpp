#include "O5MMeshResource.h"

#include <vector>

bool O5MMeshResource::doLoad(void) {
    std::string filepath = "models/" + getId() + ".gltf";

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    if (!loadMeshData(filepath, vertices, indices)) {
        return false;
    }

    createVertexBuffer(vertices);
    createIndexBuffer(indices);

    m_data->m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_data->m_indexCount = static_cast<uint32_t>(indices.size());

    return true;
}

void O5MMeshResource::doUnload(void) {
    if (isloaded()) {
        m_data.reset();
    }
}

bool O5MMeshResource::loadMeshData(std::string& filePath, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {

}

void O5MMeshResource::createVertexBuffer(std::vector<Vertex>& vertices) {

}

void O5MMeshResource::createIndexBuffer(std::vector<uint32_t>& indices) {

}
