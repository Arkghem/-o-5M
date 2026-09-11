#ifndef __O5MTRANSFORM__H
#define __O5MTRANSFORM__H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "O5MComponent.h"

class O5MTransformComponent : public O5MComponent {
private:
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); 
    glm::vec3 scale = glm::vec3(1.0f);

    mutable glm::mat4 tansformMatrix = glm::mat4(1.0f);
    mutable bool transformDirty = true;
public:
    void setPosition(const glm::vec3& pos);
    glm::vec3 getPosition(void) const;

    void setRotation(const glm::quat& rot);
    glm::quat getRotation(void) const;

    void setScale(const glm::vec3& scale);
    glm::vec3 getScale(void) const;

    glm::mat4 getTransformMatrix(void) const;
};

#endif // !__O5MTRANSFORM__H
