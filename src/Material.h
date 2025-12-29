#pragma once
#include "long_march.h"

// Simple material structure for ray tracing
struct TextureType {
    int type;
    int texture_id;
    float c1, c2, c3, c4, c5, c6, c7, c8, c9, c10;
    float normal_x, normal_y, normal_z;
    
    TextureType() : type(0), texture_id(-1), 
        c1(0), c2(0), c3(0), c4(0), c5(0), c6(0), c7(0), c8(0), c9(0), c10(0),
        normal_x(0), normal_y(0), normal_z(0) {}
    
    TextureType(int id, float p1, float p2, float p3, float p4, 
                float p5, float p6, float p7, float p8) 
        : type(1), texture_id(id), 
          c1(p1), c2(p2), c3(p3), c4(p4), 
          c5(p5), c6(p6), c7(p7), c8(p8), c9(0), c10(0),
          normal_x(0), normal_y(0), normal_z(0) {}
    
    TextureType(int id, float p1, float p2, float p3, float p4, 
                float p5, float p6, float p7, float p8,
                float p9, float p10) 
        : type(3), texture_id(id), 
          c1(p1), c2(p2), c3(p3), c4(p4), 
          c5(p5), c6(p6), c7(p7), c8(p8),
          c9(p9), c10(p10),
          normal_x(0), normal_y(0), normal_z(0) {}
    
    TextureType(int id, float p1, float p2, float p3, float p4, 
                float p5, float p6, float p7, float p8,
                float nx, float ny, float nz) 
        : type(2), texture_id(id), 
          c1(p1), c2(p2), c3(p3), c4(p4), 
          c5(p5), c6(p6), c7(p7), c8(p8), c9(0), c10(0),
          normal_x(nx), normal_y(ny), normal_z(nz) {}
};

struct Material {
    glm::vec3 base_color;
    float roughness;
    float metallic;
    float transmission;
    float ior;
    float mean_free_path;
    float anisotropy_g;
    TextureType texture_info;
    
    Material() {
        base_color = glm::vec3(0.8f, 0.8f, 0.8f);
        roughness = 0.5f;
        metallic = 0.0f;
        transmission = 0.0f;
        ior = 1.5f;
        mean_free_path = 0.0f;
        anisotropy_g = 0.0f;
        texture_info = TextureType();
    }
    
    Material(const glm::vec3& _base_color, float _roughness = 0.5f, float _metallic = 0.0f, 
             float _transmission = 0.0f, float _ior = 1.5f,
             float _mean_free_path = 0.0f, float _anisotropy_g = 0.0f,
             const TextureType& _texture = TextureType()) {
        base_color = _base_color;
        roughness = _roughness;
        metallic = _metallic;
        transmission = _transmission;
        ior = _ior;
        mean_free_path = _mean_free_path;
        anisotropy_g = _anisotropy_g;
        texture_info = _texture;
    }
};
