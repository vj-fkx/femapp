#version 440

layout(location = 0) in vec3 anchor;
layout(location = 1) in vec2 corner;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 color;

layout(location = 0) out vec2 v_texCoord;
layout(location = 1) out vec3 v_color;

layout(std140, binding = 0) uniform LabelUniforms {
    mat4 clipCorrection;      // backend-specific clip space fix-up
    mat4 modelViewProjection; // without the clip space fix-up
    vec4 halfSize;            // xy: half-size of a label in clip space
} u;

void main()
{
    v_texCoord = texCoord;
    v_color = color;
    // The label is offset in clip space, before the backend fix-up, so that it
    // always faces the camera and stays upright on every backend.
    vec4 clip = u.modelViewProjection * vec4(anchor, 1.0);
    clip.xy += corner * u.halfSize.xy * clip.w;
    gl_Position = u.clipCorrection * clip;
}
