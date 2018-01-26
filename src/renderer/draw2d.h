#pragma once


#include "engine/array.h"
#include "engine/vec.h"
#include "imgui/imgui.h"

// ImDraw2D and dependencies from imgui + refactored
namespace Lumix
{


struct Draw2D;
struct Font;
struct FontAtlas;


typedef unsigned short Wchar;
struct LUMIX_RENDERER_API FontConfig
{
	void*           FontData;                   //          // TTF data
	int             FontDataSize;               //          // TTF data size
	bool            FontDataOwnedByAtlas;       // true     // TTF data ownership taken by the container FontAtlas (will delete memory itself). Set to true
	int             FontNo;                     // 0        // Index of font within TTF file
	float           SizePixels;                 //          // Size in pixels for rasterizer
	int             OversampleH, OversampleV;   // 3, 1     // Rasterize at higher quality for sub-pixel positioning. We don't use sub-pixel positions on the Y axis.
	bool            PixelSnapH;                 // false    // Align every glyph to pixel boundary. Useful e.g. if you are merging a non-pixel aligned font with the default font. If enabled, you can set OversampleH/V to 1.
	Vec2            GlyphExtraSpacing;          // 0, 0     // Extra spacing (in pixels) between glyphs
	const Wchar*    GlyphRanges;                //          // Pointer to a user-provided list of Unicode range (2 value per range, values are inclusive, zero-terminated list). THE ARRAY DATA NEEDS TO PERSIST AS LONG AS THE FONT IS ALIVE.
	bool            MergeMode;                  // false    // Merge into previous Font, so you can combine multiple inputs font into one Font (e.g. ASCII font + icons + Japanese glyphs).
	bool            MergeGlyphCenterV;          // false    // When merging (multiple FontInput for one Font), vertically center new glyphs instead of aligning their baseline

												// [Internal]
	char            Name[32];                               // Name (strictly for debugging)
	Font*         DstFont;

	FontConfig();
};

struct LUMIX_RENDERER_API Font
{
	struct Glyph
	{
		Wchar                 Codepoint;
		float                   XAdvance;
		float                   X0, Y0, X1, Y1;
		float                   U0, V0, U1, V1;     // Texture coordinates
	};

	// Members: Hot ~62/78 bytes
	float                       FontSize;           // <user set>   // Height of characters, set during loading (don't change after loading)
	float                       Scale;              // = 1.f        // Base font scale, multiplied by the per-window font scale which you can adjust with SetFontScale()
	Vec2                      DisplayOffset;      // = (0.f,1.f)  // Offset font rendering by xx pixels
	Array<Glyph>             Glyphs;             //              // All glyphs.
	Array<float>             IndexXAdvance;      //              // Sparse. Glyphs->XAdvance in a directly indexable way (more cache-friendly, for CalcTextSize functions which are often bottleneck in large UI).
	Array<unsigned short>    IndexLookup;        //              // Sparse. Index glyphs by Unicode code-point.
	const Glyph*                FallbackGlyph;      // == FindGlyph(FontFallbackChar)
	float                       FallbackXAdvance;   // == FallbackGlyph->XAdvance
	Wchar                     FallbackChar;       // = '?'        // Replacement glyph if one isn't found. Only set via SetFallbackChar()

													// Members: Cold ~18/26 bytes
	short                       ConfigDataCount;    // ~ 1          // Number of FontConfig involved in creating this font. Bigger than 1 when merging multiple font sources into one Font.
	FontConfig*               ConfigData;         //              // Pointer within ContainerAtlas->ConfigData
	FontAtlas*                ContainerAtlas;     //              // What we has been loaded into
	float                       Ascent, Descent;    //              // Ascent: distance from top to bottom of e.g. 'A' [0..FontSize]
	int                         MetricsTotalSurface;//              // Total surface in pixels to get an idea of the font rasterization/texture cost (not exact, we approximate the cost of padding between glyphs)

													// Methods
	Font(IAllocator& allocator);
	~Font();
	void              Clear();
	void              BuildLookupTable();
	const Glyph*      FindGlyph(Wchar c) const;
	void              SetFallbackChar(Wchar c);
	float                       GetCharAdvance(Wchar c) const { return ((int)c < IndexXAdvance.size()) ? IndexXAdvance[(int)c] : FallbackXAdvance; }
	bool                        IsLoaded() const { return ContainerAtlas != nullptr; }

	// 'max_width' stops rendering after a certain width (could be turned into a 2d size). FLT_MAX to disable.
	// 'wrap_width' enable automatic word-wrapping across multiple lines to fit into given width. 0.0f to disable.
	Vec2            CalcTextSizeA(float size, float max_width, float wrap_width, const char* text_begin, const char* text_end = nullptr, const char** remaining = nullptr) const; // utf8
	const char*       CalcWordWrapPositionA(float scale, const char* text, const char* text_end, float wrap_width) const;
	void              RenderChar(Draw2D* draw_list, float size, Vec2 pos, u32 col, unsigned short c) const;
	void              RenderText(Draw2D* draw_list, float size, Vec2 pos, u32 col, const Vec4& clip_rect, const char* text_begin, const char* text_end, float wrap_width = 0.0f, bool cpu_fine_clip = false) const;

	// Private
	void              GrowIndex(int new_size);
	void              AddRemapChar(Wchar dst, Wchar src, bool overwrite_dst = true); // Makes 'dst' character/glyph points to 'src' character/glyph. Currently needs to be called AFTER fonts have been built.
};


struct LUMIX_RENDERER_API FontAtlas
{
	FontAtlas(IAllocator& allocator);
	~FontAtlas();
	Font*           AddFont(const FontConfig* font_cfg);
	Font*           AddFontDefault(const FontConfig* font_cfg = nullptr);
	Font*           AddFontFromFileTTF(const char* filename, float size_pixels, const FontConfig* font_cfg = nullptr, const Wchar* glyph_ranges = nullptr);
	Font*           AddFontFromMemoryTTF(void* ttf_data, int ttf_size, float size_pixels, const FontConfig* font_cfg = nullptr, const Wchar* glyph_ranges = nullptr);                                        // Transfer ownership of 'ttf_data' to FontAtlas, will be deleted after Build()
	Font*           AddFontFromMemoryCompressedTTF(const void* compressed_ttf_data, int compressed_ttf_size, float size_pixels, const FontConfig* font_cfg = nullptr, const Wchar* glyph_ranges = nullptr);  // 'compressed_ttf_data' still owned by caller. Compress with binary_to_compressed_c.cpp
	Font*           AddFontFromMemoryCompressedBase85TTF(const char* compressed_ttf_data_base85, float size_pixels, const FontConfig* font_cfg = nullptr, const Wchar* glyph_ranges = nullptr);              // 'compressed_ttf_data_base85' still owned by caller. Compress with binary_to_compressed_c.cpp with -base85 paramaeter
	void              ClearTexData();             // Clear the CPU-side texture data. Saves RAM once the texture has been copied to graphics memory.
	void              ClearInputData();           // Clear the input TTF data (inc sizes, glyph ranges)
	void              ClearFonts();               // Clear the ImGui-side font data (glyphs storage, UV coordinates)
	void              Clear();                    // Clear all

													// Retrieve texture data
													// User is in charge of copying the pixels into graphics memory, then call SetTextureUserID()
													// After loading the texture into your graphic system, store your texture handle in 'TexID' (ignore if you aren't using multiple fonts nor images)
													// RGBA32 format is provided for convenience and high compatibility, but note that all RGB pixels are white, so 75% of the memory is wasted.
													// Pitch = Width * BytesPerPixels
	void              GetTexDataAsAlpha8(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel = nullptr);  // 1 byte per-pixel
	void              GetTexDataAsRGBA32(unsigned char** out_pixels, int* out_width, int* out_height, int* out_bytes_per_pixel = nullptr);  // 4 bytes-per-pixel
	void                        SetTexID(void* id) { TexID = id; }

	// Helpers to retrieve list of common Unicode ranges (2 value per range, values are inclusive, zero-terminated list)
	// NB: Make sure that your string are UTF-8 and NOT in your local code page. See FAQ for details.
	const Wchar*    GetGlyphRangesDefault();    // Basic Latin, Extended Latin
	const Wchar*    GetGlyphRangesKorean();     // Default + Korean characters
	const Wchar*    GetGlyphRangesJapanese();   // Default + Hiragana, Katakana, Half-Width, Selection of 1946 Ideographs
	const Wchar*    GetGlyphRangesChinese();    // Japanese + full set of about 21000 CJK Unified Ideographs
	const Wchar*    GetGlyphRangesCyrillic();   // Default + about 400 Cyrillic characters
	const Wchar*    GetGlyphRangesThai();       // Default + Thai characters

												// Members
												// (Access texture data via GetTexData*() calls which will setup a default font for you.)
	void*                       TexID;              // User data to refer to the texture once it has been uploaded to user's graphic systems. It ia passed back to you during rendering.
	unsigned char*              TexPixelsAlpha8;    // 1 component per pixel, each component is unsigned 8-bit. Total size = TexWidth * TexHeight
	unsigned int*               TexPixelsRGBA32;    // 4 component per pixel, each component is unsigned 8-bit. Total size = TexWidth * TexHeight * 4
	int                         TexWidth;           // Texture width calculated during Build().
	int                         TexHeight;          // Texture height calculated during Build().
	int                         TexDesiredWidth;    // Texture width desired by user before Build(). Must be a power-of-two. If have many glyphs your graphics API have texture size restrictions you may want to increase texture width to decrease height.
	Vec2                      TexUvWhitePixel;    // Texture coordinates to a white pixel
	Array<Font*>           Fonts;              // Hold all the fonts returned by AddFont*. Fonts[0] is the default font upon calling ImGui::NewFrame(), use ImGui::PushFont()/PopFont() to change the current font.
	IAllocator& allocator;
	// Private
	Array<FontConfig>      ConfigData;         // Internal data
	bool              Build();            // Build pixels data. This is automatically for you by the GetTexData*** functions.
	void              RenderCustomTexData(int pass, void* rects);
};


struct LUMIX_RENDERER_API Draw2D
{
	typedef unsigned short DrawIdx;
	typedef void* TextureID;


	struct DrawVert
	{
		Vec2 pos;
		Vec2 uv;
		u32 col;
	};


	struct DrawCmd
	{
		unsigned int    ElemCount;              // Number of indices (multiple of 3) to be rendered as triangles. Vertices are stored in the callee Draw2D's vtx_buffer[] array, indices in idx_buffer[].
		Vec4          ClipRect;               // Clipping rectangle (x1, y1, x2, y2)
		TextureID     TextureId;              // User-provided texture ID. Set by user in FontAtlas::SetTexID() for fonts or passed to Image*() functions. Ignore if never using images or multiple fonts atlas.

		DrawCmd()
		{
			ElemCount = 0;
			ClipRect.x = ClipRect.y = -8192.0f;
			ClipRect.z = ClipRect.w = +8192.0f;
			TextureId = nullptr;
		}
	};

	struct DrawChannel
	{
		DrawChannel(IAllocator& allocator) : CmdBuffer(allocator), IdxBuffer(allocator) {}
		Array<DrawCmd>     CmdBuffer;
		Array<DrawIdx>     IdxBuffer;
	};

	IAllocator& allocator;
	Vec2 FontTexUvWhitePixel;
	bool AntiAliasedLines = true;
	bool AntiAliasedShapes = true;
	float CurveTessellationTol = 1.25f;
	Array<DrawCmd> CmdBuffer;
	Array<DrawIdx> IdxBuffer;
	Array<DrawVert> VtxBuffer;

	unsigned int            _VtxCurrentIdx;     // [Internal] == VtxBuffer.Size
	DrawVert*             _VtxWritePtr;       // [Internal] point within VtxBuffer.Data after each add command (to avoid using the Array<> operators too much)
	DrawIdx*              _IdxWritePtr;       // [Internal] point within IdxBuffer.Data after each add command (to avoid using the Array<> operators too much)
	Array<Vec4>        _ClipRectStack;     // [Internal]
	Array<TextureID>   _TextureIdStack;    // [Internal]
	Array<Vec2>        _Path;              // [Internal] current path building
	int                     _ChannelsCurrent;   // [Internal] current channel number (0)
	int                     _ChannelsCount;     // [Internal] number of active channels (1+)
	Array<DrawChannel> _Channels;          // [Internal] draw channels for columns API (not resized down so _ChannelsCount may be smaller than _Channels.Size)

	Draw2D(IAllocator& _allocator)
		: CmdBuffer(_allocator)
		, IdxBuffer(_allocator)
		, VtxBuffer(_allocator)
		, _ClipRectStack(_allocator)
		, _TextureIdStack(_allocator)
		, _Path(_allocator)
		, _Channels(_allocator)
		, allocator(_allocator)
		, FontTexUvWhitePixel(0, 0)
	{
		Clear();
	}

	~Draw2D() { ClearFreeMemory(); }
	void  PushClipRect(Vec2 clip_rect_min, Vec2 clip_rect_max, bool intersect_with_current_clip_rect = false);  // Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse clipping. Prefer using higher-level ImGui::PushClipRect() to affect logic (hit-testing and widget culling)
	void  PushClipRectFullScreen();
	void  PopClipRect();
	void  PushTextureID(const TextureID& texture_id);
	void  PopTextureID();

	// Primitives
	void  AddLine(const Vec2& a, const Vec2& b, u32 col, float thickness = 1.0f);
	void  AddRect(const Vec2& a, const Vec2& b, u32 col, float rounding = 0.0f, int rounding_corners_flags = ~0, float thickness = 1.0f);   // a: upper-left, b: lower-right, rounding_corners_flags: 4-bits corresponding to which corner to round
	void  AddRectFilled(const Vec2& a, const Vec2& b, u32 col, float rounding = 0.0f, int rounding_corners_flags = ~0);                     // a: upper-left, b: lower-right
	void  AddRectFilledMultiColor(const Vec2& a, const Vec2& b, u32 col_upr_left, u32 col_upr_right, u32 col_bot_right, u32 col_bot_left);
	void  AddQuad(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, u32 col, float thickness = 1.0f);
	void  AddQuadFilled(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, u32 col);
	void  AddTriangle(const Vec2& a, const Vec2& b, const Vec2& c, u32 col, float thickness = 1.0f);
	void  AddTriangleFilled(const Vec2& a, const Vec2& b, const Vec2& c, u32 col);
	void  AddCircle(const Vec2& centre, float radius, u32 col, int num_segments = 12, float thickness = 1.0f);
	void  AddCircleFilled(const Vec2& centre, float radius, u32 col, int num_segments = 12);
	void  AddText(const Font* font, float font_size, const Vec2& pos, u32 col, const char* text_begin, const char* text_end = nullptr, float wrap_width = 0.0f, const Vec4* cpu_fine_clip_rect = nullptr);
	void  AddImage(TextureID user_texture_id, const Vec2& a, const Vec2& b, const Vec2& uv0 = Vec2(0, 0), const Vec2& uv1 = Vec2(1, 1), u32 col = 0xFFFFFFFF);
	void  AddPolyline(const Vec2* points, const int num_points, u32 col, bool closed, float thickness, bool anti_aliased);
	void  AddConvexPolyFilled(const Vec2* points, const int num_points, u32 col, bool anti_aliased);
	void  AddBezierCurve(const Vec2& pos0, const Vec2& cp0, const Vec2& cp1, const Vec2& pos1, u32 col, float thickness, int num_segments = 0);

	// Stateful path API, add points then finish with PathFill() or PathStroke()
	inline    void  PathClear() { _Path.resize(0); }
	inline    void  PathLineTo(const Vec2& pos) { _Path.push(pos); }
	inline    void  PathLineToMergeDuplicate(const Vec2& pos) { if (_Path.size() == 0 || memcmp(&_Path[_Path.size() - 1], &pos, 8) != 0) _Path.push(pos); }
	inline    void  PathFill(u32 col) { AddConvexPolyFilled(&_Path[0], _Path.size(), col, true); PathClear(); }
	inline    void  PathStroke(u32 col, bool closed, float thickness = 1.0f) { AddPolyline(&_Path[0], _Path.size(), col, closed, thickness, true); PathClear(); }
	void  PathArcTo(const Vec2& centre, float radius, float a_min, float a_max, int num_segments = 10);
	void  PathArcToFast(const Vec2& centre, float radius, int a_min_of_12, int a_max_of_12);                                // Use precomputed angles for a 12 steps circle
	void  PathBezierCurveTo(const Vec2& p1, const Vec2& p2, const Vec2& p3, int num_segments = 0);
	void  PathRect(const Vec2& rect_min, const Vec2& rect_max, float rounding = 0.0f, int rounding_corners_flags = ~0);   // rounding_corners_flags: 4-bits corresponding to which corner to round

																															// Channels
																															// - Use to simulate layers. By switching channels to can render out-of-order (e.g. submit foreground primitives before background primitives)
																															// - Use to minimize draw calls (e.g. if going back-and-forth between multiple non-overlapping clipping rectangles, prefer to append into separate channels then merge at the end)
	void  ChannelsSplit(int channels_count);
	void  ChannelsMerge();
	void  ChannelsSetCurrent(int channel_index);

	// Advanced
	void  AddDrawCmd();                                               // This is useful if you need to forcefully create a new draw call (to allow for dependent rendering / blending). Otherwise primitives are merged into the same draw-call as much as possible

																		// Internal helpers
																		// NB: all primitives needs to be reserved via PrimReserve() beforehand!
	void  Clear();
	void  ClearFreeMemory();
	void  PrimReserve(int idx_count, int vtx_count);
	void  PrimRect(const Vec2& a, const Vec2& b, u32 col);      // Axis aligned rectangle (composed of two triangles)
	void  PrimRectUV(const Vec2& a, const Vec2& b, const Vec2& uv_a, const Vec2& uv_b, u32 col);
	void  PrimQuadUV(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, const Vec2& uv_a, const Vec2& uv_b, const Vec2& uv_c, const Vec2& uv_d, u32 col);
	inline    void  PrimWriteVtx(const Vec2& pos, const Vec2& uv, u32 col) { _VtxWritePtr->pos = pos; _VtxWritePtr->uv = uv; _VtxWritePtr->col = col; _VtxWritePtr++; _VtxCurrentIdx++; }
	inline    void  PrimWriteIdx(DrawIdx idx) { *_IdxWritePtr = idx; _IdxWritePtr++; }
	inline    void  PrimVtx(const Vec2& pos, const Vec2& uv, u32 col) { PrimWriteIdx((DrawIdx)_VtxCurrentIdx); PrimWriteVtx(pos, uv, col); }
	void  UpdateClipRect();
	void  UpdateTextureID();
};


} //namespace Lumix