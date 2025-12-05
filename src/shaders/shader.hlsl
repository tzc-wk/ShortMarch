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
struct PointLight {
    float3 position;
    float3 color;
    float intensity;
};
struct AreaLight {
    float3 center;
    float3 normal;
    float3 left;
    float width;
    float height;
    float3 color;
    float intensity;
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
    uint depth;
    float throughput;
};
#define MAX_DEPTH 3
#define PI 3.14159265358979323846
#define AREA_LIGHT_SAMPLES 1

// light information

static const PointLight POINT_LIGHT = {
    float3(0, 3, 0),
    float3(1.0, 0.95, 0.9),
    0
};
static const AreaLight AREA_LIGHT = {
    float3(2, 2.5, 0),
    normalize(float3(0, -1, 0)),
    normalize(float3(0, 0, 1)),
    2.0,
    1.0,
    float3(1.0, 0.9, 0.8),
    15.0
};
static const float3 AMBIENT_COLOR = float3(1.0, 1.0, 1.0);
static const float AMBIENT_INTENSITY = 0.2;

uint RandomSeed(uint2 pixel, uint depth, uint frame) {return (pixel.x * 73856093u) ^ (pixel.y * 19349663u) ^ (depth * 83492789u) ^ (frame * 735682483u);}
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
        if (r > continue_prob) return false;
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
    if (dot(normal, view_dir) < 0.0) normal = -normal;
    return normal;
}
bool TestShadow(float3 hit_point, float3 light_pos) {
    float3 light_to_point = hit_point - light_pos;
    float total_distance = length(light_to_point);
    float3 light_dir = normalize(light_to_point);
    if (total_distance < 0.005) return false;
    RayPayload shadow_payload;
    shadow_payload.color = float3(0, 0, 0); shadow_payload.hit = false;
    shadow_payload.instance_id = 0xFFFFFFFF; shadow_payload.hit_distance = 10000.0;
    shadow_payload.depth = 100; shadow_payload.throughput = 0.0;
    RayDesc shadow_ray;
    shadow_ray.Origin = light_pos + light_dir * 0.001; shadow_ray.Direction = light_dir;
    shadow_ray.TMin = 0.001; shadow_ray.TMax = total_distance - 0.002;
    TraceRay(as, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadow_ray, shadow_payload);
    return shadow_payload.hit && (shadow_payload.hit_distance < total_distance - 0.001);
}
float3 CalculatePointLightContribution(float3 hit_point, float3 normal, Material mat, float3 view_dir, PointLight light) {
    float3 light_dir = normalize(light.position - hit_point);
    float light_distance = length(light.position - hit_point);
    float attenuation = light.intensity / (light_distance * light_distance + 0.001);
    if (TestShadow(hit_point, light.position)) return float3(0, 0, 0);
    float ndotl = max(0.0, dot(normal, light_dir));
    if (ndotl <= 0.0) return float3(0, 0, 0);
    float diffuse_factor = 1.0 - mat.metallic;
    float3 diffuse = attenuation * light.color * mat.base_color * ndotl * diffuse_factor;
    float3 half_vector = normalize(light_dir + view_dir);
    float ndoth = max(0.0, dot(normal, half_vector));
    float specular_power = max(1.0, 32.0 * (1.0 - mat.roughness));
    float specular_intensity = pow(ndoth, specular_power);
    float metallic_factor = 0.2 + mat.metallic * 0.8;
    float3 specular = attenuation * light.color * mat.base_color * specular_intensity * metallic_factor;
    return diffuse + specular;
}
float3 SampleAreaLight(AreaLight light, float2 random_uv) {
    float3 up = normalize(cross(light.normal, light.left));
    float3 left = light.left;
    float u = (random_uv.x - 0.5) * light.width;
    float v = (random_uv.y - 0.5) * light.height;
    return light.center + up * u + left * v;
}
float3 CalculateAreaLightContribution(float3 hit_point, float3 normal, Material mat, float3 view_dir, AreaLight light, inout uint seed) {
    float3 total_contribution = float3(0, 0, 0);
    for (int i = 0; i < AREA_LIGHT_SAMPLES; i++) {
        float2 random_uv = float2(Random(seed), Random(seed));
        float3 light_sample = SampleAreaLight(light, random_uv);
        PointLight sample_light;
        sample_light.position = light_sample;
        sample_light.color = light.color;
        sample_light.intensity = light.intensity / float(AREA_LIGHT_SAMPLES);
        total_contribution += CalculatePointLightContribution(hit_point, normal, mat, view_dir, sample_light);
    }
    return total_contribution;
}
float3 CalculateDirectLight(float3 hit_point, float3 normal, Material mat, float3 view_dir, inout uint seed) {
    float3 total_light = float3(0, 0, 0);
    float3 ambient = AMBIENT_COLOR * AMBIENT_INTENSITY * mat.base_color;
    total_light += ambient;
    total_light += CalculatePointLightContribution(hit_point, normal, mat, view_dir, POINT_LIGHT);
    total_light += CalculateAreaLightContribution(hit_point, normal, mat, view_dir, AREA_LIGHT, seed);
    return total_light;
}
[shader("raygeneration")]
void RayGenMain() {
    uint2 pixel_coords = DispatchRaysIndex().xy;
    uint pixel_seed = RandomSeed(pixel_coords, 0, accumulated_samples[pixel_coords]);
    float random_x = Random(pixel_seed);
    float random_y = Random(pixel_seed);
    float2 pixel_center = (float2)DispatchRaysIndex() + float2(random_x, random_y);
    float2 uv = pixel_center / float2(DispatchRaysDimensions().xy);
    uv.y = 1.0 - uv.y;
    float2 d = uv * 2.0 - 1.0;
    float4 origin = mul(camera_info.camera_to_world, float4(0, 0, 0, 1));
    float4 target = mul(camera_info.screen_to_camera, float4(d, 1, 1));
    float4 direction = mul(camera_info.camera_to_world, float4(target.xyz, 0));
    RayPayload payload;
    payload.color = float3(0, 0, 0); payload.hit = false; payload.instance_id = 0;
    payload.hit_distance = 0.0; payload.depth = 0; payload.throughput = 1.0;
    RayDesc ray;
    ray.Origin = origin.xyz; ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001; ray.TMax = 10000.0;
    TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    output[pixel_coords] = float4(payload.color, 1);
    // trace another ray to get the entity id
    RayPayload test_payload;
    test_payload.color = float3(0, 0, 0); test_payload.hit = false;
    test_payload.instance_id = 0; test_payload.hit_distance = 10000.0;
    test_payload.depth = 100; test_payload.throughput = 0.0;
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
    payload.hit = false; payload.hit_distance = 10000.0; payload.instance_id = 0xFFFFFFFF;
}
[shader("closesthit")]
void ClosestHitMain(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint material_idx = InstanceID(), primitive_index = PrimitiveIndex(); Material mat = materials[material_idx];
    payload.hit = true; payload.instance_id = material_idx; payload.hit_distance = RayTCurrent();
    float3 hit_point = WorldRayOrigin() + WorldRayDirection() * payload.hit_distance;
    float3 norm = calcNormal(material_idx, primitive_index, hit_point);
    float3 view_dir = normalize(-WorldRayDirection());
    uint2 pixel_coords = DispatchRaysIndex().xy;
    uint seed = RandomSeed(pixel_coords, payload.depth, accumulated_samples[pixel_coords]);
    float3 direct_light = CalculateDirectLight(hit_point, norm, mat, view_dir, seed);
    payload.color = direct_light * payload.throughput;
    if (payload.depth < MAX_DEPTH) {
        float reflectivity = min((1.0 - mat.roughness) * (0.3 + mat.metallic * 0.8), 1.0);
        if (reflectivity < 0.01) return;
        bool should_trace = RussianRoulette(payload.throughput * reflectivity, seed);
        if (should_trace && reflectivity > 0.05) {
            float3 reflect_dir = reflect(-view_dir, norm);
            RayDesc reflect_ray;
            reflect_ray.Origin = hit_point + norm * 0.001; reflect_ray.Direction = reflect_dir;
            reflect_ray.TMin = 0.001; reflect_ray.TMax = 10000.0;
            RayPayload reflect_payload;
            reflect_payload.color = float3(0, 0, 0); reflect_payload.hit = false;
            reflect_payload.instance_id = 0; reflect_payload.hit_distance = 0.0;
            reflect_payload.depth = payload.depth + 1; reflect_payload.throughput = payload.throughput * reflectivity;
            TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, reflect_ray, reflect_payload);
            payload.color += reflect_payload.color;
        }
    }
}