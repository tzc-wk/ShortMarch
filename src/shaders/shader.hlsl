struct CameraInfo {
    float4x4 screen_to_camera;
    float4x4 camera_to_world;
};
struct Material {
    float3 base_color;
    float roughness;
    float metallic;
};
struct HoverInfo {
    int hovered_entity_id;
};
struct EntityOffset {
    uint vertex_offset;
    uint index_offset;
    uint vertex_count;
    uint index_count;
};
RaytracingAccelerationStructure as : register(t0, space0);
RWTexture2D<float4> output : register(u0, space1);
ConstantBuffer<CameraInfo> camera_info : register(b0, space2);
StructuredBuffer<Material> materials : register(t0, space3);
ConstantBuffer<HoverInfo> hover_info : register(b0, space4);
RWTexture2D<int> entity_id_output : register(u0, space5);
RWTexture2D<float4> accumulated_color : register(u0, space6);
RWTexture2D<int> accumulated_samples : register(u0, space7);
ByteAddressBuffer vertex_buffer : register(t0, space8);
ByteAddressBuffer index_buffer : register(t0, space9);
StructuredBuffer<EntityOffset> entity_offsets : register(t0, space10);
struct RayPayload {
    float3 color;
    bool hit;
    uint instance_id;
    float hit_distance;
    float3 hit_point;
    float3 normal;
    uint depth;
    float throughput;
};
#define MAX_DEPTH 6
uint RandomSeed(uint2 pixel, uint depth) {return (pixel.x * 73856093u) ^ (pixel.y * 19349663u) ^ (depth * 83492789u);}
float Random(inout uint seed) {
    seed = seed * 747796405u + 2891336453u;
    uint result = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    result = (result >> 22u) ^ result;
    return float(result) / 4294967295.0;
}
bool RussianRoulette(float throughput, inout uint seed) {
    if (throughput < 0.05) {
        float r = Random(seed);
        float continue_prob = 1.0 - exp(-throughput * 15.0);
        continue_prob = clamp(continue_prob, 0.2, 0.95);
        if (r > continue_prob) {return false;}
    }
    return true;
}
float3 reflect(float3 I, float3 N) {return I - 2.0 * dot(I, N) * N;}
float3 LoadFloat3ByVertexIndex(ByteAddressBuffer buffer, uint vertex_index) {
    uint byte_offset = vertex_index * 12;
    float x = asfloat(buffer.Load(byte_offset));
    float y = asfloat(buffer.Load(byte_offset + 4));
    float z = asfloat(buffer.Load(byte_offset + 8));
    return float3(x, y, z);
}
uint LoadUintByIndex(ByteAddressBuffer buffer, uint index_position) {
    uint byte_offset = index_position * 4;
    return buffer.Load(byte_offset);
}
float3 calcNormal(uint instance_id, uint primitive_index, float3 hit_point) {
    EntityOffset offset = entity_offsets[instance_id];
    uint index_pos = offset.index_offset + primitive_index * 3;
    uint idx0 = LoadUintByIndex(index_buffer, index_pos + 0);
    uint idx1 = LoadUintByIndex(index_buffer, index_pos + 1);
    uint idx2 = LoadUintByIndex(index_buffer, index_pos + 2);
    float3 v0 = LoadFloat3ByVertexIndex(vertex_buffer, idx0);
    float3 v1 = LoadFloat3ByVertexIndex(vertex_buffer, idx1);
    float3 v2 = LoadFloat3ByVertexIndex(vertex_buffer, idx2);
    float3 edge1 = v1 - v0;
    float3 edge2 = v2 - v0;
    float3 normal = normalize(cross(edge1, edge2));
    float3 view_dir = normalize(-WorldRayDirection());
    if (dot(normal, view_dir) < 0.0) {
        normal = -normal;
    }
    return normal;
}
bool TestShadow(float3 hit_point, float3 light_pos) {
    float3 light_to_point = hit_point - light_pos;
    float total_distance = length(light_to_point);
    float3 light_dir = normalize(light_to_point);
    RayPayload shadow_payload;
    shadow_payload.hit = false;
    shadow_payload.instance_id = 0xFFFFFFFF;
    shadow_payload.hit_distance = 10000.0;
    shadow_payload.depth = 100;
    RayDesc shadow_ray;
    shadow_ray.Origin = light_pos + light_dir * 0.001;
    shadow_ray.Direction = light_dir;
    shadow_ray.TMin = 0.001;
    shadow_ray.TMax = total_distance - 0.002;
    TraceRay(as, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadow_ray, shadow_payload);
    return shadow_payload.hit && (shadow_payload.hit_distance < total_distance - 0.001);
}
float3 CalculateDirectLight(float3 hit_point, float3 normal, Material mat, float3 view_dir) {
    float3 light_pos = float3(0, 3, 0);
    float3 light_color = float3(1.0, 0.95, 0.9);
    float light_intensity = 12;
    float3 light_dir = normalize(light_pos - hit_point), light_distance = length(light_pos - hit_point);
    float coef = light_intensity / (light_distance * light_distance + 0.001);
    // ambient
    float3 ambient = mat.base_color * 0.2;
    if (TestShadow(hit_point, light_pos)) return ambient;
    // diffuse
    float ndotl = max(0.0, dot(normal, light_dir));
    float diffuse_factor = 1.0 - mat.metallic;
    float3 diffuse = coef * light_color * mat.base_color * ndotl * diffuse_factor;
    // specular
    float3 half_vector = normalize(light_dir + view_dir);
    float ndoth = max(0.0, dot(normal, half_vector));
    float specular_power = max(1.0, 32.0 * (1.0 - mat.roughness));
    float specular_intensity = pow(ndoth, specular_power);
    float metallic_factor = 0.2 + mat.metallic * 0.8;
    float3 specular = coef * light_color * mat.base_color * specular_intensity * metallic_factor;
    return ambient + diffuse + specular;
}
[shader("raygeneration")] 
void RayGenMain() {
    float2 pixel_center = (float2)DispatchRaysIndex() + float2(0.5, 0.5);
    float2 uv = pixel_center / float2(DispatchRaysDimensions().xy);
    uv.y = 1.0 - uv.y;
    float2 d = uv * 2.0 - 1.0;
    float4 origin = mul(camera_info.camera_to_world, float4(0, 0, 0, 1));
    float4 target = mul(camera_info.screen_to_camera, float4(d, 1, 1));
    float4 direction = mul(camera_info.camera_to_world, float4(target.xyz, 0));
    uint2 pixel_coords = DispatchRaysIndex().xy;
    RayPayload payload;
    payload.color = float3(0, 0, 0);
    payload.hit = false;
    payload.instance_id = 0;
    payload.hit_distance = 0.0;
    payload.hit_point = float3(0, 0, 0);
    payload.normal = float3(0, 0, 0);
    payload.depth = 0;
    payload.throughput = 1.0;
    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    output[pixel_coords] = float4(payload.color, 1);
    // trace another ray to obtain the entity ID
    RayPayload test_payload;
    test_payload.hit = false;
    test_payload.instance_id = 0;
    test_payload.depth = 100;
    TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, test_payload);
    entity_id_output[pixel_coords] = test_payload.hit ? (int)test_payload.instance_id : -1;
    float4 prev_color = accumulated_color[pixel_coords];
    int prev_samples = accumulated_samples[pixel_coords];
    accumulated_color[pixel_coords] = prev_color + float4(payload.color, 1);
    accumulated_samples[pixel_coords] = prev_samples + 1;
}
[shader("miss")] 
void MissMain(inout RayPayload payload) {
    float t = 0.5 * (normalize(WorldRayDirection()).y + 1.0);
    payload.color = lerp(float3(1, 1, 1), float3(0.5, 0.7, 1.0), t) * payload.throughput;
    payload.hit = false;
    payload.instance_id = 0xFFFFFFFF;
}
[shader("closesthit")] 
void ClosestHitMain(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.hit = true;
    uint material_idx = InstanceID(), primitive_index = PrimitiveIndex();
    payload.instance_id = material_idx;
    payload.hit_distance = RayTCurrent();
    payload.hit_point = WorldRayOrigin() + WorldRayDirection() * payload.hit_distance;
    payload.normal = calcNormal(material_idx, primitive_index, payload.hit_point);
    Material mat = materials[material_idx];
    float3 view_dir = normalize(-WorldRayDirection());
    float3 direct_light = CalculateDirectLight(payload.hit_point, payload.normal, mat, view_dir);
    payload.color = direct_light * payload.throughput;
    if (payload.depth < MAX_DEPTH) {
        float reflectivity = (1.0 - mat.roughness) * (0.3 + mat.metallic * 0.8);
        if (reflectivity < 0.01) return;
        uint2 pixel_coords = DispatchRaysIndex().xy;
        uint seed = RandomSeed(pixel_coords, payload.depth);
        bool should_trace = RussianRoulette(payload.throughput * reflectivity, seed);
        if (should_trace && reflectivity > 0.05) {
            float3 reflect_dir = reflect(-view_dir, payload.normal);
            RayDesc reflect_ray;
            reflect_ray.Origin = payload.hit_point + payload.normal * 0.001;
            reflect_ray.Direction = reflect_dir;
            reflect_ray.TMin = 0.001;
            reflect_ray.TMax = 10000.0;
            RayPayload reflect_payload;
            reflect_payload.color = float3(0, 0, 0);
            reflect_payload.hit = false;
            reflect_payload.instance_id = 0;
            reflect_payload.hit_distance = 0.0;
            reflect_payload.hit_point = float3(0, 0, 0);
            reflect_payload.normal = float3(0, 0, 0);
            reflect_payload.depth = payload.depth + 1;
            reflect_payload.throughput = payload.throughput * reflectivity;  
            TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, reflect_ray, reflect_payload);
            payload.color += reflect_payload.color;
        }
    }
}