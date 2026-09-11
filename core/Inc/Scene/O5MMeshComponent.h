#ifndef __O5MMESHCOMPONENT__H
#define __O5MMESHCOMPONENT__H

#include "Scene/O5MComponent.h"

//TODO: find out how to make this work
class Mesh;
class Material;

class O5MMeshComponent : public O5MComponent {
private:
    Mesh* mesh = nullptr;
    Material* material = nullptr;
public:
    O5MMeshComponent(Mesh* mesh, Material* material) : mesh(mesh), material(material) {}

    void setMesh(Mesh* m) { mesh = m; }
    void setMaterial(Material* m) { material = m; }

    void render(void) override;
};

#endif //!__O5MMESHCOMPONENT__
