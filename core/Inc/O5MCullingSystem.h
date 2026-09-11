#ifndef __O5MCULLINGSYSTEM__H
#define __O5MCULLINGSYSTEM__H

#include <vector>

class O5MEntity;
class Camera; //foward declerations
class O5MCullingSystem{
private:
    Camera* m_camera;
    std::vector<O5MEntity*> m_visibleEntities;
public:
    explicit O5MCullingSystem(Camera* camera) : m_camera(camera) {}
public:
    void setCamera(Camera* camera) { m_camera = camera; }
    const std::vector<O5MEntity*>& getVisibleEntities() const { return m_visibleEntities; }

    void cullScene(const std::vector<O5MEntity*>& candidateEntities);
};

#endif // __O5MCULLINGSYSTEM__H
