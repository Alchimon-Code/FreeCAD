#version 450 core
// SPDX-License-Identifier: LGPL-2.1-or-later
in vec4 strokeInk;
layout(location = 0) out vec4 color;
void main() { color = strokeInk; }
