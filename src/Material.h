#pragma once
#include "long_march.h"

// Simple material structure for ray tracing
struct Material {
	glm::vec3 base_color;
	float roughness;
	float metallic;
	float transmission;
	float ior;
	
	Material() {
		base_color = glm::vec3(0.8f, 0.8f, 0.8f);
		roughness = 0.5f;
		metallic = 0.0f;
		transmission = 0.0f;
		ior = 1.5f;
	}
	
	Material(const glm::vec3& _base_color, float _roughness = 0.5f, float _metallic = 0.0f, 
	         float _transmission = 0.0f, float _ior = 1.5f) {
		base_color = _base_color;
		roughness = _roughness;
		metallic = _metallic;
		transmission = _transmission;
		ior = _ior;
	}
};

