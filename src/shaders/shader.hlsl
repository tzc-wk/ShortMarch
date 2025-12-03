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

RaytracingAccelerationStructure as : register(t0, space0);
RWTexture2D<float4> output : register(u0, space1);
ConstantBuffer<CameraInfo> camera_info : register(b0, space2);
StructuredBuffer<Material> materials : register(t0, space3);
ConstantBuffer<HoverInfo> hover_info : register(b0, space4);
RWTexture2D<int> entity_id_output : register(u0, space5);
RWTexture2D<float4> accumulated_color : register(u0, space6);
RWTexture2D<int> accumulated_samples : register(u0, space7);

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

#define MAX_DEPTH 8

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
float3 calcNormal(int material_idx, float3 hit_point) {
  PrimitiveIndex();
  float3 object_center = float3(0, 0, 0);
  if (material_idx == 0) return float3(0, 1, 0);
  else if (material_idx == 1) object_center = float3(-2, 0.5, 0);
  else if (material_idx == 2) object_center = float3(0, 0.5, 0);
  else if (material_idx == 3) object_center = float3(2, 0.5, 0);
  return normalize(hit_point - object_center);
}

float3 CalculateDirectLight(float3 hit_point, float3 normal, Material mat, float3 view_dir) {
  float3 light_pos = float3(3, 5, 2);
  float3 light_color = float3(1.0, 0.95, 0.9);
  float light_intensity = 0.2;
  float3 light_dir = normalize(light_pos - hit_point);
  float ndotl = max(0.0, dot(normal, light_dir));
  float3 diffuse = mat.base_color * ndotl;
  float3 half_vector = normalize(light_dir + view_dir);
  float ndoth = max(0.0, dot(normal, half_vector));
  float specular_power = 32.0 * (1.0 - mat.roughness);
  float specular_intensity = pow(ndoth, specular_power);
  float metallic_factor = 0.2 + mat.metallic * 0.8;
  float3 specular = light_color * specular_intensity * metallic_factor;
  float3 ambient = mat.base_color * 0.2;
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
  RayPayload simple_payload;
  simple_payload.hit = false;
  simple_payload.instance_id = 0;
  TraceRay(as, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, simple_payload);
  entity_id_output[pixel_coords] = simple_payload.hit ? (int)simple_payload.instance_id : -1;
  float4 prev_color = accumulated_color[pixel_coords];
  int prev_samples = accumulated_samples[pixel_coords];
  accumulated_color[pixel_coords] = prev_color + float4(payload.color, 1);
  accumulated_samples[pixel_coords] = prev_samples + 1;
}
[shader("miss")] 
void MissMain(inout RayPayload payload) {
  float t = 0.5 * (normalize(WorldRayDirection()).y + 1.0);
  payload.color = lerp(float3(0.1, 0, 0.05), float3(0.3, 0.1, 0.2), t) * payload.throughput;
  payload.hit = false;
  payload.instance_id = 0xFFFFFFFF;
}
[shader("closesthit")] 
void ClosestHitMain(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
  payload.hit = true;
  uint material_idx = InstanceID();
  payload.instance_id = material_idx;
  payload.hit_distance = RayTCurrent();
  payload.hit_point = WorldRayOrigin() + WorldRayDirection() * payload.hit_distance;
  payload.normal = calcNormal(material_idx, payload.hit_point);
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