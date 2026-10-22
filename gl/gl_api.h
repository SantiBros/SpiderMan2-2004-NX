#pragma once
// The OpenGL 4.3 core functions the renderer calls, loaded through eglGetProcAddress: the same code on the Switch
// (Mesa's nouveau driver) and on Linux (any Mesa driver, llvmpipe for the headless test build).
#include <GL/glcorearb.h>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// X(return, name, parameters)
#define GL_FUNCTIONS(X)                                                                                          \
    X(void, glActiveTexture, (GLenum))                                                                           \
    X(void, glAttachShader, (GLuint, GLuint))                                                                    \
    X(void, glBeginQuery, (GLenum, GLuint))                                                                      \
    X(void, glBindBuffer, (GLenum, GLuint))                                                                      \
    X(void, glBindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr))                                   \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                                                 \
    X(void, glBindSampler, (GLuint, GLuint))                                                                     \
    X(void, glBindTexture, (GLenum, GLuint))                                                                     \
    X(void, glBindVertexArray, (GLuint))                                                                         \
    X(void, glBindVertexBuffer, (GLuint, GLuint, GLintptr, GLsizei))                                             \
    X(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                                  \
    X(void, glBlendEquation, (GLenum))                                                                           \
    X(void, glBlendFunc, (GLenum, GLenum))                                                                       \
    X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                               \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))     \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                             \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                                        \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                                                \
    X(void, glClear, (GLbitfield))                                                                               \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                                  \
    X(void, glClearDepth, (GLdouble))                                                                            \
    X(void, glClearStencil, (GLint))                                                                             \
    X(GLenum, glClientWaitSync, (GLsync, GLbitfield, GLuint64))                                                  \
    X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                                           \
    X(void, glCompileShader, (GLuint))                                                                           \
    X(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*))      \
    X(void, glCompressedTexImage3D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const void*)) \
    X(void, glCopyImageSubData, (GLuint, GLenum, GLint, GLint, GLint, GLint, GLuint, GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei)) \
    X(GLuint, glCreateProgram, (void))                                                                           \
    X(GLuint, glCreateShader, (GLenum))                                                                          \
    X(void, glCullFace, (GLenum))                                                                                \
    X(void, glDeleteBuffers, (GLsizei, const GLuint*))                                                           \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint*))                                                      \
    X(void, glDeleteProgram, (GLuint))                                                                           \
    X(void, glDeleteQueries, (GLsizei, const GLuint*))                                                           \
    X(void, glDeleteSamplers, (GLsizei, const GLuint*))                                                          \
    X(void, glDeleteShader, (GLuint))                                                                            \
    X(void, glDeleteSync, (GLsync))                                                                              \
    X(void, glDeleteTextures, (GLsizei, const GLuint*))                                                          \
    X(void, glDepthFunc, (GLenum))                                                                               \
    X(void, glDepthMask, (GLboolean))                                                                            \
    X(void, glDisable, (GLenum))                                                                                 \
    X(void, glDisableVertexAttribArray, (GLuint))                                                                \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei))                                                              \
    X(void, glDrawBuffer, (GLenum))                                                                              \
    X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void*))                                              \
    X(void, glDrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void*, GLint))                             \
    X(void, glEnable, (GLenum))                                                                                  \
    X(void, glEnableVertexAttribArray, (GLuint))                                                                 \
    X(void, glEndQuery, (GLenum))                                                                                \
    X(GLsync, glFenceSync, (GLenum, GLbitfield))                                                                 \
    X(void, glFinish, (void))                                                                                    \
    X(void, glFlush, (void))                                                                                     \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                     \
    X(void, glFrontFace, (GLenum))                                                                               \
    X(void, glGenBuffers, (GLsizei, GLuint*))                                                                    \
    X(void, glGenFramebuffers, (GLsizei, GLuint*))                                                               \
    X(void, glGenQueries, (GLsizei, GLuint*))                                                                    \
    X(void, glGenSamplers, (GLsizei, GLuint*))                                                                   \
    X(void, glGenTextures, (GLsizei, GLuint*))                                                                   \
    X(void, glGenVertexArrays, (GLsizei, GLuint*))                                                               \
    X(GLenum, glGetError, (void))                                                                                \
    X(void, glGetIntegerv, (GLenum, GLint*))                                                                     \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                           \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint*))                                                            \
    X(void, glGetQueryObjectuiv, (GLuint, GLenum, GLuint*))                                                      \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                            \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint*))                                                             \
    X(const GLubyte*, glGetString, (GLenum))                                                                     \
    X(const GLubyte*, glGetStringi, (GLenum, GLuint))                                                            \
    X(GLuint, glGetUniformBlockIndex, (GLuint, const GLchar*))                                                   \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar*))                                                      \
    X(void, glLineWidth, (GLfloat))                                                                              \
    X(void, glLinkProgram, (GLuint))                                                                             \
    X(void*, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield))                                       \
    X(void, glPixelStorei, (GLenum, GLint))                                                                      \
    X(void, glPointSize, (GLfloat))                                                                              \
    X(void, glPolygonMode, (GLenum, GLenum))                                                                     \
    X(void, glPolygonOffset, (GLfloat, GLfloat))                                                                 \
    X(void, glReadBuffer, (GLenum))                                                                              \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                               \
    X(void, glSamplerParameterf, (GLuint, GLenum, GLfloat))                                                      \
    X(void, glSamplerParameterfv, (GLuint, GLenum, const GLfloat*))                                              \
    X(void, glSamplerParameteri, (GLuint, GLenum, GLint))                                                        \
    X(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                                                         \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                               \
    X(void, glStencilFunc, (GLenum, GLint, GLuint))                                                              \
    X(void, glStencilMask, (GLuint))                                                                             \
    X(void, glStencilOp, (GLenum, GLenum, GLenum))                                                               \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))          \
    X(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint))                                                            \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))       \
    X(void, glUniform1i, (GLint, GLint))                                                                         \
    X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                            \
    X(void, glUniformBlockBinding, (GLuint, GLuint, GLuint))                                                     \
    X(GLboolean, glUnmapBuffer, (GLenum))                                                                        \
    X(void, glUseProgram, (GLuint))                                                                              \
    X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))                                      \
    X(void, glVertexAttribBinding, (GLuint, GLuint))                                                             \
    X(void, glVertexAttribFormat, (GLuint, GLint, GLenum, GLboolean, GLuint))                                    \
    X(void, glVertexAttribIFormat, (GLuint, GLint, GLenum, GLuint))                                              \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))                     \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))

// newer than 4.3: used when the version or an extension says the driver has them
#define GL_OPTIONAL_FUNCTIONS(X)                                                    \
    X(void, glBufferStorage, (GLenum, GLsizeiptr, const void*, GLbitfield))        \
    /* diagnostics: GLPROBE_RAW's vertex positions */                              \
    X(void, glTransformFeedbackVaryings, (GLuint, GLsizei, const char* const*, GLenum)) \
    X(void, glBeginTransformFeedback, (GLenum))                                     \
    X(void, glEndTransformFeedback, ())                                             \
    X(void, glBindBufferBase, (GLenum, GLuint, GLuint))                             \
    X(void, glGetBufferSubData, (GLenum, GLintptr, GLsizeiptr, void*))

namespace glapi {

#define GLAPI_DECLARE(ret, name, params) extern ret(APIENTRY* name) params;
GL_FUNCTIONS(GLAPI_DECLARE)
GL_OPTIONAL_FUNCTIONS(GLAPI_DECLARE)
#undef GLAPI_DECLARE

// every function above through get (eglGetProcAddress), false naming the first one missing; the optional ones may
// stay null (and Mesa gives stubs for any name: the version or extension tells whether they work)
bool Load(void* (*get)(const char*), const char** missing);
// GL_EXTENSIONS holds this one
bool HasExtension(const char* name);

}  // namespace glapi

// the functions are called by their GL names
#define GLAPI_ALIAS(ret, name, params) using glapi::name;
GL_FUNCTIONS(GLAPI_ALIAS)
GL_OPTIONAL_FUNCTIONS(GLAPI_ALIAS)
#undef GLAPI_ALIAS
