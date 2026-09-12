#ifndef __O5MCAMERACOMPONENT__H
#define __O5MCAMERACOMPONENT__H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Scene/O5MComponent.h"


class O5MCameraComponent : public O5MComponent {
public:
    enum EO5MCameraProjection {
        O5MCAMERA_PROJECTION_PERSPECTIVE,
        O5MCAMERA_PROJECTION_ORTHOGRAPHIC
    };
private:
    EO5MCameraProjection projectionType = O5MCAMERA_PROJECTION_PERSPECTIVE;

    float zoom = 1.0f;
    float m_fieldOfView = 120;
    float m_aspectRatio = 16.0f / 9.0f; //16:9
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
    
    float m_yaw;
    float m_pitch;
    
    glm::vec3 m_position;
    glm::vec3 m_front;
    glm::vec3 m_up;
    glm::vec3 m_right;
    glm::vec3 m_worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::vec3 clipPosLT;
    glm::vec3 clipPosLB;
    glm::vec3 clipPosRT;
    glm::vec3 clipPosRB;
    
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    bool projectionDirty = true; 
public:
    enum ECameraMovement {
        FORWARD,
        BACKWARD,
        LEFT,
        RIGHT
    };

    void setPerspective(float fov, float aspect, float near, float far);

    glm::mat4 getViewMatrix(void);
    glm::mat4 getProjectionMatrix(void);

   void updateCameraVectors(void);

   void processMouseMovement(float xOffset, float yOffset); 
   void processKeyboard(ECameraMovement direction, float deltaTime);
   void processMouseScroll(float yOffset);
};

#endif // !__O5MCAMERACOMPONENT__H
