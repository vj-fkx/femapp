#version 440

// Thick segments (selected solid edges, REQ-004-26): a quad stretched between
// the two ends on the screen, params.w pixels wide.

layout(location = 0) in vec2 corner; // per vertex: -1..1
layout(location = 1) in vec3 from;   // per instance
layout(location = 2) in vec3 to;     // per instance
layout(location = 3) in vec4 color;  // per instance

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform MeshUniforms {
    mat4 viewProjection;
    mat4 view;
    vec4 offset;
    vec4 params; // viewport width, height, point size, wireframe
    vec4 edgeColor;
    vec4 highlightColor;
} u;

const float kWidthPx = 3.0;

void main()
{
    v_color = color;
    vec4 a = u.viewProjection * vec4(from + u.offset.xyz, 1.0);
    vec4 b = u.viewProjection * vec4(to + u.offset.xyz, 1.0);
    if (a.w <= 0.0 || b.w <= 0.0) {
        // An end behind the camera: drop the segment (design §12.10).
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec2 pixels = u.params.xy;
    vec2 sa = a.xy / a.w * 0.5 * pixels;
    vec2 sb = b.xy / b.w * 0.5 * pixels;
    vec2 along = sb - sa;
    float len = max(length(along), 1e-6);
    vec2 across = vec2(-along.y, along.x) / len;
    float t = 0.5 * (corner.x + 1.0);
    vec4 clip = mix(a, b, t);
    // params.z is the point size, 6 px times the device pixel ratio; the
    // width follows the same ratio.
    float width = kWidthPx * u.params.z / 6.0;
    clip.xy += across * corner.y * (width / pixels) * clip.w;
    gl_Position = clip;
}
