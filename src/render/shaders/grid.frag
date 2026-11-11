#version 440

layout(location = 0) in vec2 v_relative;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform GridUniforms {
    mat4 viewProjection;
    vec4 plane;
    vec4 spacing;
    vec4 axis;
} u;

const vec3 kLineColor = vec3(0.62, 0.64, 0.68);
const vec3 kXAxisColor = vec3(0.86, 0.24, 0.24);
const vec3 kYAxisColor = vec3(0.30, 0.72, 0.30);
const float kMinorOpacity = 0.30;
const float kMajorOpacity = 0.60;
const float kAxisOpacity = 0.95;
const float kAxisHalfWidthPx = 1.25;

// Opacity of a grid with unit cells in `coord`: 1 while the cells are wide on
// screen, going to 0 once they get too small, which is what happens towards
// the horizon.
float densityFade(vec2 coord)
{
    vec2 width = fwidth(coord);
    return 1.0 - smoothstep(0.25, 0.6, max(width.x, width.y));
}

// Coverage of the lines of a grid with unit cells, anti-aliased with screen
// derivatives and faded towards the horizon.
float gridLines(vec2 coord)
{
    vec2 width = max(fwidth(coord), vec2(1e-6));
    vec2 distanceInPixels = abs(fract(coord - 0.5) - 0.5) / width;
    float line = 1.0 - min(min(distanceInPixels.x, distanceInPixels.y), 1.0);
    return line * densityFade(coord);
}

// Coverage of the single line where `value` is zero.
float axisLine(float value)
{
    float width = max(fwidth(value), 1e-12);
    return 1.0 - min(abs(value) / (width * kAxisHalfWidthPx), 1.0);
}

void main()
{
    vec2 world = v_relative + u.plane.xy;

    float minor = gridLines(world / u.spacing.x) * u.spacing.z * kMinorOpacity;
    float major = gridLines(world / u.spacing.y) * kMajorOpacity;
    vec3 color = kLineColor;
    float alpha = max(minor, major);

    // The X axis is the line y = 0; the Y axis is the line x = 0.
    // The axes fade towards the horizon together with the major lines, so they
    // do not stick out beyond the visible grid.
    vec2 absolute = v_relative + u.axis.xy;
    float axisFade = densityFade(world / u.spacing.y);
    float xAxis = axisLine(absolute.y) * axisFade;
    float yAxis = axisLine(absolute.x) * axisFade;
    color = mix(color, kXAxisColor, xAxis);
    color = mix(color, kYAxisColor, yAxis);
    alpha = max(alpha, max(xAxis, yAxis) * kAxisOpacity);

    float fade = 1.0 - smoothstep(0.5, 1.0, length(v_relative) / u.plane.w);
    fragColor = vec4(color, alpha * fade);
}
