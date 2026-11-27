#version 440

// Feature 012 (design 012 §5.5): a selected node of a result, at its
// sample moved by scale x its displacement: a square of a fixed size on the
// screen, as mesh_point.vert.

layout(location = 0) in vec2 corner;       // per vertex: -1..1
layout(location = 1) in vec3 position;     // per instance, from the origin of the result
layout(location = 2) in vec3 displacement; // per instance

layout(std140, binding = 0) uniform ResultUniforms {
    mat4 viewProjection;
    vec4 offset;
    vec4 params; // viewport width, height (pixels), device pixel ratio
    vec4 pass;   // scale
    vec4 range;
    vec4 neutralColor;
    vec4 plainColor;
    vec4 highlightColor;
} u;

const float kPointSizePx = 6.0;

void main()
{
    vec4 clip = u.viewProjection * vec4(position + u.pass.x * displacement + u.offset.xyz, 1.0);
    clip.xy += corner * (kPointSizePx * u.params.z / u.params.xy) * clip.w;
    gl_Position = clip;
}
