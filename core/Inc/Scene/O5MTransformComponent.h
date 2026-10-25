#ifndef __O5MTRANSFORM__H
#define __O5MTRANSFORM__H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Scene/O5MComponent.h"

class O5MTransformComponent : public O5MComponent {
private:
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); 
    glm::vec3 scale = glm::vec3(1.0f);

    mutable glm::mat4 tansformMatrix = glm::mat4(1.0f);
    mutable bool transformDirty = true;
public:
    void setPosition(const glm::vec3& pos)  { position = pos; transformDirty = true; }
    glm::vec3 getPosition(void) const { return position; }

    void setRotation(const glm::quat& rot) { rotation = rot; transformDirty = true; }
    glm::quat getRotation(void) const { return rotation; }

    void setScale(const glm::vec3& scale) { this->scale = scale; transformDirty = true; }
    glm::vec3 getScale(void) const  { return scale; }

    glm::mat4 getTransformMatrix(void) const { 
        if (transformDirty) {
            glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
            glm::mat4 r = glm::mat4_cast(rotation);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
            tansformMatrix = t * r * s;
            transformDirty = false;
        }
        return tansformMatrix;
    }
};

#endif // !__O5MTRANSFORM__H
