#version 440

// Feature 012 (design 012 §5.5): a segment of a result, between two samples
// of a bar, each moved by scale x its displacement; a quad stretched on the
// screen, pass.y pixels wide, as mesh_segment.vert.

layout(location = 0) in vec2 corner;   // per vertex: -1..1
layout(location = 1) in vec3 fromPos;  // per instance, from the origin of the result
layout(location = 2) in vec3 fromDisp;
layout(location = 3) in vec3 toPos;
layout(location = 4) in vec3 toDisp;
layout(location = 5) in vec2 values;   // the quantity at both ends
layout(location = 6) in vec4 flags;    // x * 255: bit 0, selected

layout(location = 0) out float v_value;
layout(location = 1) out float v_flags;

layout(std140, binding = 0) uniform ResultUniforms {
    mat4 viewProjection;
    vec4 offset;         // origin of the result minus the camera target
    vec4 params;         // viewport width, height (pixels), device pixel ratio
    vec4 pass;           // scale, width in logical pixels, 1: the original shape
    vec4 range;          // min, max, bands (0: continuous), 1: colored
    vec4 neutralColor;   // the original shape
    vec4 plainColor;     // not colored
    vec4 highlightColor; // selected
} u;

void main()
{
    float t = 0.5 * (corner.x + 1.0);
    v_value = mix(values.x, values.y, t);
    v_flags = flags.x * 255.0;
    vec3 from = fromPos + u.pass.x * fromDisp;
    vec3 to = toPos + u.pass.x * toDisp;
    vec4 a = u.viewProjection * vec4(from + u.offset.xyz, 1.0);
    vec4 b = u.viewProjection * vec4(to + u.offset.xyz, 1.0);
    if (a.w <= 0.0 || b.w <= 0.0) {
        // An end behind the camera: drop the segment.
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    vec2 pixels = u.params.xy;
    vec2 sa = a.xy / a.w * 0.5 * pixels;
    vec2 sb = b.xy / b.w * 0.5 * pixels;
    vec2 along = sb - sa;
    float len = max(length(along), 1e-6);
    vec2 across = vec2(-along.y, along.x) / len;
    vec4 clip = mix(a, b, t);
    float width = u.pass.y * u.params.z;
    clip.xy += across * corner.y * (width / pixels) * clip.w;
    gl_Position = clip;
}
