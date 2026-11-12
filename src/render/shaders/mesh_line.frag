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
    // Alpha carries flags: bit 0 = selected, bit 1 = the line is a 1D element
    // (always in its own color); other lines are edges of faces, dark unless
    // in wireframe mode.
    int flags = int(v_color.a * 255.0 + 0.5);
    vec3 color;
    if ((flags & 1) != 0) {
        color = u.highlightColor.rgb;
    } else if ((flags & 2) != 0 || u.params.w > 0.5) {
        color = v_color.rgb;
    } else {
        color = u.edgeColor.rgb;
    }
    fragColor = vec4(color, 1.0);
}
