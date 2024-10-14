#include "camera.h"
#include <ext/matrix_transform.hpp>

Camera::Camera() : position(-5, 0, 0), worldUp(0, 1, 0), front(0, 0, -1), right(1, 0, 0), up(0, 1, 0) {
    UpdateVectors();
}

 Camera::Camera(const float posX, const float posY, const float posZ) : position(posX, posY, posZ), worldUp(0, 1, 0),front(0.f, 0.f, -1.0f), right(1, 0, 0), up(0, 1, 0) {
     UpdateVectors();
 }

glm::mat4 Camera::ViewMatrix() const {
    return lookAt(position, position + front, up);
    // return inverse(translate(glm::mat4{1.f}, position) * RotationMatrix());
}

// glm::mat4 Camera::RotationMatrix() const {
//     const glm::quat pitchRotation = angleAxis(glm::radians(static_cast<float>(pitch)), glm::vec3(1.f, 0.f, 0.f));
//     const glm::quat yawRotation = angleAxis(glm::radians(static_cast<float>(yaw)), glm::vec3(0.f, 1.f, 0.f));
//
//     return toMat4(pitchRotation * yawRotation);
// }

void Camera::ProcessKeyboardInput(const int key, const int action, const float deltaTime) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_W)
            position += front * 0.1f * deltaTime;

        if (key == GLFW_KEY_S)
            position -= front * 0.1f * deltaTime;

        if (key == GLFW_KEY_A)
            position -= right * 0.1f * deltaTime;

        if (key == GLFW_KEY_D)
            position += right * 0.1f * deltaTime;

        if (key == GLFW_KEY_SPACE)
            position += up * 0.1f * deltaTime;

        if (key == GLFW_KEY_LEFT_CONTROL)
            position -= up * 0.1f * deltaTime;
    }

    // if (action == GLFW_RELEASE) {
    //     if (key == GLFW_KEY_W || key == GLFW_KEY_S)
    //         position.z = 0;
    //
    //     if (key == GLFW_KEY_A || key == GLFW_KEY_D)
    //         position.x = 0;
    // }
}

void Camera::ProcessMouseInput(double xpos, double ypos) {
//    static constexpr float SENSITIVITY = 0.005f;
//
//    yaw += xpos * SENSITIVITY;
//    pitch += ypos * SENSITIVITY;
//
//    if (pitch > 89.0f)
//        pitch = 89.0f;
//    else if (pitch < -89.0f)
//        pitch = -89.0f;
//
//    UpdateVectors();
}

void Camera::UpdateVectors() {
    // position += glm::vec3(RotationMatrix() * glm::vec4(velocity * 0.5f, 0.f));
    front = normalize(glm::vec3(cos(glm::radians(yaw)) * cos(glm::radians(pitch)), sin(glm::radians(pitch)), sin(glm::radians(yaw)) * cos(glm::radians(pitch))));

    right = normalize(cross(front, worldUp));
    up = normalize(cross(right, front));
}


