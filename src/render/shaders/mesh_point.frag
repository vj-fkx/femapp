#version 440

layout(location = 0) in vec4 v_color;

layout(location = 0) out vec4 fragColor;

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
    int flags = int(v_color.a * 255.0 + 0.5);
    fragColor = vec4((flags & 1) != 0 ? u.highlightColor.rgb : v_color.rgb, 1.0);
}
