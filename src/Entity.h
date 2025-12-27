#pragma once
#include "long_march.h"
#include "Material.h"

// Entity represents a mesh instance with a material and transform
class Entity {
public:
    Entity(const std::string& obj_file_path, 
           const Material& material = Material(),
           const glm::mat4& transform = glm::mat4(1.0f),
		   const glm::vec3& velocity = glm::vec3(0.0f));

    ~Entity();

    // Load mesh from OBJ file
    bool LoadMesh(const std::string& obj_file_path);

    // Getters
    grassland::graphics::Buffer* GetVertexBuffer() const { return vertex_buffer_.get(); }
    grassland::graphics::Buffer* GetIndexBuffer() const { return index_buffer_.get(); }
    const Material& GetMaterial() const { return material_; }
    const glm::vec3& GetVelocity() const { return velocity_; }
    const glm::mat4& GetTransform() const { return transform_; }
    grassland::graphics::AccelerationStructure* GetBLAS() const { return blas_.get(); }
    
    const Eigen::Vector3f* GetMeshPositions() const { return mesh_.Positions(); }
    const uint32_t* GetMeshIndices() const { return mesh_.Indices(); }
    uint32_t GetVertexCount() const { return mesh_.NumVertices(); }
    uint32_t GetIndexCount() const { return mesh_.NumIndices(); }
    
    std::vector<float> GetMeshPositionsAsFloatArray() const {
        const Eigen::Vector3f* positions = mesh_.Positions();
        uint32_t vertex_count = mesh_.NumVertices();
        std::vector<float> result(vertex_count * 3);
        
        for (uint32_t i = 0; i < vertex_count; i++) {
            result[i * 3 + 0] = positions[i].x();
            result[i * 3 + 1] = positions[i].y();
            result[i * 3 + 2] = positions[i].z();
        }
        return result;
    }

    // Setters
    void SetMaterial(const Material& material) { material_ = material; }
    void SetTransform(const glm::mat4& transform) { transform_ = transform; }
    void SetVelocity(const glm::vec3& velocity) { velocity_ = velocity; }
    
    void UpdateAnimation();

    // Create BLAS for this entity's mesh
    void BuildBLAS(grassland::graphics::Core* core);

    // Check if mesh is loaded
    bool IsValid() const { return mesh_loaded_; }

private:
    grassland::Mesh<float> mesh_;
    Material material_;
    glm::mat4 transform_;
    glm::vec3 velocity_;

    std::unique_ptr<grassland::graphics::Buffer> vertex_buffer_;
    std::unique_ptr<grassland::graphics::Buffer> index_buffer_;
    std::unique_ptr<grassland::graphics::AccelerationStructure> blas_;

    bool mesh_loaded_;
};

