/*
 * Copyright 2014 Dario Manesku. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef CMFT_CUBEMAPFILTER_H_HEADER_GUARD
#define CMFT_CUBEMAPFILTER_H_HEADER_GUARD

#include "image.h"
#include <stdint.h> //uint32_t

namespace cmft
{
    #define SH_COEFF_NUM 25

    /// Computes spherical harominics coefficients for given cubemap data.
    /// Input data should be in RGBA32F format.
    void cubemapShCoeffs(double _shCoeffs[SH_COEFF_NUM][3], void* _data, uint32_t _faceSize, uint32_t _faceOffsets[6]);

    /// Computes spherical harominics coefficients for given cubemap image.
    bool imageShCoeffs(double _shCoeffs[SH_COEFF_NUM][3], const Image& _image, AllocatorI* _allocator = g_allocator);

    /// Creates irradiance cubemap. Uses fast spherical harmonics implementation.
    bool imageIrradianceFilterSh(Image& _dst, uint32_t _dstFaceSize, const Image& _src, AllocatorI* _allocator = g_allocator);

    /// Converts cubemap image into irradiance cubemap. Uses fast spherical harmonics implementation.
    void imageIrradianceFilterSh(Image& _image, uint32_t _faceSize, AllocatorI* _allocator = g_allocator);

    struct LightingModel
    {
        enum Enum
        {
            Phong,
            PhongBrdf,
            Blinn,
            BlinnBrdf,

            Count
        };
    };

    /// Warp edge fixup is used for DirectX 9 and OpenGL without ARB_seamless_cube_map where
    /// there is no support for seamless filtering across cubemap faces. For cubemaps filtered
    /// with Warp filter, this code needs to be called in the shader at runtime:
    ///     vec3 fixCubeLookup(vec3 _v, float _lod, float _topLevelCubeSize)
    ///     {
    ///         float ax = abs(_v.x);
    ///         float ay = abs(_v.y);
    ///         float az = abs(_v.z);
    ///         float vmax = max(max(ax, ay), az);
    ///         float scale = 1.0 - exp2(_lod)/_topLevelCubeSize;
    ///         if (ax != vmax) { _v.x *= scale; }
    ///         if (ay != vmax) { _v.y *= scale; }
    ///         if (az != vmax) { _v.z *= scale; }
    ///         return _v;
    ///     }
    ///
    /// For aditional details see: http://the-witness.net/news/2012/02/seamless-cube-map-filtering/
    ///
    struct EdgeFixup
    {
        enum Enum
        {
            None,
            Warp,
        };
    };

    /// Helper functions.
    float specularPowerFor(float _mip, float _mipCount, float _glossScale, float _glossBias);
    float applyLightningModel(float _specularPower, LightingModel::Enum _lightingModel);

    struct ClContext;

    /// Creates radiance cubemap image.
    bool imageRadianceFilter(Image& _dst
                           , uint32_t _dstFaceSize
                           , LightingModel::Enum _lightingModel
                           , bool _excludeBase
                           , uint8_t _mipCount
                           , uint8_t _glossScale
                           , uint8_t _glossBias
                           , const Image& _src
                           , EdgeFixup::Enum _edgeFixup = EdgeFixup::None
                           , uint8_t _numCpuProcessingThreads = 0
                           , ClContext* _clContext = NULL
                           , AllocatorI* _allocator = g_allocator
                           );

    /// Converts cubemap image into radiance cubemap.
    bool imageRadianceFilter(Image& _image
                           , uint32_t _dstFaceSize
                           , LightingModel::Enum _lightingModel
                           , bool _excludeBase
                           , uint8_t _mipCount
                           , uint8_t _glossScale
                           , uint8_t _glossBias
                           , EdgeFixup::Enum _edgeFixup = EdgeFixup::None
                           , uint8_t _numCpuProcessingThreads = 0
                           , ClContext* _clContext = NULL
                           , AllocatorI* _allocator = g_allocator
                           );

} // namespace cmft

#endif // CMFT_CUBEMAPFILTER_H_HEADER_GUARD

/* vim: set sw=4 ts=4 expandtab: */
