#ifndef D3D12_STUFF_CAMERA_H
#define D3D12_STUFF_CAMERA_H

#include <glm.hpp>
#include "GLFW/glfw3.h"

class Camera {
public:
    Camera();
    // Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float pitch, float yaw);

    // glm::vec3 velocity;
    glm::vec3 position;
    glm::vec3 worldUp;
    glm::vec3 front;
    glm::vec3 right;
    glm::vec3 up;

    double pitch{0.};
    double yaw{0.};

    [[nodiscard]] glm::mat4 ViewMatrix() const;
    // [[nodiscard]] glm::mat4 RotationMatrix() const;

    void ProcessKeyboardInput(int key, int action, float deltaTime);
    void ProcessMouseInput(double xpos, double ypos);
    void UpdateVectors();
};


#endif //D3D12_STUFF_CAMERA_H
