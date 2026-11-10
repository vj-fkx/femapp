#version 440

layout(location = 0) in vec2 corner;

layout(location = 0) out vec2 v_relative;

layout(std140, binding = 0) uniform GridUniforms {
    mat4 viewProjection; // for positions relative to the camera target
    vec4 plane;          // xy: target position modulo the major spacing,
                         // z: height of the grid plane relative to the target,
                         // w: half-size of the grid quad
    vec4 spacing;        // x: minor spacing, y: major spacing, z: minor fade
    vec4 axis;           // xy: target position (to locate the world axes)
} u;

void main()
{
    v_relative = corner * u.plane.w;
    gl_Position = u.viewProjection * vec4(v_relative, u.plane.z, 1.0);
}
