#ifndef ps2gl_fixed_function_h
#define ps2gl_fixed_function_h

#pragma once

typedef enum {
    QW_NONE  = 0x0,  // ----
    QW_X     = 0x1,  // X---
    QW_XY    = 0x3,  // XY--
    QW_XYZ   = 0x7,  // XYZ-
    QW_XYZW  = 0xF   // XYZW
} QuadWords;

typedef struct {
    QuadWords vertices;   // legal: QW_XYZ or QW_XYZW // TODO: remove (qw == QW_XYZ) I THINK!!
    QuadWords normals;    // legal: QW_NONE or QW_XYZ
    QuadWords texcoords;  // legal: QW_NONE or QW_XY
    QuadWords colors;     // legal: QW_NONE or QW_XYZW //TODO: add  QW_XYZ?????
} LaneConfig;

static inline int verticesOk(QuadWords  qw) { return (qw == QW_XYZ)  || (qw == QW_XYZW); } // TODO: remove (qw == QW_XYZ)
static inline int normalsOk(QuadWords   qw) { return (qw == QW_NONE) || (qw == QW_XYZ);  }
static inline int texcoordsOk(QuadWords qw) { return (qw == QW_NONE) || (qw == QW_XY);   }
static inline int colorsOk(QuadWords    qw) { return (qw == QW_NONE) || (qw == QW_XYZW); }

static inline int ValidateLaneConfig(const LaneConfig* lanes, const char* where) {
    if (!verticesOk(lanes->vertices) || !normalsOk(lanes->normals) || !texcoordsOk(lanes->texcoords) || !colorsOk(lanes->colors)) {
        mError("%s: illegal lane masks (V=%x N=%x T=%x C=%x)", where, lanes->vertices, lanes->normals, lanes->texcoords, lanes->colors);
        return 0;
    }
    return 1;
}

static inline int QWToWords(QuadWords qw) {
    switch (qw) {
    case QW_NONE: return 0;
    case QW_X:    return 1;  //TODO: just for brevity
    case QW_XY:   return 2;
    case QW_XYZ:  return 3;
    case QW_XYZW: return 4;
    default:      return 0;
    }
}

static inline int LanePresent(QuadWords qw) { return (qw != QW_NONE); }

#endif // ps2gl_fixed_function_h
