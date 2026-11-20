#version 450 core
// SPDX-License-Identifier: LGPL-2.1-or-later
layout(location = 0) in vec2 point; // relative to upload eye, or top-left pixel in overlay
layout(location = 1) in vec4 ink;   // normalized RGBA8
layout(location = 0) uniform mat3 toClip;
layout(location = 3) uniform float gripSize;
out vec4 strokeInk;
void main() {
    gl_Position = vec4((toClip * vec3(point, 1.0)).xy, 0.0, 1.0);
    gl_PointSize = gripSize;
    strokeInk = ink;
}
