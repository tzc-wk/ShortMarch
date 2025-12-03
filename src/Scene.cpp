#include "Scene.h"

Scene::Scene(grassland::graphics::Core* core)
    : core_(core) {
}

Scene::~Scene() {
    Clear();
}

void Scene::AddEntity(std::shared_ptr<Entity> entity) {
    if (!entity || !entity->IsValid()) {
        grassland::LogError("Cannot add invalid entity to scene");
        return;
    }

    // Build BLAS for the entity
    entity->BuildBLAS(core_);
    
    entities_.push_back(entity);
    grassland::LogInfo("Added entity to scene (total: {})", entities_.size());
}

void Scene::Clear() {
    entities_.clear();
    tlas_.reset();
    materials_buffer_.reset();
}

void Scene::BuildAccelerationStructures() {
    if (entities_.empty()) {
        grassland::LogWarning("No entities to build acceleration structures");
        return;
    }

    // Create TLAS instances from all entities
    std::vector<grassland::graphics::RayTracingInstance> instances;
    instances.reserve(entities_.size());

    for (size_t i = 0; i < entities_.size(); ++i) {
        auto& entity = entities_[i];
        if (entity->GetBLAS()) {
            // Create instance with entity's transform
            // instanceCustomIndex is used to index into materials buffer
            // Convert mat4 to mat4x3 (drop the last row which is always [0,0,0,1] for affine transforms)
            glm::mat4x3 transform_3x4 = glm::mat4x3(entity->GetTransform());
            
            auto instance = entity->GetBLAS()->MakeInstance(
                transform_3x4,
                static_cast<uint32_t>(i),  // instanceCustomIndex for material lookup
                0xFF,                       // instanceMask
                0,                          // instanceShaderBindingTableRecordOffset
                grassland::graphics::RAYTRACING_INSTANCE_FLAG_NONE
            );
            instances.push_back(instance);
        }
    }

    // Build TLAS
    core_->CreateTopLevelAccelerationStructure(instances, &tlas_);
    grassland::LogInfo("Built TLAS with {} instances", instances.size());

    // Update materials buffer
    UpdateMaterialsBuffer();
}

void Scene::UpdateInstances() {
    if (!tlas_ || entities_.empty()) {
        return;
    }

    // Recreate instances with updated transforms
    std::vector<grassland::graphics::RayTracingInstance> instances;
    instances.reserve(entities_.size());

    for (size_t i = 0; i < entities_.size(); ++i) {
        auto& entity = entities_[i];
        if (entity->GetBLAS()) {
            // Convert mat4 to mat4x3
            glm::mat4x3 transform_3x4 = glm::mat4x3(entity->GetTransform());
            
            auto instance = entity->GetBLAS()->MakeInstance(
                transform_3x4,
                static_cast<uint32_t>(i),
                0xFF,
                0,
                grassland::graphics::RAYTRACING_INSTANCE_FLAG_NONE
            );
            instances.push_back(instance);
        }
    }

    // Update TLAS
    tlas_->UpdateInstances(instances);
}

void Scene::UpdateMaterialsBuffer() {
    if (entities_.empty()) {
        return;
    }

    // Collect all materials
    std::vector<Material> materials;
    materials.reserve(entities_.size());

    for (const auto& entity : entities_) {
        materials.push_back(entity->GetMaterial());
    }

    // Create/update materials buffer
    size_t buffer_size = materials.size() * sizeof(Material);
    
    if (!materials_buffer_) {
        core_->CreateBuffer(buffer_size, 
                          grassland::graphics::BUFFER_TYPE_DYNAMIC, 
                          &materials_buffer_);
    }
    
    materials_buffer_->UploadData(materials.data(), buffer_size);
    grassland::LogInfo("Updated materials buffer with {} materials", materials.size());
}

void Scene::BuildVertexIndexData() {
    if (entities_.empty()) return;
    std::vector<float> all_vertices;
    std::vector<uint32_t> all_indices;
    entity_offsets_.clear();
    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;
    for (const auto& entity : entities_) {
        std::vector<float> positions = entity->GetMeshPositionsAsFloatArray();
        const uint32_t* indices = entity->GetMeshIndices();
        uint32_t vertex_count = entity->GetVertexCount();
        uint32_t index_count = entity->GetIndexCount();
        EntityOffset offset;
        offset.vertex_offset = vertex_offset;
        offset.index_offset = index_offset;
        offset.vertex_count = vertex_count;
        offset.index_count = index_count;
        entity_offsets_.push_back(offset);
        all_vertices.insert(all_vertices.end(), positions.begin(), positions.end());
        for (uint32_t i = 0; i < index_count; i++) {
            all_indices.push_back(indices[i] + vertex_offset);
        }
        vertex_offset += vertex_count;
        index_offset += index_count;
    }
    size_t vertex_buffer_size = all_vertices.size() * sizeof(float);
    size_t index_buffer_size = all_indices.size() * sizeof(uint32_t);
    size_t offset_buffer_size = entity_offsets_.size() * sizeof(EntityOffset);
    core_->CreateBuffer(vertex_buffer_size, 
                       grassland::graphics::BUFFER_TYPE_DYNAMIC,
                       &vertex_data_buffer_);
    core_->CreateBuffer(index_buffer_size,
                       grassland::graphics::BUFFER_TYPE_DYNAMIC,
                       &index_data_buffer_);
    core_->CreateBuffer(offset_buffer_size,
                       grassland::graphics::BUFFER_TYPE_DYNAMIC,
                       &entity_offset_buffer_);
    vertex_data_buffer_->UploadData(all_vertices.data(), vertex_buffer_size);
    index_data_buffer_->UploadData(all_indices.data(), index_buffer_size);
    entity_offset_buffer_->UploadData(entity_offsets_.data(), offset_buffer_size);
    
    grassland::LogInfo("Built vertex/index buffers: {} vertices ({} floats), {} indices across {} entities",
                      all_vertices.size() / 3, all_vertices.size(), 
                      all_indices.size(), entities_.size());
}