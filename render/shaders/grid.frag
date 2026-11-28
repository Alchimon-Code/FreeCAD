#version 450 core
// SPDX-License-Identifier: LGPL-2.1-or-later
layout(location = 0) uniform vec2 halfFrame;
layout(location = 1) uniform vec2 majorPhase; // remainder(eye, major world spacing) in physical pixels
layout(location = 2) uniform float spacing;
layout(location = 3) uniform vec2 axisPixel;  // axis position relative to screen centre, bounded on CPU
layout(location = 4) uniform vec3 paper;
layout(location = 0) out vec4 color;
float grid(vec2 p, float stepPx) {
    vec2 d = abs(fract(p / stepPx + 0.5) - 0.5) * stepPx;
    return 1.0 - smoothstep(0.0, 1.1, min(d.x, d.y));
}
void main() {
    vec2 p = gl_FragCoord.xy - halfFrame;
    float minor = grid(p + majorPhase, spacing);
    float major = grid(p + majorPhase, 5.0 * spacing);
    vec2 d = abs(p - axisPixel);
    float axes = 1.0 - smoothstep(0.0, 1.3, min(d.x, d.y));
    color = vec4(mix(paper, vec3(0.63, 0.73, 0.80), max(0.42 * axes, max(0.12 * minor, 0.24 * major))), 1.0);
}
