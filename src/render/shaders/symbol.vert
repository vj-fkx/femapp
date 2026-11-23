#version 440

// Feature 010 (design 010 §5.6): a mold of lines placed at each instance
// with its basis, scaled to a constant size in pixels.

layout(location = 0) in vec4 mold;      // xyz in units of the size; w = 1: y in world units
layout(location = 1) in vec4 moldColor; // alpha 0: the color of the instance
layout(location = 2) in vec3 at;
layout(location = 3) in vec3 axisX;
layout(location = 4) in vec3 axisY;
layout(location = 5) in vec3 axisZ;
layout(location = 6) in float size;     // logical pixels
layout(location = 7) in vec4 color;

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform SymbolUniforms {
    mat4 viewProjection;
    vec4 offset;  // origin of the symbols minus the camera target
    vec4 params;  // 2 / height in pixels, |P11|, device pixel ratio
} u;

void main()
{
    vec3 anchor = at + u.offset.xyz;
    vec4 clip = u.viewProjection * vec4(anchor, 1.0);
    // The length of a pixel at the depth of the anchor.
    float pixel = abs(clip.w) * u.params.x / max(u.params.y, 1e-12);
    float scale = size * u.params.z * pixel;
    float yScale = mold.w > 0.5 ? 1.0 : scale;
    vec3 p = anchor + scale * (mold.x * axisX + mold.z * axisZ) + yScale * mold.y * axisY;
    v_color = moldColor.a > 0.0 ? vec4(moldColor.rgb, 1.0) : vec4(color.rgb, 1.0);
    gl_Position = u.viewProjection * vec4(p, 1.0);
}
