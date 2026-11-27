#version 440

// Feature 012 (design 012 §5.5): the color of a result: the value in the
// range, continuous or the centre of its band (render::bandOf), from the
// scale texture; plain when not colored or without a value.

layout(location = 0) in float v_value;
layout(location = 1) in float v_flags;

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

layout(binding = 1) uniform sampler2D colormap;

void main()
{
    if (u.pass.z > 0.5) {
        fragColor = vec4(u.neutralColor.rgb, 1.0);
        return;
    }
    int flags = int(v_flags + 0.5);
    if ((flags & 1) != 0) {
        fragColor = vec4(u.highlightColor.rgb, 1.0);
        return;
    }
    if (u.range.w < 0.5 || isnan(v_value) || isinf(v_value)) {
        fragColor = vec4(u.plainColor.rgb, 1.0);
        return;
    }
    float span = u.range.y - u.range.x;
    float t = span > 0.0 ? clamp((v_value - u.range.x) / span, 0.0, 1.0) : 0.0;
    float bands = u.range.z;
    if (bands >= 1.0) {
        float band = min(floor(t * bands), bands - 1.0);
        t = (band + 0.5) / bands;
    }
    // Texel i holds the color at i / 255.
    fragColor = vec4(texture(colormap, vec2((t * 255.0 + 0.5) / 256.0, 0.5)).rgb, 1.0);
}
