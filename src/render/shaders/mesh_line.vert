#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

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
    gl_Position = u.viewProjection * vec4(position + u.offset.xyz, 1.0);
}
