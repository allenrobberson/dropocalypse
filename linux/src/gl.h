// Dropocalypse for Linux - OpenGL 3.3 core declarations.
// libGL on Linux (Mesa, NVIDIA) exports every core function, so the prototypes from glext.h are
// enough: no loader library is needed.
#pragma once

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
