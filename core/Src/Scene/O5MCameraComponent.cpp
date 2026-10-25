#include "Scene/O5MCameraComponent.h"

void O5MCameraComponent::setAspectRatio(float aspect){
    if (!std::isfinite(aspect) || aspect <= 0.0f) {
        return;
    }
    m_aspectRatio = aspect;
    projectionDirty = true;
}

void O5MCameraComponent::setPerspective(float fov, float aspect, float zNear, float zFar) {
    if (!std::isfinite(fov) || !std::isfinite(aspect) || !std::isfinite(zNear) ||
        !std::isfinite(zFar) || aspect <= 0.0f || zNear <= 0.0f || zFar <= zNear) {
        return;
    }
    m_fieldOfView = fov;
    m_aspectRatio = aspect;
    m_nearPlane = zNear;
    m_farPlane = zFar;
    projectionDirty = true;
}

glm::mat4 O5MCameraComponent::getViewMatrix(void) {
    m_viewMatrix[0][0] = m_right.x;  m_viewMatrix[1][0] = m_right.y;  m_viewMatrix[2][0] = m_right.z;  m_viewMatrix[3][0] = -glm::dot(m_right,  m_position);
    m_viewMatrix[0][1] = m_up.x;     m_viewMatrix[1][1] = m_up.y;     m_viewMatrix[2][1] = m_up.z;     m_viewMatrix[3][1] = -glm::dot(m_up,     m_position);
    m_viewMatrix[0][2] = -m_front.x; m_viewMatrix[1][2] = -m_front.y; m_viewMatrix[2][2] = -m_front.z; m_viewMatrix[3][2] = -glm::dot(-m_front, m_position);
    m_viewMatrix[0][3] = 0.0f;       m_viewMatrix[1][3] = 0.0f;       m_viewMatrix[2][3] = 0.0f;       m_viewMatrix[3][3] = 1.0f;

    return m_viewMatrix;
};

glm::mat4 O5MCameraComponent::getProjectionMatrix(void) {
    if (!projectionDirty)
        return m_projectionMatrix;

    m_projectionMatrix = glm::perspective(glm::radians(m_fieldOfView * zoom), m_aspectRatio, m_nearPlane, m_farPlane);

    m_projectionMatrix[1][1] *= -1.0f;
    projectionDirty = false;
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

void O5MCameraComponent::processKey(O5MKeyboardEvent::E_KeyboardKey key, bool isDown) {
    if (key == O5MKeyboardEvent::Key_None)
        return;
    if (isDown)
        m_keyDown.set(static_cast<size_t>(key));
    else
        m_keyDown.reset(static_cast<size_t>(key));
}

//I think this should be done in Transform component.
void O5MCameraComponent::move(O5MCameraComponent::ECameraMovement direction, float deltaTime) {
    glm::vec3 forward = glm::vec3(m_front.x, 0.0f, m_front.z);
    if (glm::dot(forward, forward) < 1e-6f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        forward = glm::normalize(forward);
    }
    const glm::vec3 right = glm::normalize(glm::cross(forward, m_worldUp));
    glm::vec3 offset(0.0f);
    switch (direction) {
        case O5MCameraComponent::FORWARD:
            offset = forward;
            break;
        case O5MCameraComponent::BACKWARD:
            offset = -forward;
            break;
        case O5MCameraComponent::LEFT:
            offset = -right;
            break;
        case O5MCameraComponent::RIGHT:
            offset = right;
            break;
    }
    m_position += offset * m_moveSpeed * deltaTime;
}

void O5MCameraComponent::processMouseMovement(float xOffset, float yOffset) {
    xOffset *= 0.1f;
    yOffset *= 0.1f;

    m_yaw += xOffset;
    m_pitch -= yOffset;

    if (m_yaw > 180.0f) {
        m_yaw -= 360.0f;
    }
    if (m_yaw < -180.0f) {
        m_yaw += 360.0f;
    }
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

    projectionDirty = true;
}

void O5MCameraComponent::onInitializing(void) {
    m_position = glm::vec3(0.0f, 0.0f, 3.0f);
    m_front = glm::vec3(0.0f, 0.0f, -1.0f);
    m_up = glm::vec3(0.0f, 1.0f, 0.0f);
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_yaw = -90.0f;
    m_pitch = 0.0f;
    projectionDirty = true;
} 

void O5MCameraComponent::update(float deltaTime) {
    const auto held = [this](O5MKeyboardEvent::E_KeyboardKey key) {
        return key != O5MKeyboardEvent::Key_None &&
               m_keyDown.test(static_cast<size_t>(key));
    };

    glm::vec3 forward = glm::vec3(m_front.x, 0.0f, m_front.z);
    if (glm::dot(forward, forward) < 1e-6f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        forward = glm::normalize(forward);
    }
    const glm::vec3 right = glm::normalize(glm::cross(forward, m_worldUp));
    glm::vec3 wish(0.0f);
    if (held(O5MKeyboardEvent::Key_W)) wish += forward;
    if (held(O5MKeyboardEvent::Key_S)) wish -= forward;
    if (held(O5MKeyboardEvent::Key_D)) wish += right;
    if (held(O5MKeyboardEvent::Key_A)) wish -= right;
    if (glm::dot(wish, wish) > 1e-6f) {
        m_position += glm::normalize(wish) * m_moveSpeed * deltaTime;
    }
    updateCameraVectors();
}
