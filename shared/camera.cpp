#include "camera.h"
#include "glm/gtx/quaternion.hpp"
#include "glm/gtx/transform.hpp"
#include <algorithm>

glm::mat4 Camera::getViewMatrix() const
{
    return glm::lookAt(position, orbitTarget, glm::vec3(0.f, 1.f, 0.f));
}

glm::mat4 Camera::getRotationMatrix() const
{
    // fairly typical FPS style camera. we join the pitch and yaw rotations into
    // the final rotation matrix

    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 { 1.f, 0.f, 0.f });
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3 { 0.f, -1.f, 0.f });

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

void Camera::processSDLEvent(SDL_Event& e)
{
    if (!orbitEnabled) return;

    if (e.type == SDL_MOUSEMOTION) {
        yaw -= static_cast<float>(e.motion.xrel) * 0.004f;
        pitch += static_cast<float>(e.motion.yrel) * 0.004f;
        pitch = std::clamp(pitch, -1.45f, 1.45f);
    }
    if (e.type == SDL_MOUSEWHEEL) {
        const float wheel = e.wheel.preciseY != 0.0f ? e.wheel.preciseY : static_cast<float>(e.wheel.y);
        orbitDistance *= std::pow(0.88f, wheel);
        orbitDistance = std::clamp(orbitDistance, 8.0f, 260.0f);
    }
}

void Camera::update()
{
    if (!orbitEnabled) return;
    const float horizontal = std::cos(pitch) * orbitDistance;
    position = orbitTarget + glm::vec3(
        std::sin(yaw) * horizontal,
        std::sin(pitch) * orbitDistance,
        std::cos(yaw) * horizontal);
}
