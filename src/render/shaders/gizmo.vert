#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;

layout(location = 0) out vec3 v_color;

layout(std140, binding = 0) uniform GizmoUniforms {
    mat4 modelViewProjection;
    mat4 rotation; // world to view, rotation only
} u;

void main()
{
    vec3 viewNormal = mat3(u.rotation) * normal;
    float light = 0.55 + 0.45 * max(viewNormal.z, 0.0);
    v_color = color * light;
    gl_Position = u.modelViewProjection * vec4(position, 1.0);
}
