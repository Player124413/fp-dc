/* Minimal GLES3 declarations, enough to syntax-check the Android renderer on a host without the
 * NDK.  Generated from the gl* and GL_* tokens the two backends use (see README in this folder).
 * Never used for a real build: there the NDK headers are. */
#ifndef FP_NDKSTUB_GLES3
#define FP_NDKSTUB_GLES3

typedef unsigned int GLuint; typedef int GLint; typedef unsigned int GLenum; typedef float GLfloat;
typedef int GLsizei; typedef unsigned char GLboolean; typedef char GLchar; typedef long GLintptr;
typedef long GLsizeiptr; typedef unsigned char GLubyte; typedef unsigned int GLbitfield; typedef double GLdouble;
typedef float GLclampf; typedef void GLvoid;
#define GL_ARRAY_BUFFER 0x1001
#define GL_BLEND 0x1002
#define GL_CLAMP_TO_EDGE 0x1003
#define GL_COLOR_ATTACHMENT0 0x1004
#define GL_COLOR_BUFFER_BIT 0x1005
#define GL_COMPILE_STATUS 0x1006
#define GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x1007
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x1008
#define GL_CULL_FACE 0x1009
#define GL_DEPTH_TEST 0x100a
#define GL_DITHER 0x100b
#define GL_ELEMENT_ARRAY_BUFFER 0x100c
#define GL_FALSE 0
#define GL_FLOAT 0x100e
#define GL_FRAGMENT_SHADER 0x100f
#define GL_FRAMEBUFFER 0x1010
#define GL_FRAMEBUFFER_COMPLETE 0x1011
#define GL_LINEAR 0x1012
#define GL_LINK_STATUS 0x1013
#define GL_NEAREST 0x1014
#define GL_NO_ERROR 0x1015
#define GL_ONE 0x1016
#define GL_ONE_MINUS_SRC_ALPHA 0x1017
#define GL_ONE_MINUS_SRC_COLOR 0x1018
#define GL_R8 0x1019
#define GL_RED 0x101a
#define GL_RGBA 0x101b
#define GL_RGBA8 0x101c
#define GL_SCISSOR_TEST 0x101d
#define GL_SRC_ALPHA 0x101e
#define GL_STATIC_DRAW 0x101f
#define GL_STREAM_DRAW 0x1020
#define GL_TEXTURE0 0x1021
#define GL_TEXTURE1 0x1022
#define GL_TEXTURE_2D 0x1023
#define GL_TEXTURE_MAG_FILTER 0x1024
#define GL_TEXTURE_MIN_FILTER 0x1025
#define GL_TEXTURE_WRAP_S 0x1026
#define GL_TEXTURE_WRAP_T 0x1027
#define GL_TRIANGLES 0x1028
#define GL_TRUE 1
#define GL_UNPACK_ALIGNMENT 0x102a
#define GL_UNSIGNED_BYTE 0x102b
#define GL_UNSIGNED_SHORT 0x102c
#define GL_VERTEX_SHADER 0x102d
#define GL_ZERO 0x102e
int glActiveTexture();
int glAttachShader();
int glBindBuffer();
int glBindFramebuffer();
int glBindTexture();
int glBlendFunc();
int glBufferData();
int glBufferSubData();
int glCheckFramebufferStatus();
int glClear();
int glClearColor();
int glCompileShader();
int glCompressedTexImage2D();
int glCreateProgram();
int glCreateShader();
int glDeleteBuffers();
int glDeleteFramebuffers();
int glDeleteProgram();
int glDeleteShader();
int glDeleteTextures();
int glDisable();
int glDisableVertexAttribArray();
int glDrawElements();
int glEnable();
int glEnableVertexAttribArray();
int glFramebufferTexture2D();
int glGenBuffers();
int glGenVertexArrays();
int glBindVertexArray();
int glDeleteVertexArrays();
int glGenFramebuffers();
int glGenTextures();
int glGetError();
int glGetProgramInfoLog();
int glGetProgramiv();
int glGetShaderInfoLog();
int glGetShaderiv();
int glGetUniformLocation();
int glLinkProgram();
int glPixelStorei();
int glShaderSource();
int glTexImage2D();
int glTexParameteri();
int glTexSubImage2D();
int glUniform1f();
int glUniform1i();
int glUniform2f();
int glUseProgram();
int glVertexAttribPointer();
int glViewport();
const char *glGetString(unsigned int name);
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#endif
