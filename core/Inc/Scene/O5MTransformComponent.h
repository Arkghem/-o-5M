#ifndef __O5MTRANSFORM__H
#define __O5MTRANSFORM__H

#include <algorithm>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Scene/O5MComponent.h"

class O5MTransformComponent : public O5MComponent {
private:
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    mutable glm::mat4 localMatrix = glm::mat4(1.0f);
    mutable bool localDirty = true;

    mutable glm::mat4 worldMatrix = glm::mat4(1.0f);
    mutable bool worldDirty = true;

    O5MTransformComponent* parent = nullptr;
    std::vector<O5MTransformComponent*> children;

    void markLocalDirty(void) {
        localDirty = true;
        markWorldDirty();
    }

    void markWorldDirty(void) {
        worldDirty = true;
        for (O5MTransformComponent* child : children) {
            if (child) {
                child->markWorldDirty();
            }
        }
    }

public:
    ~O5MTransformComponent(void) override {
        for (O5MTransformComponent* child : children) {
            if (child && child->parent == this) {
                child->parent = nullptr;
                child->worldDirty = true;
            }
        }
        if (parent) {
            std::erase(parent->children, this);
        }
    }

    void setPosition(const glm::vec3& pos) {
        position = pos;
        markLocalDirty();
    }

    glm::vec3 getPosition(void) const { return position; }

    void setRotation(const glm::quat& rot) {
        rotation = rot;
        markLocalDirty();
    }

    glm::quat getRotation(void) const { return rotation; }

    void setScale(const glm::vec3& value) {
        scale = value;
        markLocalDirty();
    }

    glm::vec3 getScale(void) const { return scale; }

    void setParent(O5MTransformComponent* value) {
        if (parent == value) {
            return;
        }
        for (O5MTransformComponent* node = value; node; node = node->parent) {
            if (node == this) {
                return;
            }
        }
        if (parent) {
            std::erase(parent->children, this);
        }
        parent = value;
        if (parent) {
            parent->children.push_back(this);
        }
        markWorldDirty();
    }

    O5MTransformComponent* getParent(void) const { return parent; }

    glm::mat4 getLocalMatrix(void) const {
        if (localDirty) {
            glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
            glm::mat4 r = glm::mat4_cast(rotation);
            glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
            localMatrix = t * r * s;
            localDirty = false;
        }
        return localMatrix;
    }

    glm::mat4 getWorldMatrix(void) const {
        if (worldDirty) {
            worldMatrix = parent ? parent->getWorldMatrix() * getLocalMatrix()
                                 : getLocalMatrix();
            worldDirty = false;
        }
        return worldMatrix;
    }

    glm::mat4 getTransformMatrix(void) const { return getLocalMatrix(); }
};

#endif // !__O5MTRANSFORM__H
