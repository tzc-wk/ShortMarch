#pragma once
#include "long_march.h"

// Simple material structure for ray tracing
struct Material {
	glm::vec3 base_color;
	float roughness;
	float metallic;
	float transmission;
	float ior;
	float mean_free_path;
	float anisotropy_g;
	
	Material() {
		base_color = glm::vec3(0.8f, 0.8f, 0.8f);
		roughness = 0.5f;
		metallic = 0.0f;
		transmission = 0.0f;
		ior = 1.5f;
		mean_free_path = 0.0f;
		anisotropy_g = 0.0f;
	}
	
	Material(const glm::vec3& _base_color, float _roughness = 0.5f, float _metallic = 0.0f, 
	         float _transmission = 0.0f, float _ior = 1.5f,
	         float _mean_free_path = 0.0f, float _anisotropy_g = 0.0f) {
		base_color = _base_color;
		roughness = _roughness;
		metallic = _metallic;
		transmission = _transmission;
		ior = _ior;
		mean_free_path = _mean_free_path;
		anisotropy_g = _anisotropy_g;
	}
};

