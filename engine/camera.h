#ifndef D3D12_STUFF_CAMERA_H
#define D3D12_STUFF_CAMERA_H

#include <glm.hpp>
#include "GLFW/glfw3.h"

class Camera {
public:
    Camera();
    Camera(float posX, float posY, float posZ);

    [[nodiscard]] glm::mat4 ViewMatrix() const;
    [[nodiscard]] glm::mat4 ProjectionMatrix();
    [[nodiscard]] float Fov() const { return fov; }

    void ProcessKeyboardInput(int key, int action, float deltaTime);
    void ProcessMouseInput(double xpos, double ypos);
    void UpdateVectors();

    void UpdateAspectRatio(float newWidth, float newHeight);
    void setFov(float newFov) {
        fov = newFov;
        needsUpdate = true;
    }

    double pitch{0.};
    double yaw{180.};

    glm::vec3 position;
    glm::vec3 worldUp;
    glm::vec3 front{};
    glm::vec3 right{};
    glm::vec3 up{};

    float nearPlane{0.1f};
    float farPlane{1000.f};
private:
    glm::mat4 projectionMatrix;

    float fov{90.f};
    float aspectRatio{16.f / 9.f};

    bool needsUpdate{true};
};


#endif //D3D12_STUFF_CAMERA_H
