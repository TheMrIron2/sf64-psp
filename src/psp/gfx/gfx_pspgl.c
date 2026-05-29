#include "src/psp/gfx/gfx_pspgl.h"

#include "src/psp/gfx/gfx_psp.h"

#include <GLES/gl.h>

typedef struct {
    GLfloat x;
    GLfloat y;
    GLfloat z;
} PspGfxVertex;

typedef struct {
    GLfloat r;
    GLfloat g;
    GLfloat b;
    GLfloat a;
} PspGfxColor;

static const PspGfxVertex sTriangleVertices[3] = {
    { 0.0f, 0.58f, 0.0f },
    { -0.68f, -0.52f, 0.0f },
    { 0.68f, -0.52f, 0.0f },
};

static const PspGfxColor sTriangleColors[3] = {
    { 1.0f, 0.95f, 0.25f, 1.0f },
    { 0.05f, 0.85f, 1.0f, 1.0f },
    { 1.0f, 0.18f, 0.45f, 1.0f },
};

void PspGfxPspgl_Init(void) {
    glViewport(0, 0, PspGfx_GetWidth(), PspGfx_GetHeight());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
}

void PspGfxPspgl_BeginFrame(void) {
    glViewport(0, 0, PspGfx_GetWidth(), PspGfx_GetHeight());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.12f, 0.02f, 0.45f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PspGfxPspgl_DrawTestTriangle(void) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_TEXTURE_2D);

    glVertexPointer(3, GL_FLOAT, 0, sTriangleVertices);
    glColorPointer(4, GL_FLOAT, 0, sTriangleColors);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFlush();
}
