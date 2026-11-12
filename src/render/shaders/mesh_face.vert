#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_viewPosition;

layout(std140, binding = 0) uniform MeshUniforms {
    mat4 viewProjection; // corrected, for positions relative to the camera target
    mat4 view;           // view matrix relative to the camera target
    vec4 offset;         // xyz: scene origin minus camera target
    vec4 params;         // x, y: target size in pixels; z: point size in pixels; w: 1 = wireframe
    vec4 edgeColor;
    vec4 highlightColor;
} u;

void main()
{
    vec4 p = vec4(position + u.offset.xyz, 1.0);
    v_viewPosition = (u.view * p).xyz;
    v_color = color;
    gl_Position = u.viewProjection * p;
}
