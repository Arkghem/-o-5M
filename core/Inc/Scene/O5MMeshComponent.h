#ifndef __O5MMESHCOMPONENT__H
#define __O5MMESHCOMPONENT__H

#include "Scene/O5MComponent.h"
#include "Resources/O5MResource.h"

class O5MMeshResource;
class O5MMaterialResource; // unimplemented

class O5MMeshComponent : public O5MComponent {
private:
    O5MResourceHandle<O5MMeshResource> m_mesh; // handle only use the pointer so okay;
    O5MResourceHandle<O5MMaterialResource> m_material; // coming soon ~
public:
    O5MMeshComponent(
        O5MResourceHandle<O5MMeshResource> mesh,
        O5MResourceHandle<O5MMaterialResource> material
    ) :
        m_mesh(mesh), 
        m_material(material) {}

    //setter
    void setMesh(O5MResourceHandle<O5MMeshResource> mesh) { m_mesh = mesh; }
    void setMaterial(O5MResourceHandle<O5MMaterialResource> material) { m_material = material; }

    //getter
    O5MResourceHandle<O5MMeshResource> mesh(void) const { return m_mesh; }
    O5MResourceHandle<O5MMaterialResource> material(void) const { return m_material; }
};

#endif //!__O5MMESHCOMPONENT__
