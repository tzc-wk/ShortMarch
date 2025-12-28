struct CameraInfo {
    float4x4 screen_to_camera;
    float4x4 camera_to_world;
};
struct Material {
    float3 base_color;
    float roughness;
    float metallic;
    float transmission;
    float ior;
};
struct HoverInfo {
    int hovered_entity_id;
};

RaytracingAccelerationStructure as : register(t0, space0);
RWTexture2D<float4> output : register(u0, space1);
ConstantBuffer<CameraInfo> camera_info : register(b0, space2);
StructuredBuffer<Material> materials : register(t0, space3);
ConstantBuffer<HoverInfo> hover_info : register(b0, space4);
RWTexture2D<int> entity_id_output : register(u0, space5);
RWTexture2D<float4> accumulated_color : register(u0, space6);
RWTexture2D<int> accumulated_samples : register(u0, space7);

uint RandomSeed(uint2 pixel, uint depth, uint frame) {return (pixel.x * 73856093u) ^ (pixel.y * 19349663u) ^ (depth * 83492789u) ^ (frame * 735682483u);}
float Random(inout uint seed) {
    seed = seed * 747796405u + 2891336453u;
    uint result = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    result = (result >> 22u) ^ result;
    return float(result) / 4294967295.0;
}

// =====================================================================================================================================
// ================================================== texture related ==================================================================
// =====================================================================================================================================

ByteAddressBuffer texture_data_buffer : register(t0, space11);
float3 GetTextureColor(uint texture_index, float2 uv, float lev = 0) {
    if (texture_index == 0) {
        int mip = (int)lev;
        float frac = lev - (float)mip;
        mip = min(mip, 10);
        int next_mip = min(mip + 1, 10);
        uint base_offset = 0;
        uint mip_offset = 0;
        uint mip_width = 1024;
        uint mip_height = 1024;
        for (int i = 0; i < mip; ++i) {
            mip_offset += mip_width * mip_height;
            mip_width = mip_width / 2;
            mip_height = mip_height / 2;
        }
        uint next_mip_offset = mip_offset + mip_width * mip_height;
        uint next_mip_width = mip_width / 2;
        uint next_mip_height = mip_height / 2;
        if (next_mip == mip) {
            next_mip_width = mip_width;
            next_mip_height = mip_height;
            next_mip_offset = mip_offset;
        }
        uint offset = base_offset + mip_offset;
        uint width = mip_width;
        uint height = mip_height;
        uint x = uint(uv.x * width) % width;
        uint y = uint(uv.y * height) % height;
        uint pixel_index = offset + y * width + x;
        uint byte_offset = pixel_index * 4 * 4;
        float r0 = asfloat(texture_data_buffer.Load(byte_offset));
        float g0 = asfloat(texture_data_buffer.Load(byte_offset + 4));
        float b0 = asfloat(texture_data_buffer.Load(byte_offset + 8));
        offset = base_offset + next_mip_offset;
        width = next_mip_width;
        height = next_mip_height;
        x = uint(uv.x * width) % width;
        y = uint(uv.y * height) % height;
        pixel_index = offset + y * width + x;
        byte_offset = pixel_index * 4 * 4;
        float r1 = asfloat(texture_data_buffer.Load(byte_offset));
        float g1 = asfloat(texture_data_buffer.Load(byte_offset + 4));
        float b1 = asfloat(texture_data_buffer.Load(byte_offset + 8));
        float r = lerp(r0, r1, frac);
        float g = lerp(g0, g1, frac);
        float b = lerp(b0, b1, frac);
        return float3(r, g, b);
    }
    uint width, height, offset;
    if (texture_index == 1) {width = 1024; height = 1024; offset = 1398101;}
    else if (texture_index == 2) {width = 1024; height = 1024; offset = 2446677;}
    else {width = 1520; height = 760; offset = 3495253;}
    uint x = uint(uv.x * width) % width;
    uint y = uint(uv.y * height) % height;
    uint pixel_index = offset + y * width + x;
    uint byte_offset = pixel_index * 4 * 4;
    float r = asfloat(texture_data_buffer.Load(byte_offset));
    float g = asfloat(texture_data_buffer.Load(byte_offset + 4));
    float b = asfloat(texture_data_buffer.Load(byte_offset + 8));
    return float3(r, g, b);
}
float CalculateGroundMipLevel(float3 hit_point, float3 view_dir, float3 camera_pos) {
    const float GROUND_SIZE = 20.0;
    const float TEXTURE_SIZE = 1024.0;
    const float PIXEL_ANGLE = 0.0003;
    float ray_length = length(hit_point - camera_pos);
    float cos_theta = abs(dot(float3(0,1,0), -view_dir));
    cos_theta = max(cos_theta, 0.001);
    float pixel_world_size = ray_length * PIXEL_ANGLE / cos_theta;
    float texels_per_world_unit = TEXTURE_SIZE / GROUND_SIZE;
    float texel_coverage = pixel_world_size * texels_per_world_unit;
    float mip_level = log2(texel_coverage);
    mip_level += 0.5;
    return clamp(mip_level, 0.0, 10.0);
}

// =====================================================================================================================================
// ================================================== geometry related =================================================================
// =====================================================================================================================================

struct EntityOffset {
    uint vertex_offset;
    uint index_offset;
    uint vertex_count;
    uint index_count;
};
ByteAddressBuffer vertex_buffer : register(t0, space8);
ByteAddressBuffer index_buffer : register(t0, space9);
StructuredBuffer<EntityOffset> entity_offsets : register(t0, space10);
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
    // normal map
    if (instance_id == 100) {
        float ux = (hit_point.x + 10.0) / 20.0;
        float uy = (hit_point.z + 10.0) / 20.0;
        float3 normal = GetTextureColor(0, float2(ux, uy)) - float3(0.5, 0.5, 0.5);
        normal = normalize(normal);
        float3 view_dir = normalize(-WorldRayDirection());
        if (dot(normal, view_dir) < 0.0) normal = -normal;
        return normal;
    }
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
float3 reflect(float3 I, float3 N) {return I - 2.0 * dot(I, N) * N;}
float3 refract(float3 I, float3 N, float eta) {
    float NdotI = dot(N, I);
    float k = 1.0 - eta * eta * (1.0 - NdotI * NdotI);
    if (k < 0.0) {
        return reflect(-I, N);
    }
    return eta * I - (eta * NdotI + sqrt(k)) * N;
}

// =====================================================================================================================================
// ================================================== lighting related =================================================================
// =====================================================================================================================================

struct RayPayload {
    float3 color;
    bool hit;
    uint instance_id;
    float hit_distance;
    uint depth;
    float throughput;
    bool inside_material;
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
static const PointLight POINT_LIGHT = {
    float3(0, 3, 0),
    float3(1.0, 0.95, 0.9),
    0
};
static const AreaLight AREA_LIGHT = {
    float3(0, 4, 0),
    normalize(float3(0, -1, 0)),
    normalize(float3(0, 0, 1)),
    5.0,
    5.0,
    float3(1.0, 0.99, 0.98),
    50.0
};
static const float3 AMBIENT_COLOR = float3(1.0, 1.0, 1.0);
static const float AMBIENT_INTENSITY = 0.2;
static const int AREA_LIGHT_SAMPLES = 1;

bool RussianRoulette(float throughput, inout uint seed) {
    if (throughput < 0.05) {
        float r = Random(seed);
        float continue_prob = 1.0 - exp(-throughput * 15.0);
        continue_prob = clamp(continue_prob, 0.2, 0.95);
        if (r > continue_prob) return false;
    }
    return true;
}

float TestShadow(float3 hit_point, float3 light_pos) {
    float3 light_to_point = hit_point - light_pos;
    float total_distance = length(light_to_point);
    float3 light_dir = normalize(light_to_point);
    if (total_distance < 0.005) return 1.0;
    float transmission_factor = 1.0;
    float current_distance = 0.001;
    float3 ray_origin = light_pos + light_dir * 0.001;
    while (current_distance < total_distance - 0.003) {
        RayPayload shadow_payload;
        shadow_payload.color = float3(0, 0, 0);
        shadow_payload.hit = false;
        shadow_payload.instance_id = 0xFFFFFFFF;
        shadow_payload.hit_distance = 10000.0;
        shadow_payload.depth = 100;
        shadow_payload.throughput = 0.0;
        shadow_payload.inside_material = false;
        RayDesc shadow_ray;
        shadow_ray.Origin = ray_origin;
        shadow_ray.Direction = light_dir;
        shadow_ray.TMin = 0.001;
        shadow_ray.TMax = total_distance - current_distance - 0.001;
        TraceRay(as, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 
                 0xFF, 0, 0, 0, shadow_ray, shadow_payload);
        if (!shadow_payload.hit) break;
        if (shadow_payload.instance_id != 0xFFFFFFFF) {
            Material hit_mat = materials[shadow_payload.instance_id];
            if (hit_mat.transmission > 0.0) {
                transmission_factor *= hit_mat.transmission;
                ray_origin = ray_origin + light_dir * (shadow_payload.hit_distance + 0.001);
                current_distance += shadow_payload.hit_distance + 0.001;
                continue;
            }
            else return 0.0;
        }
        break;
    }
    return transmission_factor;
}
float3 CalculatePointLightContribution(float3 hit_point, float3 normal, Material mat, float3 view_dir, PointLight light) {
    float3 light_dir = normalize(light.position - hit_point);
    float light_distance = length(light.position - hit_point);
    float attenuation = light.intensity / (light_distance * light_distance + 0.001);
    float shadow_factor = TestShadow(hit_point, light.position);
    if (shadow_factor <= 0.001) return float3(0, 0, 0);
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
    return shadow_factor * (diffuse + specular);
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

// =====================================================================================================================================
// ================================================== raytracing related ===============================================================
// =====================================================================================================================================

#define MAX_DEPTH 3
#define PI 3.14159265358979323846
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
    payload.inside_material = false;
    RayDesc ray;
    ray.Origin = origin.xyz; ray.Direction = normalize(direction.xyz);
    ray.TMin = 0.001; ray.TMax = 10000.0;
    TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    output[pixel_coords] = float4(payload.color, 1);
    RayPayload test_payload;
    test_payload.color = float3(0, 0, 0); test_payload.hit = false;
    test_payload.instance_id = 0; test_payload.hit_distance = 10000.0;
    test_payload.depth = 100; test_payload.throughput = 0.0;
    test_payload.inside_material = false;
    TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, test_payload);
    entity_id_output[pixel_coords] = test_payload.hit ? (int)test_payload.instance_id : -1;
    float4 prev_color = accumulated_color[pixel_coords];
    int prev_samples = accumulated_samples[pixel_coords];
    accumulated_color[pixel_coords] = prev_color + float4(payload.color, 1);
    accumulated_samples[pixel_coords] = prev_samples + 1;
}
[shader("miss")]
void MissMain(inout RayPayload payload) {
    if (payload.depth == 100) {
        payload.color = float3(0, 0, 0);
        payload.hit = false;
        payload.hit_distance = 10000.0;
        payload.instance_id = 0xFFFFFFFF;
        return;
    }
    float3 ray_dir = normalize(WorldRayDirection());
    float u = 0.5 + atan2(ray_dir.z, ray_dir.x) / (2.0 * PI);
    float v = 0.5 - asin(ray_dir.y) / PI;
    float2 uv = float2(u, v);
    float3 sky_color = GetTextureColor(3, uv);
    payload.color = sky_color * payload.throughput;
    payload.hit = false;
    payload.hit_distance = 10000.0;
    payload.instance_id = 0xFFFFFFFF;
}
[shader("closesthit")]
void ClosestHitMain(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    uint material_idx = InstanceID(), primitive_index = PrimitiveIndex(); 
    Material mat = materials[material_idx];
    payload.hit = true; 
    payload.instance_id = material_idx; 
    payload.hit_distance = RayTCurrent();
    if (payload.depth == 100) return; // test ray
    float3 hit_point = WorldRayOrigin() + WorldRayDirection() * payload.hit_distance;
    float3 norm = calcNormal(material_idx, primitive_index, hit_point);
    float3 view_dir = normalize(-WorldRayDirection());
    uint2 pixel_coords = DispatchRaysIndex().xy;
    uint seed = RandomSeed(pixel_coords, payload.depth, accumulated_samples[pixel_coords]);
    // color texture for the ground
    if (material_idx == 0) {
        float3 camera_pos = WorldRayOrigin();
        float3 view_dir = normalize(-WorldRayDirection());
        float mip_lev = CalculateGroundMipLevel(hit_point, view_dir, camera_pos);
        float ux = (hit_point.x + 10.0) / 20.0;
        float uy = (hit_point.z + 10.0) / 20.0;
        mat.base_color = GetTextureColor(0, float2(ux, uy), mip_lev);
    }
    if (material_idx == 100) {
        float3 local_pos = hit_point - float3(0.0f, 0.5f, 0.0f);
        float3 abs_local = abs(local_pos);
        float2 uv;
        if (abs_local.x > abs_local.y && abs_local.x > abs_local.z) {
            uv = float2(local_pos.z, local_pos.y) / 2.0 + 0.5;
            if (local_pos.x < 0) uv.x = 1.0 - uv.x;
        } else if (abs_local.y > abs_local.z) {
            uv = float2(local_pos.x, local_pos.z) / 2.0 + 0.5;
            if (local_pos.y < 0) uv.y = 1.0 - uv.y;
        } else {
            uv = float2(local_pos.x, local_pos.y) / 2.0 + 0.5;
            if (local_pos.z < 0) uv.x = 1.0 - uv.x;
        }
        float3 tex_color = GetTextureColor(0, uv);
        mat.base_color = tex_color;
    }
    // height map for the ground
    if (material_idx == 100) {
        float ux = (hit_point.x + 10.0) / 20.0;
        float uy = (hit_point.z + 10.0) / 20.0;
        float3 height_col = GetTextureColor(2, float2(ux, uy));
        float height = (height_col.x + height_col.y + height_col.z) / 3.0;
        hit_point = hit_point + 15 * (1 - height) * norm;
    }
    float3 direct_light = CalculateDirectLight(hit_point, norm, mat, view_dir, seed);
    payload.color = direct_light * payload.throughput;
    if (payload.depth < MAX_DEPTH) {
        float reflectivity = min((1.0 - mat.roughness) * (0.3 + mat.metallic * 0.8), 1.0);
        if (reflectivity < 0.01) return;
        float reflection_prob = reflectivity;
        float refraction_prob = mat.transmission * (1.0 - reflectivity);
        float absorption_prob = 1.0 - reflection_prob - refraction_prob;
        absorption_prob = max(absorption_prob, 0.0);
        bool should_trace = RussianRoulette(payload.throughput * reflectivity, seed);
        if (!should_trace) return;
        float random_val = Random(seed);
        if (random_val < reflection_prob) {
            float3 reflect_dir = reflect(-view_dir, norm);
            RayDesc reflect_ray;
            reflect_ray.Origin = hit_point + norm * 0.001;
            reflect_ray.Direction = reflect_dir;
            reflect_ray.TMin = 0.001;
            reflect_ray.TMax = 10000.0;
            RayPayload reflect_payload;
            reflect_payload.color = float3(0, 0, 0);
            reflect_payload.hit = false;
            reflect_payload.instance_id = 0;
            reflect_payload.hit_distance = 0.0;
            reflect_payload.depth = payload.depth + 1;
            reflect_payload.throughput = payload.throughput * reflectivity;
            reflect_payload.inside_material = payload.inside_material;
            TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, reflect_ray, reflect_payload);
            payload.color += reflect_payload.color;
        }
        else if (random_val < reflection_prob + refraction_prob && refraction_prob > 0.001) {
            float3 refract_dir;
            if (payload.inside_material) refract_dir = refract(-view_dir, norm, 1.0 / mat.ior);
            else refract_dir = refract(-view_dir, norm, mat.ior);
            if (dot(refract_dir, refract_dir) < 0.001) refract_dir = reflect(-view_dir, norm);
            refract_dir = normalize(refract_dir);
            RayDesc refract_ray;
            refract_ray.Origin = hit_point - norm * 0.001;
            refract_ray.Direction = refract_dir;
            refract_ray.TMin = 0.001;
            refract_ray.TMax = 10000.0;
            RayPayload refract_payload;
            refract_payload.color = float3(0, 0, 0);
            refract_payload.hit = false;
            refract_payload.instance_id = 0;
            refract_payload.hit_distance = 0.0;
            refract_payload.depth = payload.depth + 1;
            refract_payload.throughput = payload.throughput * mat.transmission * (1.0 - reflectivity);
            refract_payload.inside_material = !payload.inside_material;
            TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, refract_ray, refract_payload);
            payload.color += refract_payload.color;
        }
        else return;
    }
}