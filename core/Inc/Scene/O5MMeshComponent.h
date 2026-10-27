#ifndef __O5MMESHCOMPONENT__H
#define __O5MMESHCOMPONENT__H

#include "Scene/O5MComponent.h"
#include "Resources/O5MResource.h"

class O5MMeshResource;
class O5MMaterialResource;

class O5MMeshComponent : public O5MComponent {
private:
    O5MResourceHandle<O5MMeshResource> m_mesh;
    // Optional override for the whole mesh. Empty means "use each SubMesh's
    // imported/default material".
    O5MResourceHandle<O5MMaterialResource> m_material;
    bool m_visible = true;
public:
    O5MMeshComponent(
        O5MResourceHandle<O5MMeshResource> mesh,
        O5MResourceHandle<O5MMaterialResource> material = {}
    ) :
        m_mesh(mesh),
        m_material(material) {}

    void setMesh(O5MResourceHandle<O5MMeshResource> mesh) { m_mesh = mesh; }
    void setMaterial(O5MResourceHandle<O5MMaterialResource> material) { m_material = material; }
    void setVisible(bool visible) { m_visible = visible; }

    O5MResourceHandle<O5MMeshResource> mesh(void) const { return m_mesh; }
    O5MResourceHandle<O5MMaterialResource> material(void) const { return m_material; }
    bool isVisible(void) const { return m_visible; }
};

#endif //!__O5MMESHCOMPONENT__
