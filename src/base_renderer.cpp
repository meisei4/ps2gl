/*	  Copyright (C) 2000,2001,2002  Sony Computer Entertainment America

       	  This file is subject to the terms and conditions of the GNU Lesser
	  General Public License Version 2.1. See the file "COPYING" in the
	  main directory of this archive for more details.                             */

#include "ps2s/cpu_matrix.h"
#include "ps2s/math.h"
#include "ps2s/packet.h"

#include <stdlib.h>
#include <string.h>

#include "ps2gl/base_renderer.h"
#include "ps2gl/drawcontext.h"
#include "ps2gl/glcontext.h"
#include "ps2gl/immgmanager.h"
#include "ps2gl/lighting.h"
#include "ps2gl/material.h"
#include "ps2gl/matrix.h"
#include "ps2gl/metrics.h"
#include "ps2gl/texture.h"

#include "vu1_context.h"

void CBaseRenderer::GetUnpackAttribs(int numWords, unsigned int& mode, Vifs::tMask& mask)
{

    if (numWords == 3) {
        Vifs::tMask vec3Mask = { 0, 0, 0, 1,
            0, 0, 0, 1,
            0, 0, 0, 1,
            0, 0, 0, 1 };
        mode = Vifs::UnpackModes::v3_32;
        mask = vec3Mask;
    } else if (numWords == 4) {
        Vifs::tMask vec4Mask = { 0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0 };
        mode = Vifs::UnpackModes::v4_32;
        mask = vec4Mask;
    } else if (numWords == 2) {
        Vifs::tMask vec2Mask = { 0, 0, 1, 1,
            0, 0, 1, 1,
            0, 0, 1, 1,
            0, 0, 1, 1 };
        mode = Vifs::UnpackModes::v2_32;
        mask = vec2Mask;
    } else {
        mError("shouldn't get here (you're probably calling glDrawArrays"
               "without setting one of the pointers)");
    }
}

/**
 * Caches some data frequently used by XferBlock(), sets up row register.
 * The parameters wordsPerNormal, wordsPerTex, and wordsPerColor should be
 * zero if the application has not given normals, texture coords, or colors.
 */
void CBaseRenderer::InitXferBlock(CVifSCDmaPacket& packet,
    int wordsPerVertex, int wordsPerNormal,
    int wordsPerTex, int wordsPerColor)
{
    CImmGeomManager& gmanager = pGLContext->GetImmGeomManager();

    NormalBuf   = &gmanager.GetNormalBuf();
    TexCoordBuf = &gmanager.GetTexCoordBuf();
    ColorBuf = &gmanager.GetColorBuf();

    CurNormal             = gmanager.GetCurNormal();
    const float* texCoord = gmanager.GetCurTexCoord();
    CurTexCoord[0]        = texCoord[0];
    CurTexCoord[1]        = texCoord[1];

    CurGeomColor = gmanager.GetCurGeomColor();
    // get unpack modes/masks

    WordsPerVertex = wordsPerVertex;
    GetUnpackAttribs(WordsPerVertex, VertexUnpackMode, VertexUnpackMask);

    WordsPerNormal = (wordsPerNormal > 0) ? wordsPerNormal : 3;
    GetUnpackAttribs(WordsPerNormal, NormalUnpackMode, NormalUnpackMask);

    WordsPerTexCoord = (wordsPerTex > 0) ? wordsPerTex : 2;
    GetUnpackAttribs(WordsPerTexCoord, TexCoordUnpackMode, TexCoordUnpackMask);

    WordsPerColor = (wordsPerColor > 0) ? wordsPerColor : 3;
    GetUnpackAttribs(WordsPerColor, ColorUnpackMode, ColorUnpackMask);

    //TODO: this realy needs to be corrected and centralized somewhere clearer
    XferVertices  = (wordsPerVertex  > 0);
    XferNormals   = (wordsPerNormal  > 0) && pGLContext->GetImmLighting().GetLightingEnabled();
    XferTexCoords = (wordsPerTex     > 0);
    XferColors    = (WordsPerColor   > 0)
                 && (!pGLContext->GetImmLighting().GetLightingEnabled()
                     || pGLContext->GetMaterialManager().GetColorMaterialEnabled());
    {
        auto dump_mask_4x4 = [&](const char* label, const Vifs::tMask& maskRef) {
            const int* m = reinterpret_cast<const int*>(&maskRef);
            mDebugPrint("[CBaseRenderer::InitXferBlock]] %s_mask row0=[%d %d %d %d]\n", label, m[0], m[1], m[2], m[3]);
            mDebugPrint("[CBaseRenderer::InitXferBlock]] %s_mask row1=[%d %d %d %d]\n", label, m[4], m[5], m[6], m[7]);
            mDebugPrint("[CBaseRenderer::InitXferBlock]] %s_mask row2=[%d %d %d %d]\n", label, m[8], m[9], m[10], m[11]);
            mDebugPrint("[CBaseRenderer::InitXferBlock]] %s_mask row3=[%d %d %d %d]\n", label, m[12], m[13], m[14], m[15]);
        };
        mDebugPrint("[CBaseRenderer::InitXferBlock]] words_per_attribute: vertex=%d normal=%d texture_coordinates=%d color=%d\n",
                    WordsPerVertex, WordsPerNormal, WordsPerTexCoord, WordsPerColor);
        mDebugPrint("[CBaseRenderer::InitXferBlock]] unpack_modes_hex?: vertex=0x%08x normal=0x%08x texture_coordinates=0x%08x color=0x%08x\n",
                    VertexUnpackMode, NormalUnpackMode, TexCoordUnpackMode, ColorUnpackMode);
        mDebugPrint("[CBaseRenderer::InitXferBlock]] attributes_enabled: vertices=%d normals=%d texture_coordinates=%d colors=%d  (InputQuadsPerVert=%d)\n",
                    (int)XferVertices, (int)XferNormals, (int)XferTexCoords, (int)XferColors, InputQuadsPerVert);
        dump_mask_4x4("vertex",        VertexUnpackMask);
        dump_mask_4x4("normal",        NormalUnpackMask);
        dump_mask_4x4("texcoord",      TexCoordUnpackMask);
        dump_mask_4x4("color",         ColorUnpackMask);
    }
    // set up the row register to expand vectors with fewer than 4 elements

    packet.Cnt();
    {
        // w is 256 to remind me that this is not used as the vertex w but
        // is necessary to clear any adc bits set for strips, otherwise they
        // accumulate..
        static const float row[4] = { 0.0f, 0.0f, 1.0f, 256.0f };
        packet.Strow(row);
        mDebugPrint("[CBaseRenderer::InitXferBlock]] STROW=[%g %g %g %g]\n", row[0],row[1],row[2],row[3]);
        mDebugPrint("[CBaseRenderer::InitXferBlock]] STROW row_register=[%f %f %f %f]\n", row[0], row[1], row[2], row[3]);

        packet.Pad128();
    }
    packet.CloseTag();
}

static const char* expand_desc(int wordsPerVec) {
    switch (wordsPerVec) {
    case 4: return "(x,y,z,w) <- data,data,data,data";
    case 3: return "(x,y,z,w) <- data,data,data,ROW.w=256";
    case 2: return "(s,t,q,w) <- data,data,ROW.z=1,ROW.w=256";
    default:return "(?)";
    }
}

/**
 * Transfers a block of geometry to vu0/vu1 using <i>packet</i>, where
 * "geometry" means vertices and zero or more normals,
 * texture coordinates, and colors.
 * <b>Note that you MUST set the vif1 write mode correctly before calling
 * XferBlock!!</b> (e.g., Stcycl(1, vu1QuadsPerVert))
 * normals, texCoords, and colors should be NULL if not provided.
 * @param vu1Offset offset into vu1 memory in quadwords
 * @param firstElement the starting "offset" into the vertex, normal, etc.
 *  arrays (for example: this would be "2" to start draw from the 3rd element)
 */
void CBaseRenderer::XferBlock(CVifSCDmaPacket& packet,
    const void* vertices, const void* normals,
    const void* texCoords, const void* colors,
    int vu1Offset, int firstElement, int numToAdd)
{
    // packet.Cnt();
    // {
    //     packet.Stcycl(1, InputQuadsPerVert);
    //     packet.Pad128();
    // }
    // packet.CloseTag();
    mDebugPrint("[CBaseRenderer::XferBlock] STCYCLE set to (CL=1, WL=%d) for geometry\n", InputQuadsPerVert);
    auto dump_vec = [&](const char* label, const float* p, int strideW) {
        mDebugPrint("[CBaseRenderer::XferBlock] %s[0]=[%f %f %f %f]\n", label, p[0], p[1],
                    (strideW>2? p[2]:0.0f), (strideW>3? p[3]:0.0f));
        mDebugPrint("[CBaseRenderer::XferBlock] %s[1]=[%f %f %f %f]\n", label, p[strideW+0], p[strideW+1],
                    (strideW>2? p[strideW+2]:0.0f), (strideW>3? p[strideW+3]:0.0f));
    };
    //TODO: lane mapping V|T|C, V|C|T, V|N|T|C is difficult to figure out with the vu code
    const int laneV = 0;
    const int laneN = 1;
    const int laneT = 2;
    const int laneC = 3;
    mDebugPrint("[CBaseRenderer::XferBlock] lanes: vertexLane=%d normalLane=%d texcoordLane=%d colorLane=%d (vu1OffsetBase=%d)\n",
                laneV, laneN, laneT, laneC, vu1Offset);
    int pushedV = 0, pushedN = 0, pushedT = 0, pushedC = 0;
    //
    // vertices
    //

    if (XferVertices) {
        dump_vec("Vertices", (const float*)vertices + firstElement*WordsPerVertex, WordsPerVertex);
        mErrorIf(vertices == NULL, "Tried to render an array with no vertices!");
        XferVectors(packet, (unsigned int*)vertices,
            firstElement, numToAdd,
            WordsPerVertex, VertexUnpackMask, VertexUnpackMode,
            vu1Offset + laneV);
        pushedV = numToAdd;
    }

    //
    // normals
    //

    int firstNormal = firstElement;
    if (XferNormals && normals == NULL) {
        // no normals given, so use the current normal..
        // I hate to actually write every normal into the packet,
        // but I can't use the vif to expand the data because I
        // need it to interleave the vertices, normals, etc..
        CDmaPacket& normalBuf = *NormalBuf;
        normals               = (void*)normalBuf.GetNextPtr();
        firstNormal           = 0;

        for (int i = 0; i < numToAdd; i++)
            normalBuf += CurNormal;
        mDebugPrint("[CBaseRenderer::XferBlock] normals were defaulted from CurNormal for %d vertices\n", numToAdd);
    }

    if (XferNormals) {
        dump_vec("Normals", (const float*)normals + firstNormal*WordsPerNormal, WordsPerNormal);
        XferVectors(packet, (unsigned int*)normals,
            firstNormal, numToAdd,
            WordsPerNormal, NormalUnpackMask, NormalUnpackMode,
            vu1Offset + laneN);
        pushedN = numToAdd;
    }

    //
    // tex coords
    //

    int firstTexCoord = firstElement;
    if (XferTexCoords && texCoords == NULL) {
        // no tex coords given, so use the current value..
        // see note above for normals
        CDmaPacket& texCoordBuf = *TexCoordBuf;
        texCoords               = (void*)texCoordBuf.GetNextPtr();
        firstTexCoord           = 0;

        for (int i = 0; i < numToAdd; i++) {
            texCoordBuf += CurTexCoord[0];
            texCoordBuf += CurTexCoord[1];
        }
        mDebugPrint("[CBaseRenderer::XferBlock] texture coordinates were defualted from CurTexCoord for %d vertices\n", numToAdd);
    }
    if (XferTexCoords) {
        dump_vec("TexCoords", (const float*)texCoords + firstTexCoord*WordsPerTexCoord, WordsPerTexCoord);
        XferVectors(packet, (unsigned int*)texCoords,
            firstTexCoord, numToAdd,
            WordsPerTexCoord, TexCoordUnpackMask, TexCoordUnpackMode,
            vu1Offset + laneT);
        pushedT = numToAdd;
    }

    //
    // colors
    //

    int firstColor = firstElement;
    // TODO: i know its bad for EE maybe, but i feel like this should also somehow default to white or the pushed registered color
    if (XferColors && colors == NULL) {
        CDmaPacket& colorBuf = *ColorBuf;
        colors = (void*)colorBuf.GetNextPtr();
        firstColor = 0;
        for (int i = 0; i < numToAdd; ++i) {
            colorBuf += CurGeomColor[0];
            colorBuf += CurGeomColor[1];
            colorBuf += CurGeomColor[2];
        }
        mDebugPrint("[CBaseRenderer::XferBlock] colors were defaulted from CurGeomColor for %d vertices (WordsPerColor=%d)\n",
                    numToAdd, WordsPerColor);
    }
    //TODO this wont send when material is off
    if (colors != NULL && XferColors) {
        mErrorIf(colors == NULL, "XferColors=true but no color data present");
        dump_vec("Colors", (const float*)colors + firstColor*WordsPerColor, WordsPerColor);
        XferVectors(packet, (unsigned int*)colors,
            firstColor, numToAdd,
            WordsPerColor, ColorUnpackMask, ColorUnpackMode,
            vu1Offset + laneC);
        pushedC = numToAdd;
    }
    constexpr float INF = std::numeric_limits<float>::infinity();
    auto minmax = [](const float* p, int n, int stride){
        float mn[4]={+INF,+INF,+INF,+INF}, mx[4]={-INF,-INF,-INF,-INF};
        for(int i=0;i<n;i++){ for(int c=0;c<stride;c++){ mn[c]=std::min(mn[c],p[i*stride+c]);
            mx[c]=std::max(mx[c],p[i*stride+c]); } }
        mDebugPrint("[CBaseRenderer::XferBlock] COLOR in-range: R[%g..%g] G[%g..%g] B[%g..%g] A[%g..%g]\n",
                    mn[0],mx[0], mn[1],mx[1], mn[2],mx[2], (stride>3?mn[3]:0),(stride>3?mx[3]:0));
    };
    if (colors) minmax((float*)colors + firstElement*WordsPerColor, numToAdd, WordsPerColor);

    mDebugPrint("[CBaseRenderer::XferBlock] block summary: pushed vertices/normals/texcoords/colors = %d/%d/%d/%d  firstElement=%d count=%d vu1OffsetBase=%d\n",
            pushedV, pushedN, pushedT, pushedC, firstElement, numToAdd, vu1Offset);
    mDebugPrint("[CBaseRenderer::XferBlock] layout: WL=%d CL=1 vu1OffsetBase=%d lanes{V=%d T=%d C=%d}\n",
            InputQuadsPerVert, vu1Offset, laneV, laneT, laneC);
    mDebugPrint("[CBaseRenderer::XferBlock] lane_bases: VU1[%d]=V  VU1[%d]=T  VU1[%d]=C\n",
                vu1Offset+laneV, vu1Offset+laneT, vu1Offset+laneC);
    mDebugPrint("[CBaseRenderer::XferBlock] expand vertex   %s\n", expand_desc(WordsPerVertex));
    mDebugPrint("[CBaseRenderer::XferBlock] expand texcoord %s\n", expand_desc(WordsPerTexCoord));
    mDebugPrint("[CBaseRenderer::XferBlock] expand color    %s\n", expand_desc(WordsPerColor));
}

#define kContextStart 0 // for the kLightBase stuff below

void CBaseRenderer::AddVu1RendererContext(CVifSCDmaPacket& packet, GLenum primType, int vu1Offset)
{
    CGLContext& glContext = *pGLContext;

    packet.Stcycl(1, 1);
    packet.Flush();
    packet.Pad96();
    mDebugPrint("[CBaseRenderer::AddVu1RendererContext] OpenUnpack begin: mode=v4_32 vu1MemOffset=%d buffering=kSingleBuff\n", vu1Offset);
    packet.OpenUnpack(Vifs::UnpackModes::v4_32, vu1Offset, Packet::kSingleBuff);
    {
        // find light pointers
        CImmLighting& lighting = glContext.GetImmLighting();
        tLightPtrs lightPtrs[8];
        tLightPtrs *nextDir, *nextPt, *nextSpot;
        nextDir = nextPt = nextSpot = &lightPtrs[0];
        int numDirs, numPts, numSpots;
        numDirs = numPts = numSpots = 0;
        for (int i = 0; i < 8; i++) {
            CImmLight& light = lighting.GetImmLight(i);
            if (light.IsEnabled()) {
                int lightBase = kLight0Base + vu1Offset;
                if (light.IsDirectional()) {
                    nextDir->dir = lightBase + i * kLightStructSize;
                    nextDir++;
                    numDirs++;
                } else if (light.IsPoint()) {
                    nextPt->point = lightBase + i * kLightStructSize;
                    nextPt++;
                    numPts++;
                } else if (light.IsSpot()) {
                    nextSpot->spot = lightBase + i * kLightStructSize;
                    nextSpot++;
                    numSpots++;
                }
            }
        }

        bool doLighting = glContext.GetImmLighting().GetLightingEnabled();
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] lighting: enabled=%d  dir_lights=%d point_lights=%d spot_lights=%d\n",
                    (int)doLighting, numDirs, numPts, numSpots);

        // transpose of object to world space xfrm (for light directions)
        cpu_mat_44 objToWorldXfrmTrans = glContext.GetModelViewStack().GetTop();
        // clear any translations.. should be doing a 3x3 transpose..
        objToWorldXfrmTrans.set_col3(cpu_vec_xyzw(0, 0, 0, 1));
        objToWorldXfrmTrans = objToWorldXfrmTrans.transpose();
        // do we need to rescale normals?
        cpu_mat_44 normalRescale;
        normalRescale.set_identity();
        float normalScale            = 1.0f;
        CImmDrawContext& drawContext = glContext.GetImmDrawContext();
        if (drawContext.GetRescaleNormals()) {
            cpu_vec_xyzw fake_normal(1, 0, 0, 0);
            fake_normal = objToWorldXfrmTrans * fake_normal;
            normalScale = 1.0f / fake_normal.length();
            normalRescale.set_scale(cpu_vec_xyz(normalScale, normalScale, normalScale));
        }
        objToWorldXfrmTrans = normalRescale * objToWorldXfrmTrans;

        // num lights
        if (doLighting) {
            packet += numDirs;
            packet += numPts;
            packet += numSpots;
        } else {
            packet += (uint64_t)0;
            packet += 0;
        }
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] light_counts written: directional=%d point=%d spot=%d\n", numDirs, numPts, numSpots);

        // backface culling multiplier -- this is 1.0f or -1.0f, the 6th bit
        // also turns on/off culling
        float bfc_mult = (float)drawContext.GetCullFaceDir();
        unsigned int bfc_word;
        asm(" ## nop ## "
            : "=r"(bfc_word)
            : "0"(bfc_mult));
        bool do_culling = drawContext.GetDoCullFace() && (primType > GL_LINE_STRIP);
        packet += bfc_word | (unsigned int)do_culling << 5;
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] culling: multiplier_float=%f packed_word=0x%08x do_culling=%d\n",
                    bfc_mult, bfc_word, (int)do_culling);

        // light pointers
        packet.Add(&lightPtrs[0], 8);
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] light pointer table (8 entries) written\n");

        float maxColorValue = GetMaxColorValue(glContext.GetTexManager().GetTexEnabled());
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] maxColorValue (format scaling)=%f  texture_enabled=%d\n",
                    maxColorValue, (int)glContext.GetTexManager().GetTexEnabled());

        // add light info
        for (int i = 0; i < 8; i++) {
            CImmLight& light = lighting.GetImmLight(i);
            packet += light.GetAmbient() * maxColorValue;
            packet += light.GetDiffuse() * maxColorValue;
            packet += light.GetSpecular() * maxColorValue;

            if (light.IsDirectional())
                packet += light.GetPosition();
            else {
                packet += light.GetPosition();
            }

            packet += light.GetSpotDir();

            // attenuation coeffs for positional light sources
            // because we're doing lighting calculations in object space,
            // we need to adjust the attenuation of positional light sources
            // and all lighting directions to take into account scaling
            packet += light.GetConstantAtten();
            packet += light.GetLinearAtten() * 1.0f / normalScale;
            packet += light.GetQuadAtten() * 1.0f / normalScale;
            packet += 0; // padding
        }
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] per-light blocks (8) written (ambient/diffuse/specular, position/dir, attenuation)\n");

        // global ambient
        cpu_vec_4 globalAmb;
        if (doLighting)
            globalAmb = lighting.GetGlobalAmbient() * maxColorValue;
        else
            globalAmb = cpu_vec_4(0, 0, 0, 0);
        packet.Add((uint32_t*)&globalAmb, 3);
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] global_ambient written (scaled_if_lit=%d)\n", (int)doLighting);

        // stick in the offset to convert clip space depth value to GS
        float depthClipToGs = (float)((1 << drawContext.GetDepthBits()) - 1) / 2.0f;
        packet += depthClipToGs;
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] depthClipToGs=%f (depth_bits=%d)\n", depthClipToGs, (int)drawContext.GetDepthBits());

        // cur material

        CImmMaterial& material = glContext.GetMaterialManager().GetImmMaterial();

        // add emissive component
        cpu_vec_4 emission;
        if (doLighting) {
            emission = material.GetEmission() * maxColorValue;
        } else {
            emission = glContext.GetGeomManager().GetCurGeomColor() * maxColorValue;
            // emission = glContext.GetMaterialManager().GetCurMatColor() * maxColorValue;
        }
        packet += emission;

        // ambient
        packet += material.GetAmbient();

        // diffuse
        cpu_vec_4 matDiffuse = material.GetDiffuse();
        // the alpha value is set to the alpha of the diffuse in the renderers;
        // this should be the current color alpha if lighting is disabled
        if (!doLighting)
            matDiffuse[3] = glContext.GetGeomManager().GetCurGeomColor()[3];
            // matDiffuse[3] = glContext.GetMaterialManager().GetCurMatColor()[3];
        packet += matDiffuse;

        // specular
        packet += material.GetSpecular();
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] material written: emissive/ambient/diffuse/specular (diffuse.a may be overridden when unlit)\n");

        // vertex xform
        packet += drawContext.GetVertexXform();

        // fixed vertToEye vector for non-local specular
        cpu_vec_xyzw vertToEye(0.0f, 0.0f, 1.0f, 0.0f);
        packet += objToWorldXfrmTrans * vertToEye;

        // transpose of object to world space transform
        packet += objToWorldXfrmTrans;

        // world to object space xfrm (for light positions)
        cpu_mat_44 worldToObjXfrm = glContext.GetModelViewStack().GetInvTop();
        packet += worldToObjXfrm;
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] transforms written: vertexXform, vertToEye(object), objToWorld^T, worldToObj\n");

        // giftag - this is down at the bottom to make sure that when switching
        // primitives the last buffer will have a chance to copy the giftag before
        // it is overwritten with the new one
        GLenum newPrimType = drawContext.GetPolygonMode();
        if (newPrimType == GL_FILL)
            newPrimType = primType;
        newPrimType &= 0xff;
        tGifTag giftag = BuildGiftag(newPrimType);
        packet += giftag;

        {
            const uint64_t* giftag_u64 = reinterpret_cast<const uint64_t*>(&giftag);
            mDebugPrint("[CBaseRenderer::AddVu1RendererContext] giftag written: primType=%u  u64[0]=0x%016llx u64[1]=0x%016llx\n",
                        (unsigned int)newPrimType,
                        (unsigned long long)giftag_u64[0],
                        (unsigned long long)giftag_u64[1]);
        }

        // add info used by clipping code
        // first the dimensions of the framebuffer
        float xClip = (float)2048.0f / (drawContext.GetFBWidth() * 0.5f * 2.0f);
        packet += Math::Max(xClip, 1.0f);
        float yClip = (float)2048.0f / (drawContext.GetFBHeight() * 0.5f * 2.0f);
        packet += Math::Max(yClip, 1.0f);
        float depthClip = 2048.0f / depthClipToGs;
        // FIXME: maybe these 2048's should be 2047.5s...
        depthClip *= 1.003f; // round up a bit for fp error (????)
        packet += depthClip;
        // enable/disable clipping
        packet += (drawContext.GetDoClipping()) ? 1 : 0;
        mDebugPrint("[CBaseRenderer::AddVu1RendererContext] clipping params written: xClip>=1, yClip>=1, depthClip=%.6f, clippingEnabled=%d\n",
                    depthClip, (int)drawContext.GetDoClipping());
    }
    packet.CloseUnpack();
    mDebugPrint("[CBaseRenderer::AddVu1RendererContext] OpenUnpack end (CloseUnpack) at vu1MemOffset=%d\n", vu1Offset);
}

tGifTag
CBaseRenderer::BuildGiftag(GLenum primType)
{
    CGLContext& glContext = *pGLContext;

    primType &= 0x7; // convert from GL #define to gs prim number
    CImmDrawContext& drawContext = glContext.GetImmDrawContext();
    bool smoothShading           = drawContext.GetDoSmoothShading();
    bool useTexture              = glContext.GetTexManager().GetTexEnabled();
    bool alpha                   = drawContext.GetBlendEnabled();
    unsigned int nreg            = OutputQuadsPerVert;

    GS::tPrim prim = { prim_type : primType, iip : smoothShading, tme : useTexture, fge : 0, abe : alpha, aa1 : 0, fst : 0, ctxt : 0, fix : 0 };
    tGifTag giftag = { NLOOP : 0, EOP : 1, pad0 : 0, id : 0, PRE : 1, PRIM : *(uint64_t*)&prim, FLG : 0, NREG : nreg, REGS0 : 2, REGS1 : 1, REGS2 : 4 };
    mDebugPrint("[CBaseRenderer::BuildGiftag] giftag regs order: REGS0=%u REGS1=%u REGS2=%u  (I WANT 2=ST, 1=RGBAQ, 4=XYZF2...... I THINK!)\n",
                giftag.REGS0, giftag.REGS1, giftag.REGS2);
    return giftag;
}

void CBaseRenderer::CacheRendererState()
{
    XferNormals   = pGLContext->GetImmLighting().GetLightingEnabled();
    XferTexCoords = pGLContext->GetTexManager().GetTexEnabled();
    //TODO: something tells me this needs to be restructured i think, it feels too hidden for where its important...
    XferColors    = false;
    // XferColors    = pGLContext->GetMaterialManager().GetColorMaterialEnabled();
}

void CBaseRenderer::Load()
{
    unsigned int size64     = MicrocodePacketSize / 8;
    CVifSCDmaPacket& packet = pGLContext->GetVif1Packet();
    const u64* code         = (const u64*)MicrocodePacket;
    unsigned int addr64     = 0;

    mErrorIf((unsigned int)code & 0xf, "code not & 0xf");
    mErrorIf(MicrocodePacketSize & 0xf, "size not & 0xf");

    while (size64 > 0) {
        // Total send size
        unsigned int sendSize64 = (size64 > 256) ? 256 : size64;

        // Add send code command (VIF_CMD_MPG)
        packet.Ref(code, sendSize64 / 2);
        packet.Pad96();
        packet.Mpg(sendSize64 & 0xff, addr64);

        code += sendSize64;
        size64 -= sendSize64;
        addr64 += sendSize64;
    }
    packet.Cnt();
    packet.Mscal(0);
    packet.Pad128();
    packet.CloseTag();

    pglAddToMetric(kMetricsRendererUpload);
}

void CBaseRenderer::XferVectors(CVifSCDmaPacket& packet, unsigned int* dataStart,
    int startOffset, int numVectors, int wordsPerVec,
    Vifs::tMask unpackMask, uint32_t unpackMode,
    int vu1MemOffset)
{
    // find number of words to prepend with a cnt

    unsigned int* vecDataStart = dataStart + startOffset * wordsPerVec;
    unsigned int* vecDataEnd   = vecDataStart + numVectors * wordsPerVec;

    mAssert(numVectors > 0);
    mErrorIf((unsigned int)vecDataStart & (4 - 1),
        "XferVectors only works with word-aligned data");

    int numWordsToPrepend      = 0;
    unsigned int* refXferStart = vecDataStart;
    while ((unsigned int)refXferStart & (16 - 1)) {
        numWordsToPrepend++;
        refXferStart++;
        if (refXferStart == vecDataEnd)
            break;
    }
    int numWordsToAppend     = 0;
    unsigned int* refXferEnd = vecDataEnd;
    while (((unsigned int)refXferEnd & (16 - 1)) && refXferEnd > refXferStart) {
        numWordsToAppend++;
        refXferEnd--;
    }
    int numQuadsInRefXfer = ((unsigned int)refXferEnd - (unsigned int)refXferStart) / 16;
    mDebugPrint("[CBaseRenderer::XferVectors] begin: unpackMode=%#x vu1MemOff=%d wordsPerVec=%d numVectors=%d "
                "vecDataStart=%p vecDataEnd=%p startOffset=%d\n",
                unpackMode, vu1MemOffset, wordsPerVec, numVectors,
                (void*)vecDataStart, (void*)vecDataEnd, startOffset);
    mDebugPrint("[CBaseRenderer::XferVectors] alignment: prependWords=%d appendWords=%d refStart=%p refEnd=%p qwcRef=%d\n",
                numWordsToPrepend, numWordsToAppend, (void*)refXferStart, (void*)refXferEnd, numQuadsInRefXfer);

    packet.Cnt();
    {
        // set mask to expand vectors appropriately
        packet.Stmask(unpackMask);

        mDebugPrint("[CBaseRenderer::XferVectors] STMASK set; entering prepend path (prependWords=%d)\n",
                    numWordsToPrepend);
        // prepend
        if (numWordsToPrepend > 1) {
            // either 2 or 3 words to prepend
            packet.Nop().Nop();
            if (numWordsToPrepend == 2)
                packet.Nop();

            mDebugPrint("[CBaseRenderer::XferVectors] OpenUnpack (prepend-block): mode=%#x vu1MemOff=%d masked, vectors=%d\n",
                        unpackMode, vu1MemOffset, numVectors);
            packet.OpenUnpack(unpackMode,
                vu1MemOffset,
                VifDoubleBuffered,
                Packet::kMasked);
            packet.CloseUnpack(numVectors);

            if (numWordsToPrepend == 3)
                packet += *vecDataStart;
        }

        packet.Pad128();
    }
    packet.CloseTag();

    mDebugPrint("[CBaseRenderer::XferVectors] REF transfer: addr=%p qwc=%d (prependWords=%d)\n",
                (void*)refXferStart, numQuadsInRefXfer, numWordsToPrepend);
    // xfer qword block of vectors
    packet.Ref(Core::MakePtrNormal(refXferStart), numQuadsInRefXfer);
    {
        // either 0 words to prepend or 1 word left to prepend
        if (numWordsToPrepend == 0)
            packet.Nop();
        if (numWordsToPrepend <= 1) {
            mDebugPrint("[CBaseRenderer::XferVectors] OpenUnpack (main-block): mode=%#x vu1MemOff=%d masked, vectors=%d\n",
                        unpackMode, vu1MemOffset, numVectors);
            packet.OpenUnpack(unpackMode,
                vu1MemOffset,
                VifDoubleBuffered,
                Packet::kMasked);
            packet.CloseUnpack(numVectors);
        }
        if (numWordsToPrepend == 1)
            packet += *vecDataStart;
        else if (numWordsToPrepend == 2)
            packet.Add(vecDataStart, 2);
        else if (numWordsToPrepend == 3)
            packet.Add(&vecDataStart[1], 2);
    }

    // xfer any remaining vectors
    if (numWordsToAppend > 0) {
        mDebugPrint("[CBaseRenderer::XferVectors] trailing append: words=%d from=%p\n",
                    numWordsToAppend, (void*)refXferEnd);
        packet.Cnt();
        {
            packet.Add(refXferEnd, numWordsToAppend);
            packet.Pad128();
        }
        packet.CloseTag();
    }
    mDebugPrint("[CBaseRenderer::XferVectors] end\n");
}
