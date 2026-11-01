// No-op OpenGL stub so spectrogram.cpp compiles without GL (reference harness only).
#pragma once
typedef unsigned int GLuint; typedef int GLint; typedef unsigned int GLenum; typedef int GLsizei;
enum { GL_TEXTURE_2D = 1, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR,
       GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE, GL_RGBA8, GL_RGBA,
       GL_UNSIGNED_BYTE, GL_R8, GL_RED, GL_UNPACK_ALIGNMENT };
inline void glGenTextures(GLsizei, GLuint* t) { *t = 0; }
inline void glDeleteTextures(GLsizei, const GLuint*) {}
inline void glBindTexture(GLenum, GLuint) {}
inline void glTexParameteri(GLenum, GLenum, GLint) {}
inline void glPixelStorei(GLenum, GLint) {}
inline void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) {}
