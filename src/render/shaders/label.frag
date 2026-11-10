#version 440

layout(location = 0) in vec2 v_texCoord;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D atlas;

void main()
{
    fragColor = vec4(v_color, texture(atlas, v_texCoord).a);
}
