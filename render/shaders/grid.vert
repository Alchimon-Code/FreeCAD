#version 450 core
// SPDX-License-Identifier: LGPL-2.1-or-later
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); // one oversized triangle, no world geometry
}
