#pragma once
#include "long_march.h"

// Simple material structure for ray tracing
struct Material {
	glm::vec3 base_color;
	float roughness;
	float metallic;
	Material() {
		base_color = glm::vec3(0.8f, 0.8f, 0.8f);
		roughness = 0.5f;
		metallic = 0.0f; 
    }
	Material(const glm::vec3& _base_color, float _roughness = 0.5f, float _metallic = 0.0f) {
		base_color = _base_color;
		roughness = _roughness;
		metallic = _metallic;
	}
};

