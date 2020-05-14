#pragma once

#include "engine/lumix.h"

namespace Lumix::gpu::DDS {

static const u32 DDS_MAGIC = 0x20534444; //  little-endian
static const u32 DDSD_CAPS = 0x00000001;
static const u32 DDSD_HEIGHT = 0x00000002;
static const u32 DDSD_WIDTH = 0x00000004;
static const u32 DDSD_PITCH = 0x00000008;
static const u32 DDSD_PIXELFORMAT = 0x00001000;
static const u32 DDSD_MIPMAPCOUNT = 0x00020000;
static const u32 DDSD_LINEARSIZE = 0x00080000;
static const u32 DDSD_DEPTH = 0x00800000;
static const u32 DDPF_ALPHAPIXELS = 0x00000001;
static const u32 DDPF_FOURCC = 0x00000004;
static const u32 DDPF_INDEXED = 0x00000020;
static const u32 DDPF_RGB = 0x00000040;
static const u32 DDSCAPS_COMPLEX = 0x00000008;
static const u32 DDSCAPS_TEXTURE = 0x00001000;
static const u32 DDSCAPS_MIPMAP = 0x00400000;
static const u32 DDSCAPS2_CUBEMAP = 0x00000200;
static const u32 DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400;
static const u32 DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800;
static const u32 DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000;
static const u32 DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000;
static const u32 DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000;
static const u32 DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000;
static const u32 DDSCAPS2_VOLUME = 0x00200000;
static const u32 D3DFMT_ATI1 = '1ITA';
static const u32 D3DFMT_ATI2 = '2ITA';
static const u32 D3DFMT_DXT1 = '1TXD';
static const u32 D3DFMT_DXT2 = '2TXD';
static const u32 D3DFMT_DXT3 = '3TXD';
static const u32 D3DFMT_DXT4 = '4TXD';
static const u32 D3DFMT_DXT5 = '5TXD';
static const u32 D3DFMT_DX10 = '01XD';

enum class DxgiFormat : u32 {
  UNKNOWN                     ,
  R32G32B32A32_TYPELESS       ,
  R32G32B32A32_FLOAT          ,
  R32G32B32A32_UINT           ,
  R32G32B32A32_SINT           ,
  R32G32B32_TYPELESS          ,
  R32G32B32_FLOAT             ,
  R32G32B32_UINT              ,
  R32G32B32_SINT              ,
  R16G16B16A16_TYPELESS       ,
  R16G16B16A16_FLOAT          ,
  R16G16B16A16_UNORM          ,
  R16G16B16A16_UINT           ,
  R16G16B16A16_SNORM          ,
  R16G16B16A16_SINT           ,
  R32G32_TYPELESS             ,
  R32G32_FLOAT                ,
  R32G32_UINT                 ,
  R32G32_SINT                 ,
  R32G8X24_TYPELESS           ,
  D32_FLOAT_S8X24_UINT        ,
  R32_FLOAT_X8X24_TYPELESS    ,
  X32_TYPELESS_G8X24_UINT     ,
  R10G10B10A2_TYPELESS        ,
  R10G10B10A2_UNORM           ,
  R10G10B10A2_UINT            ,
  R11G11B10_FLOAT             ,
  R8G8B8A8_TYPELESS           ,
  R8G8B8A8_UNORM              ,
  R8G8B8A8_UNORM_SRGB         ,
  R8G8B8A8_UINT               ,
  R8G8B8A8_SNORM              ,
  R8G8B8A8_SINT               ,
  R16G16_TYPELESS             ,
  R16G16_FLOAT                ,
  R16G16_UNORM                ,
  R16G16_UINT                 ,
  R16G16_SNORM                ,
  R16G16_SINT                 ,
  R32_TYPELESS                ,
  D32_FLOAT                   ,
  R32_FLOAT                   ,
  R32_UINT                    ,
  R32_SINT                    ,
  R24G8_TYPELESS              ,
  D24_UNORM_S8_UINT           ,
  R24_UNORM_X8_TYPELESS       ,
  X24_TYPELESS_G8_UINT        ,
  R8G8_TYPELESS               ,
  R8G8_UNORM                  ,
  R8G8_UINT                   ,
  R8G8_SNORM                  ,
  R8G8_SINT                   ,
  R16_TYPELESS                ,
  R16_FLOAT                   ,
  D16_UNORM                   ,
  R16_UNORM                   ,
  R16_UINT                    ,
  R16_SNORM                   ,
  R16_SINT                    ,
  R8_TYPELESS                 ,
  R8_UNORM                    ,
  R8_UINT                     ,
  R8_SNORM                    ,
  R8_SINT                     ,
  A8_UNORM                    ,
  R1_UNORM                    ,
  R9G9B9E5_SHAREDEXP          ,
  R8G8_B8G8_UNORM             ,
  G8R8_G8B8_UNORM             ,
  BC1_TYPELESS                ,
  BC1_UNORM                   ,
  BC1_UNORM_SRGB              ,
  BC2_TYPELESS                ,
  BC2_UNORM                   ,
  BC2_UNORM_SRGB              ,
  BC3_TYPELESS                ,
  BC3_UNORM                   ,
  BC3_UNORM_SRGB              ,
  BC4_TYPELESS                ,
  BC4_UNORM                   ,
  BC4_SNORM                   ,
  BC5_TYPELESS                ,
  BC5_UNORM                   ,
  BC5_SNORM                   ,
  B5G6R5_UNORM                ,
  B5G5R5A1_UNORM              ,
  B8G8R8A8_UNORM              ,
  B8G8R8X8_UNORM              ,
  R10G10B10_XR_BIAS_A2_UNORM  ,
  B8G8R8A8_TYPELESS           ,
  B8G8R8A8_UNORM_SRGB         ,
  B8G8R8X8_TYPELESS           ,
  B8G8R8X8_UNORM_SRGB         ,
  BC6H_TYPELESS               ,
  BC6H_UF16                   ,
  BC6H_SF16                   ,
  BC7_TYPELESS                ,
  BC7_UNORM                   ,
  BC7_UNORM_SRGB              ,
  AYUV                        ,
  Y410                        ,
  Y416                        ,
  NV12                        ,
  P010                        ,
  P016                        ,
  OPAQUE_420                  ,
  YUY2                        ,
  Y210                        ,
  Y216                        ,
  NV11                        ,
  AI44                        ,
  IA44                        ,
  P8                          ,
  A8P8                        ,
  B4G4R4A4_UNORM              ,
  P208                        ,
  V208                        ,
  V408                        ,
  FORCE_UINT
} ;

struct PixelFormat {
	u32 dwSize;
	u32 dwFlags;
	u32 dwFourCC;
	u32 dwRGBBitCount;
	u32 dwRBitMask;
	u32 dwGBitMask;
	u32 dwBBitMask;
	u32 dwAlphaBitMask;
};

struct Caps2 {
	u32 dwCaps1;
	u32 dwCaps2;
	u32 dwDDSX;
	u32 dwReserved;
};

struct Header {
	u32 dwMagic;
	u32 dwSize;
	u32 dwFlags;
	u32 dwHeight;
	u32 dwWidth;
	u32 dwPitchOrLinearSize;
	u32 dwDepth;
	u32 dwMipMapCount;
	u32 dwReserved1[11];

	PixelFormat pixelFormat;
	Caps2 caps2;

	u32 dwReserved2;
};

struct DXT10Header
{
	DxgiFormat dxgi_format;
	u32 resource_dimension;
	u32 misc_flag;
	u32 array_size;
	u32 misc_flags2;
};


static bool isDXT1(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT1));
}

static bool isDXT10(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DX10));
}

static bool isATI1(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_ATI1));
}

static bool isATI2(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_ATI2));
}

static bool isDXT3(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT3));
}

static bool isDXT5(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT5));
}

static bool isBGRA8(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& (pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 32)
		&& (pf.dwRBitMask == 0xff0000)
		&& (pf.dwGBitMask == 0xff00)
		&& (pf.dwBBitMask == 0xff)
		&& (pf.dwAlphaBitMask == 0xff000000U));
}

static bool isBGR8(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 24)
		&& (pf.dwRBitMask == 0xff0000)
		&& (pf.dwGBitMask == 0xff00)
		&& (pf.dwBBitMask == 0xff));
}

static bool isBGR5A1(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& (pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 16)
		&& (pf.dwRBitMask == 0x00007c00)
		&& (pf.dwGBitMask == 0x000003e0)
		&& (pf.dwBBitMask == 0x0000001f)
		&& (pf.dwAlphaBitMask == 0x00008000));
}

static bool isBGR565(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 16)
		&& (pf.dwRBitMask == 0x0000f800)
		&& (pf.dwGBitMask == 0x000007e0)
		&& (pf.dwBBitMask == 0x0000001f));
}

static bool isINDEX8(const PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_INDEXED) && (pf.dwRGBBitCount == 8));
}

} // namespace Lumix::gpu::DDS

