#include "SDL_events.h"
#include <vk_types.h>

class Camera {
public:
    glm::vec3 velocity;
    glm::vec3 position;
    // vertical rotation
    float pitch { 0.f };
    // horizontal rotation
    float yaw { 0.f };
    glm::vec3 orbitTarget { 0.f };
    float orbitDistance { 100.f };
    bool orbitEnabled { false };

    glm::mat4 getViewMatrix() const;
    glm::mat4 getRotationMatrix() const;

    void processSDLEvent(SDL_Event& e);

    void update();
    void setOrbitEnabled(bool enabled) { orbitEnabled = enabled; }
};
