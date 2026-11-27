#version 440

// Feature 012: the selected nodes of a result, in the highlight color.

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform ResultUniforms {
    mat4 viewProjection;
    vec4 offset;
    vec4 params;
    vec4 pass;
    vec4 range;
    vec4 neutralColor;
    vec4 plainColor;
    vec4 highlightColor;
} u;

void main()
{
    fragColor = vec4(u.highlightColor.rgb, 1.0);
}
