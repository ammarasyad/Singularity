#include "camera.h"
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
        if (key == GLFW_KEY_W)
            position += front /** 0.1f * deltaTime*/;

        if (key == GLFW_KEY_S)
            position -= front /** 0.1f * deltaTime*/;

        if (key == GLFW_KEY_A)
            position -= right /** 0.1f * deltaTime*/;

        if (key == GLFW_KEY_D)
            position += right /** 0.1f * deltaTime*/;

        if (key == GLFW_KEY_SPACE)
            position += worldUp /** 0.1f * deltaTime*/;

        if (key == GLFW_KEY_LEFT_CONTROL)
            position -= worldUp /** 0.1f * deltaTime*/;

        if (key == GLFW_KEY_RIGHT) {
            yaw += 15.0f;
            if (yaw >= 360.0f)
                yaw -= 360.0f;
            UpdateVectors();
        }

        if (key == GLFW_KEY_LEFT) {
            yaw -= 15.0f;
            if (yaw < 0.0f)
                yaw += 360.0f;
            UpdateVectors();
        }

        if (key == GLFW_KEY_UP) {
            pitch += 15.0f;
            if (pitch > 90.0f)
                pitch = 90.0f;
            UpdateVectors();
        }

        if (key == GLFW_KEY_DOWN) {
            pitch -= 15.0f;
            if (pitch < -90.0f)
                pitch = -90.0f;
            UpdateVectors();
        }
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

void Camera::UpdateAspectRatio(float newWidth, float newHeight) {
    aspectRatio = newWidth / newHeight;
    needsUpdate = true;
}


