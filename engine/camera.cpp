#include "camera.h"

#include <bits/stl_algo.h>

#ifdef _WIN32
#include <glfw/glfw3.h>
#else
// use system glfw
#include <GLFW/glfw3.h>
#endif

#include "ext/matrix_clip_space.hpp"
#include <ext/matrix_transform.hpp>

Camera::Camera() : position(-5, 0, 0), worldUp(0, 1, 0) {
    UpdateVectors();
}

Camera::Camera(const float posX, const float posY, const float posZ) : position(posX, posY, posZ), worldUp(0, 1, 0) {
    UpdateVectors();
}

glm::mat4 Camera::ViewMatrix() const {
    return lookAt(position, position + front, up);
}

glm::mat4 Camera::ProjectionMatrix() {
    if (needsUpdate) {
        projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
        projectionMatrix[1][1] *= -1;
        needsUpdate = false;
    }

    return projectionMatrix;
}

void Camera::ProcessKeyboardInput(const int key, const int action, const float deltaTime) {
    if (action == GLFW_PRESS) {
        constexpr float sensitivity = 0.01f;
        if (key == GLFW_KEY_W)
            position += front * sensitivity * deltaTime;

        if (key == GLFW_KEY_S)
            position -= front * sensitivity * deltaTime;

        if (key == GLFW_KEY_A)
            position -= right * sensitivity * deltaTime;

        if (key == GLFW_KEY_D)
            position += right * sensitivity * deltaTime;

        if (key == GLFW_KEY_SPACE)
            position += worldUp * sensitivity * deltaTime;

        if (key == GLFW_KEY_LEFT_CONTROL)
            position -= worldUp * sensitivity * deltaTime;
    }
}

void Camera::ProcessMouseInput(double xpos, double ypos) {
    static constexpr float SENSITIVITY = 0.05f;

    yaw += xpos * SENSITIVITY;
    pitch += ypos * SENSITIVITY;

    pitch = std::clamp(pitch, -90.0, 90.0);
    yaw = fmod(yaw, 360.0);

    UpdateVectors();
}

void Camera::UpdateVectors() {
    // position += glm::vec3(RotationMatrix() * glm::vec4(velocity * 0.5f, 0.f));
    front = normalize(glm::vec3(cos(glm::radians(yaw)) * cos(glm::radians(pitch)), sin(glm::radians(pitch)), sin(glm::radians(yaw)) * cos(glm::radians(pitch))));

    right = normalize(cross(front, worldUp));
    up = normalize(cross(right, front));
}

void Camera::UpdateAspectRatio(float newWidth, float newHeight) {
    aspectRatio = newWidth / newHeight;
    needsUpdate = true;
}


