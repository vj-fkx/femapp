#version 440

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_viewPosition;

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
    // Flat shading from the face normal in view space. The absolute value
    // lights both sides of shells and ignores the sign convention of the
    // screen derivatives, which differs between backends.
    vec3 normal = normalize(cross(dFdx(v_viewPosition), dFdy(v_viewPosition)));
    float light = 0.35 + 0.65 * abs(normal.z);

    // Alpha carries flags: bit 0 = selected.
    int flags = int(v_color.a * 255.0 + 0.5);
    vec3 base = (flags & 1) != 0 ? u.highlightColor.rgb : v_color.rgb;
    fragColor = vec4(base * light, 1.0);
}
