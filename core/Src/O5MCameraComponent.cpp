#include "O5MCameraComponent.h"

void O5MCameraComponent::setPerspective(float fov, float aspect, float zNear, float zFar) {
    m_fieldOfView = fov;
    m_aspectRatio = aspect;
    m_nearPlane = zNear;
    m_farPlane = zFar;
}

glm::mat4 O5MCameraComponent::getViewMatrix(void) {
    m_viewMatrix[0][0] = m_right.x;  m_viewMatrix[1][0] = m_right.y;  m_viewMatrix[2][0] = m_right.z;  m_viewMatrix[3][0] = -glm::dot(m_right,  m_position);
    m_viewMatrix[0][1] = m_up.x;     m_viewMatrix[1][1] = m_up.y;     m_viewMatrix[2][1] = m_up.z;     m_viewMatrix[3][1] = -glm::dot(m_up,     m_position);
    m_viewMatrix[0][2] = -m_front.x; m_viewMatrix[1][2] = -m_front.y; m_viewMatrix[2][2] = -m_front.z; m_viewMatrix[3][2] = -glm::dot(-m_front, m_position);
    m_viewMatrix[0][3] = 0.0f;       m_viewMatrix[1][3] = 0.0f;       m_viewMatrix[2][3] = 0.0f;       m_viewMatrix[3][3] = 1.0f;

    return m_viewMatrix;
};

glm::mat4 O5MCameraComponent::getProjectionMatrix(void) {
    m_projectionMatrix = glm::perspective(glm::radians(m_fieldOfView * zoom), m_aspectRatio, m_nearPlane, m_farPlane);
    return m_projectionMatrix;
};

void O5MCameraComponent::updateCameraVectors(void) {
    m_front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_front.y = sin(glm::radians(m_pitch));
    m_front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_front = glm::normalize(m_front);

    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up = glm::normalize(glm::cross(m_right, m_front));
}

void O5MCameraComponent::processKeyboard(O5MCameraComponent::ECameraMovement direction, float deltaTime) {
    switch (direction) {
        case O5MCameraComponent::FORWARD:
            m_position += m_front * deltaTime;
            break;
        case O5MCameraComponent::BACKWARD:
            m_position -= m_front * deltaTime;
            break;
        case O5MCameraComponent::LEFT:
            m_position -= m_right * deltaTime;
            break;
        case O5MCameraComponent::RIGHT:
            m_position += m_right * deltaTime;
            break;
    }
}

void O5MCameraComponent::processMouseMovement(float xOffset, float yOffset) {
    xOffset *= 0.1f;
    yOffset *= 0.1f;

    m_yaw += xOffset;
    m_pitch += yOffset;

    if (m_pitch > 89.0f) {
        m_pitch = 89.0f;
    }
    if (m_pitch < -89.0f) {
        m_pitch = -89.0f;
    }
    
    updateCameraVectors();
}

void O5MCameraComponent::processMouseScroll(float yOffset) {
    zoom -= (float)yOffset;
    if (zoom < 0.1f) {
        zoom = 0.1f;
    }
    if (zoom > 1.0f) {
        zoom = 1.0f;
    }
}
