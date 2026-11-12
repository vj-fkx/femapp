#version 440

layout(location = 0) in vec2 corner;   // per vertex: -1..1
layout(location = 1) in vec3 position; // per instance
layout(location = 2) in vec4 color;    // per instance

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform MeshUniforms {
    mat4 viewProjection;
    mat4 view;
    vec4 offset;
    vec4 params;
    vec4 edgeColor;
    vec4 highlightColor;
} u;

void main()
{
    v_color = color;
    vec4 clip = u.viewProjection * vec4(position + u.offset.xyz, 1.0);
    // A square of params.z pixels, whatever the distance (REQ-004-09).
    clip.xy += corner * (u.params.z / u.params.xy) * clip.w;
    gl_Position = clip;
}
