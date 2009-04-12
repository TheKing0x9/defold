///////////////////////////////////////////////////////////////////////////////////
//
//	graphics_device.h - Graphics device interface
//
///////////////////////////////////////////////////////////////////////////////////
#ifndef __GRAPHICSDEVICE_H__
#define __GRAPHICSDEVICE_H__


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "opengl/opengl_device.h"

// primitive type
enum GFXPrimitiveType
{
    GFX_PRIMITIVE_POINTLIST         = GFX_DEVICE_PRIMITIVE_POINTLIST,
    GFX_PRIMITIVE_LINES             = GFX_DEVICE_PRIMITIVE_LINES,
    GFX_PRIMITIVE_LINE_LOOP         = GFX_DEVICE_PRIMITIVE_LINE_LOOP,
    GFX_PRIMITIVE_LINE_STRIP        = GFX_DEVICE_PRIMITIVE_LINE_STRIP,
    GFX_PRIMITIVE_TRIANGLES         = GFX_DEVICE_PRIMITIVE_TRIANGLES,
    GFX_PRIMITIVE_TRIANGLE_STRIP    = GFX_DEVICE_PRIMITIVE_TRIANGLE_STRIP,
    GFX_PRIMITIVE_TRIANGLE_FAN      = GFX_DEVICE_PRIMITIVE_TRIANGLE_FAN
};

// buffer clear types
enum GFXBufferClear
{
    GFX_CLEAR_COLOUR_BUFFER         = GFX_DEVICE_CLEAR_COLOURUFFER,
    GFX_CLEAR_DEPTH_BUFFER          = GFX_DEVICE_CLEAR_DEPTHBUFFER,
    GFX_CLEAR_STENCIL_BUFFER        = GFX_DEVICE_CLEAR_STENCILBUFFER
};

// bool states
enum GFXRenderState
{
    GFX_DEPTH_TEST                  = GFX_DEVICE_STATE_DEPTH_TEST,
    GFX_ALPHA_BLEND                 = GFX_DEVICE_STATE_ALPHA_TEST
};

enum GFXMatrixMode
{
    GFX_MATRIX_TYPE_WORLD       = GFX_DEVICE_MATRIX_TYPE_WORLD,
    GFX_MATRIX_TYPE_VIEW        = GFX_DEVICE_MATRIX_TYPE_VIEW,
    GFX_MATRIX_TYPE_PROJECTION  = GFX_DEVICE_MATRIX_TYPE_PROJECTION
};

// Types
enum GFXType
{
    GFX_TYPE_BYTE               = GFX_DEVICE_TYPE_BYTE,
    GFX_TYPE_UNSIGNED_BYTE      = GFX_DEVICE_TYPE_UNSIGNED_BYTE,
    GFX_TYPE_SHORT              = GFX_DEVICE_TYPE_SHORT,
    GFX_TYPE_UNSIGNED_SHORT     = GFX_DEVICE_TYPE_UNSIGNED_SHORT,
    GFX_TYPE_INT                = GFX_DEVICE_TYPE_INT,
    GFX_TYPE_UNSIGNED_INT       = GFX_DEVICE_TYPE_UNSIGNED_INT,
    GFX_TYPE_FLOAT              = GFX_DEVICE_TYPE_FLOAT,
};

// Parameter structure for CreateDevice
struct GFXSCreateDeviceParams
{
    uint32_t        m_DisplayWidth;
    uint32_t        m_DisplayHeight;
    const char*	    m_AppTitle;
    bool            m_Fullscreen;
};

typedef uint32_t HGFXVertexProgram;
typedef uint32_t HGFXFragmentProgram;

GFXHContext GFXGetContext();

GFXHDevice GFXCreateDevice(int* argc, char** argv, GFXSCreateDeviceParams *params);

// clear/draw functions
void GFXFlip();
void GFXClear(GFXHContext context, uint32_t flags, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha, float depth, uint32_t stencil);
void GFXDrawTriangle2D(GFXHContext context, const float* vertices, const float* colours);
void GFXDrawTriangle3D(GFXHContext context, const float* vertices, const float* colours);
void GFXSetVertexStream(GFXHContext context, uint16_t stream, uint16_t size, GFXType type, uint16_t stride, const void* vertex_buffer);
void GFXDisableVertexStream(GFXHContext context, uint16_t stream);
void GFXDrawElements(GFXHContext context, GFXPrimitiveType prim_type, uint32_t count, GFXType type, const void* index_buffer);

HGFXVertexProgram GFXCreateVertexProgram(const void* program, uint32_t program_size);
HGFXFragmentProgram GFXCreateFragmentProgram(const void* program, uint32_t program_size);
void GFXSetVertexProgram(GFXHContext context, HGFXVertexProgram program);
void GFXSetFragmentProgram(GFXHContext context, HGFXFragmentProgram program);

void GFXSetViewport(GFXHContext context, int width, int height, float field_of_view, float z_near, float z_far);

void GFXEnableState(GFXHContext context, GFXRenderState state);
void GFXDisableState(GFXHContext context, GFXRenderState state);
void GFXSetMatrix(GFXHContext context, GFXMatrixMode matrix_mode, const Vectormath::Aos::Matrix4* matrix);

GFXHTexture GFXCreateTexture(const char* file);
void GFXSetTexture(GFXHTexture t);
void GFXDestroyTexture(GFXHTexture t);



#endif	// __GRAPHICSDEVICE_H__


