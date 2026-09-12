#include "Scene/O5MCullingSystem.h"
#include "Scene/O5MCameraComponent.h"

void O5MCullingSystem::cullScene(const std::vector<O5MEntity*>& candidateEntities) {
    m_visibleEntities.clear();

    if (!m_camera)
        return;
}

