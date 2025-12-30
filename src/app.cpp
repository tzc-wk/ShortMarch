#include "app.h"
#include "Material.h"
#include "Entity.h"

#include "glm/gtc/matrix_transform.hpp"
#include "imgui.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace {
#include "built_in_shaders.inl"
}

Application::Application(grassland::graphics::BackendAPI api) {
    grassland::graphics::CreateCore(api, grassland::graphics::Core::Settings{}, &core_);
    core_->InitializeLogicalDeviceAutoSelect(true);

    grassland::LogInfo("Device Name: {}", core_->DeviceName());
    grassland::LogInfo("- Ray Tracing Support: {}", core_->DeviceRayTracingSupport());
}

Application::~Application() {
    core_.reset();
}

// Event handler for keyboard input
// Poll keyboard state directly to ensure it works even when ImGui is active
void Application::ProcessInput() {
    // Get GLFW window handle
    GLFWwindow* glfw_window = window_->GLFWWindow();
    
    // Check if this window has focus - only process input for focused window
    if (glfwGetWindowAttrib(glfw_window, GLFW_FOCUSED) == GLFW_FALSE) {
        return;
    }

    // Tab key to toggle UI visibility (only in inspection mode)
    if (!camera_enabled_) {
        ui_hidden_ = (glfwGetKey(glfw_window, GLFW_KEY_TAB) == GLFW_PRESS);
    }
    
    // Ctrl+S to save accumulated output (only in inspection mode)
    static bool ctrl_s_was_pressed = false;
    bool ctrl_pressed = (glfwGetKey(glfw_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || 
                        glfwGetKey(glfw_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    bool s_pressed = (glfwGetKey(glfw_window, GLFW_KEY_S) == GLFW_PRESS);
    bool ctrl_s_pressed = ctrl_pressed && s_pressed;
    
    if (ctrl_s_pressed && !ctrl_s_was_pressed && !camera_enabled_) {
        // Generate filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &time_t);
        
        std::ostringstream filename;
        filename << "screenshot_" 
                 << std::put_time(&tm, "%Y%m%d_%H%M%S")
                 << ".png";
        
        SaveAccumulatedOutput(filename.str());
    }
    ctrl_s_was_pressed = ctrl_s_pressed;
    
    // Only process camera movement if camera is enabled
    if (!camera_enabled_) {
        return;
    }

    // Poll key states directly
    // Move forward
    if (glfwGetKey(glfw_window, GLFW_KEY_W) == GLFW_PRESS) {
        camera_pos_ += camera_speed_ * camera_front_;
    }
    // Move backward
    if (glfwGetKey(glfw_window, GLFW_KEY_S) == GLFW_PRESS) {
        camera_pos_ -= camera_speed_ * camera_front_;
    }
    // Strafe left
    if (glfwGetKey(glfw_window, GLFW_KEY_A) == GLFW_PRESS) {
        camera_pos_ -= glm::normalize(glm::cross(camera_front_, camera_up_)) * camera_speed_;
    }
    // Strafe right
    if (glfwGetKey(glfw_window, GLFW_KEY_D) == GLFW_PRESS) {
        camera_pos_ += glm::normalize(glm::cross(camera_front_, camera_up_)) * camera_speed_;
    }
    // Move up (Space)
    if (glfwGetKey(glfw_window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        camera_pos_ += camera_speed_ * camera_up_;
    }
    // Move down (Shift)
    if (glfwGetKey(glfw_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || 
        glfwGetKey(glfw_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        camera_pos_ -= camera_speed_ * camera_up_;
    }
}

// Event handler for mouse movement
void Application::OnMouseMove(double xpos, double ypos) {
    // Always store mouse position for hover detection (even if ImGui wants input)
    mouse_x_ = xpos;
    mouse_y_ = ypos;

    // Only process camera look if camera is enabled
    if (!camera_enabled_) {
        return;
    }

    if (first_mouse_) {
        last_x_ = (float)xpos;
        last_y_ = (float)ypos;
        first_mouse_ = false;
        return;
    }

    float xoffset = (float)xpos - last_x_;
    float yoffset = last_y_ - (float)ypos; // Reversed since y-coordinates go from bottom to top
    last_x_ = (float)xpos;
    last_y_ = (float)ypos;

    xoffset *= mouse_sensitivity_;
    yoffset *= mouse_sensitivity_;

    yaw_ += xoffset;
    pitch_ += yoffset;

    // Constrain pitch to avoid flipping
    if (pitch_ > 89.0f)
        pitch_ = 89.0f;
    if (pitch_ < -89.0f)
        pitch_ = -89.0f;

    // Recalculate the camera_front_ vector
    glm::vec3 front;
    front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front.y = sin(glm::radians(pitch_));
    front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    camera_front_ = glm::normalize(front);
}

// Event handler for mouse button clicks
void Application::OnMouseButton(int button, int action, int mods, double xpos, double ypos) {
    const int BUTTON_LEFT = 0;  // Left mouse button
    const int BUTTON_RIGHT = 1; // Right mouse button
    const int ACTION_PRESS = 1;

    // Left-click to select entity (only when camera is disabled)
    if (button == BUTTON_LEFT && action == ACTION_PRESS && !camera_enabled_) {
        // Select the currently hovered entity
        if (hovered_entity_id_ >= 0) {
            selected_entity_id_ = hovered_entity_id_;
            grassland::LogInfo("Selected Entity #{}", selected_entity_id_);
        } else {
            selected_entity_id_ = -1;
            grassland::LogInfo("Deselected entity");
        }
    }

    if (button == BUTTON_RIGHT && action == ACTION_PRESS) {
        // Toggle camera mode
        camera_enabled_ = !camera_enabled_;
        
        GLFWwindow* glfw_window = window_->GLFWWindow();
        
        if (camera_enabled_) {
            // Entering camera mode - hide cursor and grab it
            glfwSetInputMode(glfw_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            first_mouse_ = true; // Reset to prevent jump
            grassland::LogInfo("Camera mode enabled - use WASD/Space/Shift to move, mouse to look");
        } else {
            // Exiting camera mode - show cursor
            glfwSetInputMode(glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            grassland::LogInfo("Camera mode disabled - cursor visible, showing info overlay");
        }
    }
}



void Application::OnInit() {
    alive_ = true;
    core_->CreateWindowObject(2000, 1414,
        ((core_->API() == grassland::graphics::BACKEND_API_VULKAN) ? "[Vulkan]" : "[D3D12]") +
        std::string(" Ray Tracing Scene Demo"),
        &window_);

    // Initialize ImGui for this window
    window_->InitImGui();

    // Register the mouse move event handler
    window_->MouseMoveEvent().RegisterCallback(
        [this](double xpos, double ypos) {
            this->OnMouseMove(xpos, ypos);
        }
    );
    // Register the mouse button event handler
    window_->MouseButtonEvent().RegisterCallback(
        [this](int button, int action, int mods, double xpos, double ypos) {
            this->OnMouseButton(button, action, mods, xpos, ypos);
        }
    );

    // Initialize camera as DISABLED to avoid cursor conflicts with multiple windows
    camera_enabled_ = false;
    last_camera_enabled_ = false;
    ui_hidden_ = false;
    hovered_entity_id_ = -1; // No entity hovered initially
    hovered_pixel_color_ = glm::vec4(0.0f); // No pixel color initially
    selected_entity_id_ = -1; // No entity selected initially
    mouse_x_ = 0.0;
    mouse_y_ = 0.0;
    // Don't grab cursor initially - user can right-click to enable camera mode

    // Create scene
    scene_ = std::make_unique<Scene>(core_.get());

    // Add entities to the scene
    
    // color texture version:
//	auto ground = std::make_shared<Entity>(
//		"meshes/cube.obj",
//		Material(glm::vec3(0.4f, 0.4f, 0.4f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(0, 1.0f/20.0f, 0.0f, 0.0f, 10.0f/20.0f, 0.0f, 0.0f, 1.0/20.0f, 10.0f/20.0f)),
//		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f)), 
//		glm::vec3(10.0f, 0.1f, 10.0f))
//	);
    // height map version:
//	auto ground = std::make_shared<Entity>(
//		"meshes/cube.obj",
//		Material(glm::vec3(0.4f, 0.4f, 0.4f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(2, 1.0f/20.0f, 0.0f, 0.0f, 10.0f/20.0f, 0.0f, 0.0f, 1.0/20.0f, 10.0f/20.0f, -30.0, 30.0)),
//		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f)), 
//		glm::vec3(10.0f, 0.1f, 10.0f))
//	);
	auto ground = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.4f, 0.4f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(1, 1.0f/20.0f, 0.0f, 0.0f, 10.0f/20.0f, 0.0f, 0.0f, 1.0/20.0f, 10.0f/20.0f, 1.0f, 0.0f, 0.0f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f)), 
		glm::vec3(10.0f, 0.1f, 10.0f))
	);
	scene_->AddEntity(ground);
	auto purple_sphere = std::make_shared<Entity>(
		"meshes/preview_sphere.obj",
		Material(glm::vec3(1.0f, 0.5f, 1.0f), 0.2f, 0.5f),
		glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 0.3f, -7.0f))
	);
	scene_->AddEntity(purple_sphere);
	auto teapot = std::make_shared<Entity>(
		"meshes/teapot.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.2f, 0.0f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.45f, -4.0f)), glm::vec3(0.3f, 0.3f, 0.3f))
	);
	scene_->AddEntity(teapot);
	auto left_wall = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(5, 0.0f, 0.0f, 1.0f/8.5f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-10.0f, 0.0f, 0.0f)), 
		glm::vec3(0.1f, 10.0f, 10.0f))
	);
	scene_->AddEntity(left_wall);
	auto right_wall = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(5, 0.0f, 0.0f, 1.0f/8.5f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)), 
		glm::vec3(0.1f, 10.0f, 10.0f))
	);
	scene_->AddEntity(right_wall);
	auto back_wall = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(0, 1.0f/8.5f, 0.0f, 0.0f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f)), 
		glm::vec3(10.0f, 10.0f, 0.1f))
	);
	scene_->AddEntity(back_wall);
	auto ceiling = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(4, 1.0f/20.0f, 0.0f, 0.0f, 10.0f/20.0f, 0.0f, 0.0f, 1.0/20.0f, 10.0f/20.0f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 7.5f, 0.0f)), 
		glm::vec3(10.0f, 0.1f, 10.0f))
	);
	scene_->AddEntity(ceiling);
	auto front_wall1 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(0, 1.0f/8.5f, 0.0f, 0.0f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, -10.0f)), 
		glm::vec3(6.5f, 10.0f, 0.1f))
	);
	scene_->AddEntity(front_wall1);
	auto front_wall2 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(0, 1.0f/8.5f, 0.0f, 0.0f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-10.0f, 0.0f, -10.0f)), 
		glm::vec3(6.5f, 10.0f, 0.1f))
	);
	scene_->AddEntity(front_wall2);
	auto front_wall3 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(0, 1.0f/8.5f, 0.0f, 0.0f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f)), 
		glm::vec3(10.0f, 2.2f, 0.1f))
	);
	scene_->AddEntity(front_wall3);
	auto front_wall4 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(0, 1.0f/8.5f, 0.0f, 0.0f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 7.5f, -10.0f)), 
		glm::vec3(10.0f, 2.2f, 0.1f))
	);
	scene_->AddEntity(front_wall4);
	auto pillar = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(0, 1.0f/8.5f, 0.0f, 0.0f, 10.0f/8.5f, 0.0f, 1.0f/8.5f, 0, 1.0f/8.5f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f)), 
		glm::vec3(0.6f, 10.0f, 0.1f))
	);
	scene_->AddEntity(pillar);
	auto pink_bunny = std::make_shared<Entity>(
		"meshes/bunny.obj",
		Material(glm::vec3(0.9f, 0.4f, 0.6f), 0.5f, 0),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-7.0f, 0.3f, -7.0f)), glm::vec3(1.2f, 1.2f, 1.2f))
	);
	scene_->AddEntity(pink_bunny);
	auto brown_table = std::make_shared<Entity>(
		"meshes/table.obj",
		Material(glm::vec3(0.4f, 0.3f, 0.2f), 0.7f, 0),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, -3.3f)), glm::vec3(0.007f, 0.0035f, 0.007f))
	);
	scene_->AddEntity(brown_table);
	auto painting_frame = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.4f, 0.325f, 0.25f), 0.8f, 0.0f, 0.0f, 1.5f, 0.0f, 0.0f, TextureType(6, 1.0f/2.0f, 0.0f, 0.0f, 8.0f/2.0f, 0.0f, -1.0f/2.0f, 0.0f, 4.5f/2.0f)),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-7.0f, 3.5f, -9.9f)), 
		glm::vec3(1.0f, 1.0f, 0.2f))
	);
	scene_->AddEntity(painting_frame);
	auto brown_chair = std::make_shared<Entity>(
		"meshes/chair.obj",
		Material(glm::vec3(0.4f, 0.3f, 0.2f), 0.7f, 0),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.6f, -1.5f)), glm::vec3(0.3f, 0.3f, 0.3f))
	);
	scene_->AddEntity(brown_chair);
	auto sss_sculpture = std::make_shared<Entity>(
    	"meshes/happy.obj",
    	Material(glm::vec3(0.5f, 0.8f, 0.6f), 0.3f, 0.0f, 0.9f, 1.4f, 0.2f, 0.8f),
    	glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(2.75f, -1.05f, -0.5f)), glm::vec3(20.0f, 20.0f, 20.0f))
	);
	scene_->AddEntity(sss_sculpture);
	auto red_apple = std::make_shared<Entity>(
    	"meshes/appleuvw.obj",
    	Material(glm::vec3(0.9f, 0.05f, 0.0f), 0.8f, 0.0f),
    	glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, 0.7f, -1.5f)), glm::vec3(0.006f, 0.006f, 0.006f)),
    	glm::vec3(0.0f, -100.0f, 0.0f)
	);
	scene_->AddEntity(red_apple);
	auto lampshade1 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 7.5f, -0.7f)), 
		glm::vec3(1.5f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade1);
	auto lampshade2 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 7.5f, -3.7f)), 
		glm::vec3(1.5f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade2);
	auto lampshade3 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 7.5f, -2.2f)), 
		glm::vec3(0.1f, 1.0f, 1.5f))
	);
	scene_->AddEntity(lampshade3);
	auto lampshade4 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 7.5f, -2.2f)), 
		glm::vec3(0.1f, 1.0f, 1.5f))
	);
	scene_->AddEntity(lampshade4);
	auto lampshade5 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-8.0f, 7.5f, -7.0f)), 
		glm::vec3(1.0f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade5);
	auto lampshade6 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-8.0f, 7.5f, -9.0f)), 
		glm::vec3(1.0f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade6);
	auto lampshade7 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-7.0f, 7.5f, -8.0f)), 
		glm::vec3(0.1f, 1.0f, 1.0f))
	);
	scene_->AddEntity(lampshade7);
	auto lampshade8 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-9.0f, 7.5f, -8.0f)), 
		glm::vec3(0.1f, 1.0f, 1.0f))
	);
	scene_->AddEntity(lampshade8);
	auto lampshade9 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(8.0f, 7.5f, -7.0f)), 
		glm::vec3(1.0f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade9);
	auto lampshade10 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(8.0f, 7.5f, -9.0f)), 
		glm::vec3(1.0f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade10);
	auto lampshade11 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 7.5f, -8.0f)), 
		glm::vec3(0.1f, 1.0f, 1.0f))
	);
	scene_->AddEntity(lampshade11);
	auto lampshade12 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(9.0f, 7.5f, -8.0f)), 
		glm::vec3(0.1f, 1.0f, 1.0f))
	);
	scene_->AddEntity(lampshade12);
	auto lampshade13 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 7.5f, -7.0f)), 
		glm::vec3(1.0f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade13);
	auto lampshade14 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 7.5f, -9.0f)), 
		glm::vec3(1.0f, 1.0f, 0.1f))
	);
	scene_->AddEntity(lampshade14);
	auto lampshade15 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 7.5f, -8.0f)), 
		glm::vec3(0.1f, 1.0f, 1.0f))
	);
	scene_->AddEntity(lampshade15);
	auto lampshade16 = std::make_shared<Entity>(
		"meshes/cube.obj",
		Material(glm::vec3(0.8f, 0.8f, 0.8f), 0.3f, 0.7f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 7.5f, -8.0f)), 
		glm::vec3(0.1f, 1.0f, 1.0f))
	);
	scene_->AddEntity(lampshade16);
	auto basket = std::make_shared<Entity>(
		"meshes/basket.obj",
		Material(glm::vec3(0.5f, 0.25f, 0.0f), 0.9f, 0.0f),
		glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(-2.5f, -1.0f, -1.0f)), 
		glm::vec3(0.006f, 0.006f, 0.006f))
	);
	scene_->AddEntity(basket);
	
	// Load textures

	std::vector<std::string> texture_paths = {
	    "textures/texture1.png",
	    "textures/texture2.png",
	    "textures/texture3.png",
	    "textures/texture4.png",
	    "textures/texture5.png",
	    "textures/texture6.png",
	    "textures/texture7.png"
	};
	std::vector<int> mip_levels = {10, 0, 0, 0, 0, 0, 0};
	std::vector<float> texture_data_buffer_content;
	
	for (int tex_idx = 0; tex_idx < texture_paths.size(); ++tex_idx) {
	    const auto& path = texture_paths[tex_idx];
	    std::string full_path = grassland::FindAssetFile(path);
	    grassland::LogInfo("Trying to load texture from: {}", full_path);
	    int width, height, channels;
	    unsigned char* data = stbi_load(full_path.c_str(), &width, &height, &channels, 4);
	    
	    if (data) {
	        TextureInfo info;
	        info.width = width;
	        info.height = height;
	        info.offset = (uint32_t)(texture_data_buffer_content.size() / 4);
	        info.mip_levels = mip_levels[tex_idx];
	        texture_infos_.push_back(info);
	        
	        int current_width = width;
	        int current_height = height;
	        unsigned char* current_data = data;
	        int max_mip = mip_levels[tex_idx];
	        
	        for (int mip = 0; mip <= max_mip; ++mip) {
	            for (int i = 0; i < current_width * current_height; ++i) {
	                int x = i % current_width;
	                int y = i / current_width;
	                int base_idx = (y * current_width + x) * 4;
	                
	                float r = current_data[base_idx] / 255.0f;
	                float g = current_data[base_idx + 1] / 255.0f;
	                float b = current_data[base_idx + 2] / 255.0f;
	                float a = current_data[base_idx + 3] / 255.0f;
	                
	                texture_data_buffer_content.push_back(r);
	                texture_data_buffer_content.push_back(g);
	                texture_data_buffer_content.push_back(b);
	                texture_data_buffer_content.push_back(a);
	            }
	            
	            if (mip < max_mip && current_width > 1 && current_height > 1) {
	                int next_width = current_width / 2;
	                int next_height = current_height / 2;
	                unsigned char* next_data = new unsigned char[next_width * next_height * 4];
	                
	                for (int y = 0; y < next_height; ++y) {
	                    for (int x = 0; x < next_width; ++x) {
	                        for (int c = 0; c < 4; ++c) {
	                            float sum = 0.0f;
	                            sum += current_data[((y*2) * current_width + (x*2)) * 4 + c];
	                            sum += current_data[((y*2) * current_width + (x*2+1)) * 4 + c];
	                            sum += current_data[((y*2+1) * current_width + (x*2)) * 4 + c];
	                            sum += current_data[((y*2+1) * current_width + (x*2+1)) * 4 + c];
	                            next_data[(y * next_width + x) * 4 + c] = (unsigned char)(sum / 4.0f);
	                        }
	                    }
	                }
	                
	                if (mip > 0) delete[] current_data;
	                current_data = next_data;
	                current_width = next_width;
	                current_height = next_height;
	            }
	        }
	        
	        if (max_mip > 0) delete[] current_data;
	        else stbi_image_free(data);
	        
	        grassland::LogInfo("Successfully loaded texture from: {}", full_path);
	    } else grassland::LogInfo("Failed to load texture from: {}", full_path);
	}
	
	size_t buffer_size = texture_data_buffer_content.size() * sizeof(float);
	core_->CreateBuffer(buffer_size, 
	                    grassland::graphics::BUFFER_TYPE_DYNAMIC,
	                    &texture_data_buffer_);
	texture_data_buffer_->UploadData(texture_data_buffer_content.data(), buffer_size);
	size_t info_buffer_size = texture_infos_.size() * sizeof(TextureInfo);
	core_->CreateBuffer(info_buffer_size, 
		                grassland::graphics::BUFFER_TYPE_DYNAMIC,
		                &texture_info_buffer_);
	texture_info_buffer_->UploadData(texture_infos_.data(), info_buffer_size);
	
	// Add lightings
	
	point_lights_.clear();
	area_lights_.clear();

	AddPointLight(PointLight(glm::vec3(0, 0.5, 0), glm::vec3(1.0f, 0.95f, 0.9f), 0.0f));
	AddAreaLight(AreaLight(
    	glm::vec3(0, 7.0f, -2.2f), 
    	glm::normalize(glm::vec3(0, -1, 0)), 
    	glm::normalize(glm::vec3(0, 0, 1)),
    	3.0f, 3.0f, 
    	glm::vec3(1.0f, 0.99f, 0.98f), 
    	100.0f
	));
	AddAreaLight(AreaLight(
    	glm::vec3(-8.0f, 7.0f, -8.0f), 
    	glm::normalize(glm::vec3(0, -1, 0)), 
    	glm::normalize(glm::vec3(0, 0, 1)),
    	2.0f, 2.0f, 
    	glm::vec3(1.0f, 0.0f, 0.0f), 
    	50.0f
	));
	AddAreaLight(AreaLight(
    	glm::vec3(0.0f, 7.0f, -8.0f), 
    	glm::normalize(glm::vec3(0, -1, 0)), 
    	glm::normalize(glm::vec3(0, 0, 1)),
    	2.0f, 2.0f, 
    	glm::vec3(0.0f, 1.0f, 0.0f), 
    	50.0f
	));
	AddAreaLight(AreaLight(
    	glm::vec3(8.0f, 7.0f, -8.0f), 
    	glm::normalize(glm::vec3(0, -1, 0)), 
    	glm::normalize(glm::vec3(0, 0, 1)),
    	2.0f, 2.0f, 
    	glm::vec3(0.0f, 0.0f, 1.0f), 
    	50.0f
	));
	
	size_t point_lights_buffer_size = point_lights_.size() * sizeof(PointLight);
	size_t area_lights_buffer_size = area_lights_.size() * sizeof(AreaLight);
	core_->CreateBuffer(point_lights_buffer_size, 
                   grassland::graphics::BUFFER_TYPE_DYNAMIC,
                   &point_lights_buffer_);
	core_->CreateBuffer(area_lights_buffer_size, 
                   grassland::graphics::BUFFER_TYPE_DYNAMIC,
                   &area_lights_buffer_);
	point_lights_buffer_->UploadData(point_lights_.data(), point_lights_buffer_size);
	area_lights_buffer_->UploadData(area_lights_.data(), area_lights_buffer_size);

    // Build acceleration structures
    scene_->BuildAccelerationStructures();
    scene_->BuildVertexIndexData();

    // Create film for accumulation
    film_ = std::make_unique<Film>(core_.get(), window_->GetWidth(), window_->GetHeight());

    core_->CreateBuffer(sizeof(CameraObject), grassland::graphics::BUFFER_TYPE_DYNAMIC, &camera_object_buffer_);
    
    // Create hover info buffer
    core_->CreateBuffer(sizeof(HoverInfo), grassland::graphics::BUFFER_TYPE_DYNAMIC, &hover_info_buffer_);
    HoverInfo initial_hover{};
    initial_hover.hovered_entity_id = -1;
    hover_info_buffer_->UploadData(&initial_hover, sizeof(HoverInfo));

    // Initialize camera state member variables
    camera_pos_ = glm::vec3{ 0.0f, 2.0f, 5.0f };
    camera_up_ = glm::vec3{ 0.0f, 1.0f, 0.0f }; // World up
    camera_speed_ = 0.1f;

    // Initialize new mouse/view variables
    yaw_ = -90.0f; // Point down -Z
    pitch_ = 0.0f;
    last_x_ = (float)window_->GetWidth() / 2.0f;
    last_y_ = (float)window_->GetHeight() / 2.0f;
    mouse_sensitivity_ = 0.1f;
    first_mouse_ = true;

    // Calculate initial camera_front_ based on yaw and pitch
    glm::vec3 front;
    front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front.y = sin(glm::radians(pitch_));
    front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    camera_front_ = glm::normalize(front);

    // Set initial camera buffer data
    CameraObject camera_object{};
    camera_object.screen_to_camera = glm::inverse(
        glm::perspective(glm::radians(60.0f), (float)window_->GetWidth() / (float)window_->GetHeight(), 0.1f, 10.0f));
    camera_object.camera_to_world =
        glm::inverse(glm::lookAt(camera_pos_, camera_pos_ + camera_front_, camera_up_));
    camera_object_buffer_->UploadData(&camera_object, sizeof(CameraObject));

    core_->CreateImage(window_->GetWidth(), window_->GetHeight(), grassland::graphics::IMAGE_FORMAT_R32G32B32A32_SFLOAT,
        &color_image_);
    
    // Create entity ID buffer for accurate picking (R32_SINT to store entity indices)
    core_->CreateImage(window_->GetWidth(), window_->GetHeight(), grassland::graphics::IMAGE_FORMAT_R32_SINT,
        &entity_id_image_);

    core_->CreateShader(GetShaderCode("shaders/shader.hlsl"), "RayGenMain", "lib_6_3", &raygen_shader_);
    core_->CreateShader(GetShaderCode("shaders/shader.hlsl"), "MissMain", "lib_6_3", &miss_shader_);
    core_->CreateShader(GetShaderCode("shaders/shader.hlsl"), "ClosestHitMain", "lib_6_3", &closest_hit_shader_);
    grassland::LogInfo("Shader compiled successfully");

    core_->CreateRayTracingProgram(raygen_shader_.get(), miss_shader_.get(), closest_hit_shader_.get(), &program_);
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_ACCELERATION_STRUCTURE, 1);  // space0
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_WRITABLE_IMAGE, 1);          // space1 - color output
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_UNIFORM_BUFFER, 1);          // space2
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);          // space3 - materials
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_UNIFORM_BUFFER, 1);          // space4 - hover info
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_WRITABLE_IMAGE, 1);          // space5 - entity ID output
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_WRITABLE_IMAGE, 1);          // space6 - accumulated color
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_WRITABLE_IMAGE, 1);          // space7 - accumulated samples
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);
    program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);          // space11 - texture data
	program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);          // space12 - point lights
	program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);          // space13 - area lights
	program_->AddResourceBinding(grassland::graphics::RESOURCE_TYPE_STORAGE_BUFFER, 1);          // space14 - texture info
	program_->Finalize();
}

void Application::OnClose() {
    // Clean up graphics resources first
    program_.reset();
    raygen_shader_.reset();
    miss_shader_.reset();
    closest_hit_shader_.reset();

    scene_.reset();
    film_.reset();

    color_image_.reset();
    entity_id_image_.reset();
    camera_object_buffer_.reset();
    hover_info_buffer_.reset();
    
    // Don't call TerminateImGui - let the window destructor handle it
    // Just reset window which will clean everything up properly
    window_.reset();
}

void Application::UpdateHoveredEntity() {
    // Only detect hover when camera is disabled (cursor visible)
    if (camera_enabled_) {
        hovered_entity_id_ = -1;
        hovered_pixel_color_ = glm::vec4(0.0f);
        return;
    }

    // Get mouse position in pixel coordinates
    int x = static_cast<int>(mouse_x_);
    int y = static_cast<int>(mouse_y_);
    int width = window_->GetWidth();
    int height = window_->GetHeight();
    
    // Check bounds
    if (x < 0 || x >= width || y < 0 || y >= height) {
        hovered_entity_id_ = -1;
        hovered_pixel_color_ = glm::vec4(0.0f);
        return;
    }

    grassland::graphics::Offset2D offset{ x, y };
    grassland::graphics::Extent2D extent{ 1, 1 };
    
    // Read entity ID from the ID buffer at the mouse position
    // The entity_id_image_ stores the entity index (-1 for no entity)
    int32_t entity_id = -1;
    entity_id_image_->DownloadData(&entity_id, offset, extent);
    hovered_entity_id_ = entity_id;
    
    // Read pixel color from accumulated buffer (before highlighting is applied)
    // Note: This is a synchronous read which may cause a GPU stall
    // For better performance, consider using a readback buffer with a frame delay
    float accumulated_rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    film_->GetAccumulatedColorImage()->DownloadData(accumulated_rgba, offset, extent);
    
    // Average by sample count to get final color (before highlighting)
    int sample_count = film_->GetSampleCount();
    if (sample_count > 0) {
        hovered_pixel_color_ = glm::vec4(
            accumulated_rgba[0] / static_cast<float>(sample_count),
            accumulated_rgba[1] / static_cast<float>(sample_count),
            accumulated_rgba[2] / static_cast<float>(sample_count),
            accumulated_rgba[3] / static_cast<float>(sample_count)
        );
    } else {
        hovered_pixel_color_ = glm::vec4(0.0f);
    }
    
    // Hover state is shown in the UI panels, no logging needed
}

void Application::OnUpdate() {
    if (window_->ShouldClose()) {
        window_->CloseWindow();
        alive_ = false;
        return;  // Exit update immediately after closing
    }
    if (alive_) {
        // Process keyboard input to move camera
        ProcessInput();
        
        // Detect camera state change and reset accumulation if camera started moving
        if (camera_enabled_ != last_camera_enabled_) {
            if (camera_enabled_) {
                // Camera just got enabled - will be moving, so prepare for reset when it stops
                grassland::LogInfo("Camera enabled - accumulation will reset when camera stops");
            } else {
                // Camera just got disabled - reset accumulation for new stationary view
                film_->Reset();
                grassland::LogInfo("Camera disabled - starting accumulation");
            }
            last_camera_enabled_ = camera_enabled_;
        }
        
        // Update which entity is being hovered
        UpdateHoveredEntity();
        
        // Update hover info buffer
        HoverInfo hover_info{};
        hover_info.hovered_entity_id = hovered_entity_id_;
        hover_info_buffer_->UploadData(&hover_info, sizeof(HoverInfo));

        // Update the camera buffer with new position/orientation
        CameraObject camera_object{};
        camera_object.screen_to_camera = glm::inverse(
            glm::perspective(glm::radians(60.0f), (float)window_->GetWidth() / (float)window_->GetHeight(), 0.1f, 10.0f));
        camera_object.camera_to_world =
            glm::inverse(glm::lookAt(camera_pos_, camera_pos_ + camera_front_, camera_up_));
        camera_object_buffer_->UploadData(&camera_object, sizeof(CameraObject));


        // Optional: Animate entities
        // For now, entities are static. You can update their transforms and call:
        // scene_->UpdateInstances();
    }
}

void Application::ApplyHoverHighlight(grassland::graphics::Image* image) {
    // Apply hover highlighting by modifying pixels where entity ID matches hovered entity
    // This is done as a CPU-side post-process so it doesn't affect accumulation
    
    int width = window_->GetWidth();
    int height = window_->GetHeight();
    size_t pixel_count = width * height;
    
    // Download current image
    std::vector<float> image_data(pixel_count * 4);
    image->DownloadData(image_data.data());
    
    // Download entity ID buffer
    std::vector<int32_t> entity_ids(pixel_count);
    entity_id_image_->DownloadData(entity_ids.data());
    
    // Apply highlight to pixels matching hovered entity
    float highlight_factor = 0.4f; // Blend factor for white highlight
    for (size_t i = 0; i < pixel_count; i++) {
        if (entity_ids[i] == hovered_entity_id_) {
            // Lerp towards white (1, 1, 1) by highlight_factor
            image_data[i * 4 + 0] = image_data[i * 4 + 0] * (1.0f - highlight_factor) + 1.0f * highlight_factor;
            image_data[i * 4 + 1] = image_data[i * 4 + 1] * (1.0f - highlight_factor) + 1.0f * highlight_factor;
            image_data[i * 4 + 2] = image_data[i * 4 + 2] * (1.0f - highlight_factor) + 1.0f * highlight_factor;
            // Keep alpha unchanged
        }
    }
    
    // Upload modified image
    image->UploadData(image_data.data());
}

void Application::SaveAccumulatedOutput(const std::string& filename) {
    // Save the accumulated output image to a PNG file (without hover highlighting)
    int width = window_->GetWidth();
    int height = window_->GetHeight();
    int sample_count = film_->GetSampleCount();
    
    if (sample_count == 0) {
        grassland::LogWarning("Cannot save screenshot: no samples accumulated yet");
        return;
    }
    
    // Download accumulated color directly from film buffers (not the output image which may have highlights)
    std::vector<float> accumulated_colors(width * height * 4);
    film_->GetAccumulatedColorImage()->DownloadData(accumulated_colors.data());
    
    // Convert from accumulated sum to averaged color, then to 8-bit
    std::vector<uint8_t> byte_data(width * height * 4);
    for (size_t i = 0; i < width * height; i++) {
        // Average the accumulated color by dividing by sample count
        float r = accumulated_colors[i * 4 + 0] / static_cast<float>(sample_count);
        float g = accumulated_colors[i * 4 + 1] / static_cast<float>(sample_count);
        float b = accumulated_colors[i * 4 + 2] / static_cast<float>(sample_count);
        float a = accumulated_colors[i * 4 + 3] / static_cast<float>(sample_count);
        
        // Clamp to [0, 1] and convert to 8-bit
        byte_data[i * 4 + 0] = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, r)) * 255.0f);
        byte_data[i * 4 + 1] = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, g)) * 255.0f);
        byte_data[i * 4 + 2] = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, b)) * 255.0f);
        byte_data[i * 4 + 3] = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, a)) * 255.0f);
    }
    
    // Write PNG file
    int result = stbi_write_png(filename.c_str(), width, height, 4, byte_data.data(), width * 4);
    
    if (result) {
        // Get absolute path for logging
        std::filesystem::path abs_path = std::filesystem::absolute(filename);
        grassland::LogInfo("Screenshot saved: {} ({}x{}, {} samples)", 
                          abs_path.string(), width, height, sample_count);
    } else {
        grassland::LogError("Failed to save screenshot: {}", filename);
    }
}

void Application::RenderInfoOverlay() {
    // Only show overlay when camera is disabled and UI is not hidden
    if (camera_enabled_ || ui_hidden_) {
        return;
    }

    // Create a window on the left side (matching entity panel style)
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(350.0f, (float)window_->GetHeight()), ImGuiCond_Always);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | 
                                     ImGuiWindowFlags_NoResize | 
                                     ImGuiWindowFlags_NoCollapse;
    
    if (!ImGui::Begin("Scene Information", nullptr, window_flags)) {
        ImGui::End();
        return;
    }

    // Camera Information
    ImGui::SeparatorText("Camera");
    ImGui::Text("Position: (%.2f, %.2f, %.2f)", camera_pos_.x, camera_pos_.y, camera_pos_.z);
    ImGui::Text("Direction: (%.2f, %.2f, %.2f)", camera_front_.x, camera_front_.y, camera_front_.z);
    ImGui::Text("Yaw: %.1f掳  Pitch: %.1f掳", yaw_, pitch_);
    ImGui::Text("Speed: %.3f", camera_speed_);
    ImGui::Text("Sensitivity: %.2f", mouse_sensitivity_);

    ImGui::Spacing();

    // Scene Information
    ImGui::SeparatorText("Scene");
    size_t entity_count = scene_->GetEntityCount();
    ImGui::Text("Entities: %zu", entity_count);
    ImGui::Text("Materials: %zu", entity_count); // One material per entity
    
    // Show hovered entity
    if (hovered_entity_id_ >= 0) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Hovered: Entity #%d", hovered_entity_id_);
    } else {
        ImGui::Text("Hovered: None");
    }
    
    // Show selected entity
    if (selected_entity_id_ >= 0) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Selected: Entity #%d", selected_entity_id_);
    } else {
        ImGui::Text("Selected: None");
    }
    
    ImGui::Spacing();
    
    // Show hovered pixel information
    ImGui::SeparatorText("Pixel Inspector");
    ImGui::Text("Mouse Position: (%d, %d)", (int)mouse_x_, (int)mouse_y_);
    
    // Display color value with a color preview box
    ImGui::Text("Pixel Color:");
    ImGui::SameLine();
    ImGui::ColorButton("##pixel_color_preview", 
                       ImVec4(hovered_pixel_color_.r, hovered_pixel_color_.g, hovered_pixel_color_.b, 1.0f),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                       ImVec2(40, 20));
    
    ImGui::Text("  R: %.3f", hovered_pixel_color_.r);
    ImGui::Text("  G: %.3f", hovered_pixel_color_.g);
    ImGui::Text("  B: %.3f", hovered_pixel_color_.b);
    
    // Show as 8-bit values too (common for texture work)
    ImGui::Text("  RGB (8-bit): (%d, %d, %d)", 
                (int)(hovered_pixel_color_.r * 255.0f),
                (int)(hovered_pixel_color_.g * 255.0f),
                (int)(hovered_pixel_color_.b * 255.0f));
    
    // Calculate total triangles
    size_t total_triangles = 0;
    for (const auto& entity : scene_->GetEntities()) {
        if (entity && entity->GetIndexBuffer()) {
            // Each 3 indices = 1 triangle
            size_t indices = entity->GetIndexBuffer()->Size() / sizeof(uint32_t);
            total_triangles += indices / 3;
        }
    }
    ImGui::Text("Total Triangles: %zu", total_triangles);

    ImGui::Spacing();

    // Render Information
    ImGui::SeparatorText("Render");
    ImGui::Text("Resolution: %d x %d", window_->GetWidth(), window_->GetHeight());
    ImGui::Text("Backend: %s", 
                core_->API() == grassland::graphics::BACKEND_API_VULKAN ? "Vulkan" : "D3D12");
    ImGui::Text("Device: %s", core_->DeviceName().c_str());
    
    ImGui::Spacing();
    
    // Accumulation Information
    ImGui::SeparatorText("Accumulation");
    if (!camera_enabled_) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Status: Active");
        ImGui::Text("Samples: %d", film_->GetSampleCount());
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Status: Paused");
        ImGui::Text("(Disable camera to accumulate)");
    }

    ImGui::Spacing();

    // Controls hint
    ImGui::SeparatorText("Controls");
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Right Click to enable camera");
    ImGui::Text("W/A/S/D - Move camera");
    ImGui::Text("Space/Shift - Up/Down");
    ImGui::Text("Mouse - Look around");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Hold Tab to hide UI");
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 1.0f, 1.0f), "Ctrl+S to save screenshot");

    ImGui::End();
}

void Application::RenderEntityPanel() {
    // Only show entity panel when camera is disabled and UI is not hidden
    if (camera_enabled_ || ui_hidden_) {
        return;
    }

    // Create a window on the right side
    ImGui::SetNextWindowPos(ImVec2((float)window_->GetWidth() - 350.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(350.0f, (float)window_->GetHeight()), ImGuiCond_Always);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | 
                                     ImGuiWindowFlags_NoResize | 
                                     ImGuiWindowFlags_NoCollapse;
    
    if (!ImGui::Begin("Entity Inspector", nullptr, window_flags)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Entity Selection");
    
    const auto& entities = scene_->GetEntities();
    size_t entity_count = entities.size();
    
    // Entity dropdown with limited height
    ImGui::Text("Select Entity:");
    
    // Create preview text
    std::string preview_text = selected_entity_id_ >= 0 ? 
        "Entity #" + std::to_string(selected_entity_id_) : 
        "None";
    
    ImGui::SetNextItemWidth(-1); // Full width
    if (ImGui::BeginCombo("##entity_select", preview_text.c_str())) {
        // Add "None" option
        bool is_selected = (selected_entity_id_ == -1);
        if (ImGui::Selectable("None", is_selected)) {
            selected_entity_id_ = -1;
        }
        if (is_selected) {
            ImGui::SetItemDefaultFocus();
        }
        
        // Add all entities to the list
        for (size_t i = 0; i < entity_count; i++) {
            std::string label = "Entity #" + std::to_string(i);
            bool is_entity_selected = (selected_entity_id_ == (int)i);
            
            if (ImGui::Selectable(label.c_str(), is_entity_selected)) {
                selected_entity_id_ = (int)i;
            }
            
            if (is_entity_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        
        ImGui::EndCombo();
    }
    
    ImGui::Spacing();
    
    // Show details if an entity is selected
    if (selected_entity_id_ >= 0 && selected_entity_id_ < (int)entity_count) {
        ImGui::SeparatorText("Entity Details");
        
        const auto& entity = entities[selected_entity_id_];
        
        // Transform information
        ImGui::Text("Transform:");
        glm::mat4 transform = entity->GetTransform();
        glm::vec3 position = glm::vec3(transform[3]);
        ImGui::Text("  Position: (%.2f, %.2f, %.2f)", position.x, position.y, position.z);
        
        // Scale
        glm::vec3 scale = glm::vec3(
            glm::length(glm::vec3(transform[0])),
            glm::length(glm::vec3(transform[1])),
            glm::length(glm::vec3(transform[2]))
        );
        ImGui::Text("  Scale: (%.2f, %.2f, %.2f)", scale.x, scale.y, scale.z);
        
        ImGui::Spacing();
        
        // Material information
        ImGui::SeparatorText("Material");
        Material mat = entity->GetMaterial();
        
        ImGui::Text("Base Color:");
        ImGui::ColorEdit3("##base_color", &mat.base_color[0], ImGuiColorEditFlags_NoInputs);
        ImGui::Text("  RGB: (%.2f, %.2f, %.2f)", mat.base_color.r, mat.base_color.g, mat.base_color.b);
        
        ImGui::Text("Roughness: %.2f", mat.roughness);
        ImGui::Text("Metallic: %.2f", mat.metallic);
        
        ImGui::Spacing();
        
        // Mesh information
        ImGui::SeparatorText("Mesh");
        if (entity->GetIndexBuffer()) {
            size_t index_count = entity->GetIndexBuffer()->Size() / sizeof(uint32_t);
            size_t triangle_count = index_count / 3;
            ImGui::Text("Triangles: %zu", triangle_count);
            ImGui::Text("Indices: %zu", index_count);
        }
        
        if (entity->GetVertexBuffer()) {
            size_t vertex_size = sizeof(float) * 3; // Assuming pos(3)
            size_t vertex_count = entity->GetVertexBuffer()->Size() / vertex_size;
            ImGui::Text("Vertices: %zu", vertex_count);
        }
        
        ImGui::Spacing();
        
        // BLAS information
        ImGui::SeparatorText("Acceleration Structure");
        if (entity->GetBLAS()) {
            ImGui::Text("BLAS: Built");
        } else {
            ImGui::Text("BLAS: Not built");
        }
    } else {
        ImGui::TextDisabled("No entity selected");
        ImGui::Spacing();
        ImGui::TextWrapped("Hover over an entity to highlight it, then left-click to select. Or use the dropdown above.");
    }
    
    ImGui::End();
}

void Application::OnRender() {
    // Don't render if window is closing
    if (!alive_) {
        return;
    }
    for (auto& entity : scene_->GetEntities()) {
        entity->UpdateAnimation();
    }
    scene_->UpdateInstances();

    std::unique_ptr<grassland::graphics::CommandContext> command_context;
    core_->CreateCommandContext(&command_context);
    command_context->CmdClearImage(color_image_.get(), { {0.6, 0.7, 0.8, 1.0} });
    
    // Clear entity ID buffer with -1 (no entity)
    command_context->CmdClearImage(entity_id_image_.get(), { {-1, 0, 0, 0} });
    
    command_context->CmdBindRayTracingProgram(program_.get());
    command_context->CmdBindResources(0, scene_->GetTLAS(), grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(1, { color_image_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(2, { camera_object_buffer_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(3, { scene_->GetMaterialsBuffer() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(4, { hover_info_buffer_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(5, { entity_id_image_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(6, { film_->GetAccumulatedColorImage() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(7, { film_->GetAccumulatedSamplesImage() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(8, { scene_->GetVertexDataBuffer() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(9, { scene_->GetIndexDataBuffer() }, grassland::graphics::BIND_POINT_RAYTRACING);
    command_context->CmdBindResources(10, { scene_->GetEntityOffsetBuffer() }, grassland::graphics::BIND_POINT_RAYTRACING);
    if (texture_data_buffer_) {
    	command_context->CmdBindResources(11, { texture_data_buffer_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
	}
	command_context->CmdBindResources(12, { point_lights_buffer_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
	command_context->CmdBindResources(13, { area_lights_buffer_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
	if (texture_info_buffer_) {
		command_context->CmdBindResources(14, { texture_info_buffer_.get() }, grassland::graphics::BIND_POINT_RAYTRACING);
	}
	command_context->CmdDispatchRays(window_->GetWidth(), window_->GetHeight(), 1);
    
    // When camera is disabled, increment sample count and use accumulated image
    grassland::graphics::Image* display_image = color_image_.get();
    if (!camera_enabled_) {
        film_->IncrementSampleCount();
        film_->DevelopToOutput();
        display_image = film_->GetOutputImage();
    }
    
    // Apply hover highlighting as post-process (doesn't affect accumulation)
    if (hovered_entity_id_ >= 0 && !camera_enabled_) {
        ApplyHoverHighlight(display_image);
    }
    
    // Render ImGui overlay
    window_->BeginImGuiFrame();
    RenderInfoOverlay();
    RenderEntityPanel();
    window_->EndImGuiFrame();
    
    command_context->CmdPresent(window_.get(), display_image);
    core_->SubmitCommandContext(command_context.get());
}
