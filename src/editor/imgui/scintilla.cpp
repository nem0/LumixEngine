/*
 * Copyright 2011-2016 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "core/string.h"
#include "engine/debug/debug.h"

#include <algorithm>
#include <map>

#include <string>
#include <vector>
#include <string.h>

#include "../external/scintilla/include/Platform.h"
#include "../external/scintilla/include/Scintilla.h"
#include "../external/scintilla/include/ILexer.h"
#include "../external/scintilla/include/SciLexer.h"
#include "../external/scintilla/internal/LexerModule.h"
#include "../external/scintilla/internal/SplitVector.h"
#include "../external/scintilla/internal/Partitioning.h"
#include "../external/scintilla/internal/RunStyles.h"
#include "../external/scintilla/internal/Catalogue.h"
#include "../external/scintilla/internal/ContractionState.h"
#include "../external/scintilla/internal/CellBuffer.h"
#include "../external/scintilla/internal/KeyMap.h"
#include "../external/scintilla/internal/Indicator.h"
#include "../external/scintilla/internal/XPM.h"
#include "../external/scintilla/internal/LineMarker.h"
#include "../external/scintilla/internal/Style.h"
#include "../external/scintilla/internal/ViewStyle.h"
#include "../external/scintilla/internal/Decoration.h"
#include "../external/scintilla/internal/CharClassify.h"
#include "../external/scintilla/internal/CaseFolder.h"
#include "../external/scintilla/internal/Document.h"
#include "../external/scintilla/internal/Selection.h"
#include "../external/scintilla/internal/PositionCache.h"
#include "../external/scintilla/internal/EditModel.h"
#include "../external/scintilla/internal/MarginView.h"
#include "../external/scintilla/internal/EditView.h"
#include "../external/scintilla/internal/Editor.h"
#include "../external/scintilla/internal/AutoComplete.h"
#include "../external/scintilla/internal/CallTip.h"
#include "../external/scintilla/internal/ScintillaBase.h"

#define STBTT_DEF extern
#include "imgui/stb_truetype.h"

#include "imgui.h"
#include <cstdint>
#include "editor/platform_interface.h"


namespace Scintilla
{
	ElapsedTime::ElapsedTime() {}
	double ElapsedTime::Duration(bool reset) { return 0; }
}

#define IMGUI_NEW(type)         new (ImGui::MemAlloc(sizeof(type) ) ) type
#define IMGUI_DELETE(type, obj) reinterpret_cast<type*>(obj)->~type(), ImGui::MemFree(obj)

static void fillRectangle(Scintilla::PRectangle _rc, Scintilla::ColourDesired _color)
{
	const uint32_t abgr = (uint32_t)_color.AsLong();

	ImVec2 pos = ImGui::GetCursorScreenPos();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddDrawCmd();
	drawList->AddRectFilled(
		  ImVec2(_rc.left  + pos.x, _rc.top    + pos.y)
		, ImVec2(_rc.right + pos.x, _rc.bottom + pos.y)
		, abgr
		);
}

static inline uint32_t makeRgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 0xFF)
{
	return a << 24 | b << 16 | g << 8 | r;
}

struct FontInt
{
	ImFont* m_font;
	float m_scale;
	float m_fontSize;
};


class SurfaceInt : public Scintilla::Surface
{
public:
	SurfaceInt()
	{
	}

	virtual ~SurfaceInt()
	{
	}

	virtual void Init(Scintilla::WindowID /*_wid*/) override
	{
	}

	virtual void Init(Scintilla::SurfaceID /*_sid*/, Scintilla::WindowID /*_wid*/) override
	{
	}

	virtual void InitPixMap(int /*_width*/, int /*_height*/, Scintilla::Surface* /*_surface*/, Scintilla::WindowID /*_wid*/) override
	{
	}

	virtual void Release() override
	{
	}

	virtual bool Initialised() override
	{
		return true;
	}

	virtual void PenColour(Scintilla::ColourDesired /*_fore*/) override
	{
	}

	virtual int LogPixelsY() override
	{
		return 72;
	}

	virtual int DeviceHeightFont(int /*points*/) override
	{
		return ImGui::GetIO().Fonts->Fonts[0]->FontSize * 100;
	}

	virtual void MoveTo(int _x, int _y) override
	{
	}

	virtual void LineTo(int _x, int _y) override
	{
	}

	virtual void Polygon(Scintilla::Point *pts, int npts, Scintilla::ColourDesired fore, Scintilla::ColourDesired back) override
	{
	}

	virtual void RectangleDraw(Scintilla::PRectangle rc, Scintilla::ColourDesired fore, Scintilla::ColourDesired back) override
	{
		FillRectangle(rc, back);
	}

	virtual void FillRectangle(Scintilla::PRectangle rc, Scintilla::ColourDesired back) override
	{
		fillRectangle(rc, back);
	}

	virtual void FillRectangle(Scintilla::PRectangle rc, Scintilla::Surface& surfacePattern) override
	{
	}

	virtual void RoundedRectangle(Scintilla::PRectangle rc, Scintilla::ColourDesired fore, Scintilla::ColourDesired back) override
	{
	}

	virtual void AlphaRectangle(Scintilla::PRectangle _rc, int /*_cornerSize*/, Scintilla::ColourDesired _fill, int _alphaFill, Scintilla::ColourDesired /*_outline*/, int /*_alphaOutline*/, int /*_flags*/) override
	{
		unsigned int back = 0
			| (uint32_t)( (_fill.AsLong() & 0xffffff)
			| ( (_alphaFill & 0xff) << 24) )
			;

		FillRectangle(_rc, Scintilla::ColourDesired(back) );
	}


	virtual void DrawRGBAImage(Scintilla::PRectangle /*_rc*/, int /*_width*/, int /*_height*/, const unsigned char* /*_pixelsImage*/) override
	{
	}

	virtual void Ellipse(Scintilla::PRectangle rc, Scintilla::ColourDesired fore, Scintilla::ColourDesired /*back*/) override
	{
		FillRectangle(rc, fore);
	}

	virtual void Copy(Scintilla::PRectangle /*_rc*/, Scintilla::Point /*_from*/, Scintilla::Surface& /*_surfaceSource*/) override
	{
	}

	virtual void DrawTextNoClip(Scintilla::PRectangle rc, Scintilla::Font& _font, Scintilla::XYPOSITION ybase, const char *s, int len, Scintilla::ColourDesired fore, Scintilla::ColourDesired back) override
	{
		DrawTextBase(rc, _font, ybase, s, len, fore);
	}

	virtual void DrawTextClipped(Scintilla::PRectangle rc, Scintilla::Font& _font, Scintilla::XYPOSITION ybase, const char *s, int len, Scintilla::ColourDesired fore, Scintilla::ColourDesired back) override
	{
		DrawTextBase(rc, _font, ybase, s, len, fore);
	}

	virtual void DrawTextTransparent(Scintilla::PRectangle rc, Scintilla::Font& _font, Scintilla::XYPOSITION ybase, const char *s, int len, Scintilla::ColourDesired fore) override
	{
		DrawTextBase(rc, _font, ybase, s, len, fore);
	}

	virtual void MeasureWidths(Scintilla::Font& _font, const char* _str, int _len, Scintilla::XYPOSITION* _positions) override
	{
		float position = 0;

		ImFont* imFont = ImGui::GetWindowFont();
		FontInt* fi = (FontInt*)_font.GetID();
		while (_len--)
		{
			position     += imFont->GetCharAdvance( (unsigned short)*_str++) * fi->m_scale;
			*_positions++ = position;
		}
	}

	virtual Scintilla::XYPOSITION WidthText(Scintilla::Font& _font, const char* _str, int _len) override
	{
		FontInt* fi = (FontInt*)_font.GetID();
		ImVec2 t = ImGui::CalcTextSize(_str, _str + _len);
		return t.x * fi->m_scale;
	}

	virtual Scintilla::XYPOSITION WidthChar(Scintilla::Font& _font, char ch) override
	{
		FontInt* fi = (FontInt*)_font.GetID();
		return fi->m_font->GetCharAdvance( (unsigned int)ch) * fi->m_scale;
	}

	virtual Scintilla::XYPOSITION Ascent(Scintilla::Font& _font) override
	{
		FontInt* fi = (FontInt*)_font.GetID();
		return fi->m_font->Ascent * fi->m_scale;
	}

	virtual Scintilla::XYPOSITION Descent(Scintilla::Font& _font) override
	{
		FontInt* fi = (FontInt*)_font.GetID();
		return -fi->m_font->Descent * fi->m_scale;
	}

	virtual Scintilla::XYPOSITION InternalLeading(Scintilla::Font& /*_font*/) override
	{
		return 0;
	}

	virtual Scintilla::XYPOSITION ExternalLeading(Scintilla::Font& /*_font*/) override
	{
		return 0;
	}

	virtual Scintilla::XYPOSITION Height(Scintilla::Font& _font) override
	{
		return Ascent(_font) + Descent(_font);
	}

	virtual Scintilla::XYPOSITION AverageCharWidth(Scintilla::Font& _font) override
	{
		return WidthChar(_font, 'n');
	}

	virtual void SetClip(Scintilla::PRectangle /*_rc*/) override
	{
	}

	virtual void FlushCachedState() override
	{
	}

	virtual void SetUnicodeMode(bool /*_unicodeMode*/) override
	{
	}

	virtual void SetDBCSMode(int /*_codePage*/) override
	{
	}

private:
	void DrawTextBase(Scintilla::PRectangle _rc, Scintilla::Font& _font, float _ybase, const char* _str, int _len, Scintilla::ColourDesired _fore)
	{
		float xt = _rc.left;
		float yt = _ybase;

		uint32_t fore = (uint32_t)_fore.AsLong();
		FontInt* fi = (FontInt*)_font.GetID();

		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddText(fi->m_font
			, fi->m_fontSize
			, ImVec2(xt + pos.x, yt + pos.y - fi->m_fontSize)
			, fore
			, _str
			, _str + _len
			);
	}

	Scintilla::ColourDesired m_penColour;
};

struct WindowInt
{
	WindowInt()
		: m_show(false)
	{
	}

	Scintilla::PRectangle position;
	bool m_show;
};

WindowInt* AllocateWindowInt()
{
	return new WindowInt;
}

inline WindowInt* GetWindow(Scintilla::WindowID id)
{
	return (WindowInt*)id;
}

class ListBoxInt : public Scintilla::ListBox
{
public:
	ListBoxInt()
		: m_maxStrWidth(0)
		, m_lineHeight(10)
		, m_desiredVisibleRows(5)
		, m_aveCharWidth(8)
		, m_unicodeMode(false)
	{
	}

	~ListBoxInt()
	{
	}

	virtual void SetFont(Scintilla::Font& /*_font*/) override
	{
	}

	virtual void Create(Scintilla::Window& /*_parent*/, int /*_ctrlID*/, Scintilla::Point _location, int _lineHeight, bool _unicodeMode, int /*_technology*/) override
	{
		m_location    = _location;
		m_lineHeight  = _lineHeight;
		m_unicodeMode = _unicodeMode;
		m_maxStrWidth = 0;
		wid = Scintilla::WindowID(4);
	}

	virtual void SetAverageCharWidth(int width) override
	{
		m_aveCharWidth = width;
	}

	virtual void SetVisibleRows(int rows) override
	{
		m_desiredVisibleRows = rows;
	}

	virtual int GetVisibleRows() const override
	{
		return m_desiredVisibleRows;
	}

	virtual Scintilla::PRectangle GetDesiredRect() override
	{
		Scintilla::PRectangle rc;
		rc.top    = 0;
		rc.left   = 0;
		rc.right  = 350;
		rc.bottom = 140;
		return rc;
	}

	virtual int CaretFromEdge() override
	{
		return 4 + 16;
	}

	virtual void Clear() override
	{
	}

	virtual void Append(char* /*s*/, int /*type = -1*/) override
	{
	}

	virtual int Length() override
	{
		return 0;
	}

	virtual void Select(int /*n*/) override
	{
	}

	virtual int GetSelection() override
	{
		return 0;
	}

	virtual int Find(const char* /*prefix*/) override
	{
		return 0;
	}

	virtual void GetValue(int /*n*/, char* value, int /*len*/) override
	{
		value[0] = '\0';
	}

	virtual void RegisterImage(int /*type*/, const char* /*xpm_data*/) override
	{
	}

	virtual void RegisterRGBAImage(int /*type*/, int /*width*/, int /*height*/, const unsigned char* /*pixelsImage*/) override
	{
	}

	virtual void ClearRegisteredImages() override
	{
	}

	virtual void SetDoubleClickAction(Scintilla::CallBackAction, void*) override
	{
	}

	virtual void SetList(const char* /*list*/, char /*separator*/, char /*typesep*/) override
	{
	}

private:
	Scintilla::Point m_location;
	size_t m_maxStrWidth;
	int    m_lineHeight;
	int    m_desiredVisibleRows;
	int    m_aveCharWidth;
	bool   m_unicodeMode;
};

struct Editor : public Scintilla::ScintillaBase
{
public:
	Editor()
		: m_width(0)
		, m_height(0)
		, m_searchResultIndication(0xff5A5A5A)
		, m_filteredSearchResultIndication(0xff5a5a5a)
		, m_occurrenceIndication(0xff5a5a5a)
		, m_writeOccurrenceIndication(0xff5a5a5a)
		, m_findScope(0xffddf0ff)
		, m_sourceHoverBackground(0xff000000)
		, m_singleLineComment(0xffa8a8a8)
		, m_multiLineComment(0xffa8a8a8)
		, m_commentTaskTag(0xffa8a8a8)
		, m_javadoc(0xffa8a8a8)
		, m_javadocLink(0xff548fa0)
		, m_javadocTag(0xffa8a8a8)
		, m_javadocKeyword(0xffea9c77)
		, m_class(0xfff9f9f9)
		, m_interface(0xfff9f9f9)
		, m_method(0xfff9f9f9)
		, m_methodDeclaration(0xfff9f9f9)
		, m_bracket(0xfff9f9f9)
		, m_number(0xfff9f9f9)
		, m_string(0xff76ba53)
		, m_operator(0xfff9f9f9)
		, m_keyword(0xffea9c77)
		, m_annotation(0xffa020f0)
		, m_staticMethod(0xfff9f9f9)
		, m_localVariable(0xff4b9ce9)
		, m_localVariableDeclaration(0xff4b9ce9)
		, m_field(0xff4b9ce9)
		, m_staticField(0xff4b9ce9)
		, m_staticFinalField(0xff4b9ce9)
		, m_deprecatedMember(0xfff9f9f9)
		, m_foreground(0xffffffff)
		, m_lineNumber(0xff00ffff)
	{
	}

	virtual ~Editor()
	{
	}

	virtual void Initialise() override
	{
		wMain = AllocateWindowInt();

		ImGuiIO& io = ImGui::GetIO();
		wMain.SetPosition(Scintilla::PRectangle::FromInts(0, 0, int(io.DisplaySize.x), int(io.DisplaySize.y) ) );

		view.bufferedDraw = false;

		command(SCI_SETLEXER, SCLEX_LUA);
		command(SCI_SETSTYLEBITS, 7);

		const int   fontSize = 13;
		const char* fontName = "";

		m_foreground = (ImU32)ImColor(ImGui::GetStyle().Colors[ImGuiCol_Text]);
		m_background = (ImU32)ImColor(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
		setStyle(STYLE_DEFAULT, m_foreground, m_background, fontSize, fontName);
		command(SCI_STYLECLEARALL);
		command(SCI_SETCARETFORE, 0xff000000, 0);
		command(SCI_SETEXTRAASCENT, 3);
		command(SCI_SETEXTRADESCENT, 0);
		setStyle(SCE_LUA_NUMBER, 0xffff00ff, m_background, fontSize, fontName);
		setStyle(SCE_LUA_WORD, 0xffff00ff, m_background, fontSize, fontName);
		setStyle(SCE_LUA_WORD2, 0xffff00ff, m_background, fontSize, fontName);
		setStyle(SCE_LUA_WORD3, 0xffff00ff, m_background, fontSize, fontName);
		setStyle(SCE_LUA_WORD4, 0xffff00ff, m_background, fontSize, fontName);
		setStyle(SCE_LUA_WORD5, 0xffff00ff, m_background, fontSize, fontName);
		setStyle(SCE_LUA_OPERATOR, 0xffff00ff, m_background, fontSize, fontName);
		command(SCI_SETKEYWORDS, 0, (intptr_t)"and break do else elseif "
			"end false for function if "
			"in local nil not or "
			"repeat return then true until while ");
		char tmp[200];
		command(SCI_DESCRIBEKEYWORDSETS, 0, (intptr_t)tmp);
		command(SCI_SETUSETABS, 1);
		command(SCI_SETTABWIDTH, 4);
		command(SCI_SETMARGINWIDTHN, 0, 44);
		command(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
		command(SCI_SETMARGINMASKN, 1, ~SC_MASK_FOLDERS);
		command(SCI_RGBAIMAGESETSCALE, 100);
		command(SCI_SETMARGINWIDTHN, 1, 0);
		command(SCI_MARKERDEFINE, 0, SC_MARK_RGBAIMAGE); 
			/*setStyle(STYLE_DEFAULT, 0xffffFFFF, 0xff000000, fontSize, fontName);
		/*setStyle(STYLE_INDENTGUIDE, 0xffc0c0c0, m_background, fontSize, fontName);
		setStyle(STYLE_BRACELIGHT, m_bracket, m_background, fontSize, fontName);
		setStyle(STYLE_BRACEBAD, m_bracket, m_background, fontSize, fontName);
		setStyle(STYLE_LINENUMBER, m_lineNumber, 0xd0333333, fontSize, fontName);

		setStyle(SCE_C_DEFAULT, m_foreground, m_background, fontSize, fontName);
		setStyle(SCE_C_STRING, m_string, m_background);
		setStyle(SCE_C_IDENTIFIER, m_method, m_background);
		setStyle(SCE_C_CHARACTER, m_string, m_background);
		setStyle(SCE_C_WORD, m_keyword, m_background);
		setStyle(SCE_C_WORD2, m_keyword, m_background);
		setStyle(SCE_C_GLOBALCLASS, m_class, m_background);
		setStyle(SCE_C_PREPROCESSOR, m_annotation, m_background);
		setStyle(SCE_C_NUMBER, m_number, m_background);
		setStyle(SCE_C_OPERATOR, m_operator, m_background);
		setStyle(SCE_C_COMMENT, m_multiLineComment, m_background);
		setStyle(SCE_C_COMMENTLINE, m_singleLineComment, m_background);
		setStyle(SCE_C_COMMENTDOC, m_multiLineComment, m_background);

		command(SCI_SETSELBACK, 1, m_background.AsLong() );
		command(SCI_SETCARETFORE, UINT32_MAX, 0);
		command(SCI_SETCARETLINEVISIBLE, 1);
		command(SCI_SETCARETLINEBACK, UINT32_MAX);
		command(SCI_SETCARETLINEBACKALPHA, 0x20);

		command(SCI_SETUSETABS, 1);
		command(SCI_SETTABWIDTH, 4);
		command(SCI_SETINDENTATIONGUIDES, SC_IV_REAL);

		command(SCI_MARKERSETBACK, 0, 0xff6a6a6a);
		command(SCI_MARKERSETFORE, 0, 0xff0000ff);

		command(SCI_SETMARGINWIDTHN, 0, 44);
		command(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
		command(SCI_SETMARGINMASKN, 1, ~SC_MASK_FOLDERS);
		command(SCI_RGBAIMAGESETSCALE, 100);
		command(SCI_SETMARGINWIDTHN, 1, 0);
		command(SCI_MARKERDEFINE, 0, SC_MARK_RGBAIMAGE);*/

		SetFocusState(true);
		CaretSetPeriod(0);
	}

	virtual void CreateCallTipWindow(Scintilla::PRectangle /*_rc*/) override
	{
		if (!ct.wCallTip.Created() )
		{
			ct.wCallTip = AllocateWindowInt();
			ct.wDraw = ct.wCallTip;
		}
	}

	virtual void AddToPopUp(const char* /*_label*/, int /*_cmd*/, bool /*_enabled*/) override
	{
	}

	void Resize(int /*_x*/, int /*_y*/, int _width, int _height)
	{
		m_width  = _width;
		m_height = _height;

		wMain.SetPosition(Scintilla::PRectangle::FromInts(0, 0, m_width, m_height) );
	}

	virtual void SetVerticalScrollPos() override
	{
	}

	virtual void SetHorizontalScrollPos() override
	{
		xOffset = 0;
	}

	virtual bool ModifyScrollBars(int /*nMax*/, int /*nPage*/) override
	{
		return false;
	}

	void ClaimSelection()
	{
	}

	virtual void Copy() override
	{
	}

	virtual void Paste() override
	{
	}

	virtual void NotifyChange() override
	{
		m_is_text_changed = true;
	}

	virtual void NotifyParent(Scintilla::SCNotification /*scn*/) override
	{
	}

	virtual void CopyToClipboard(const Scintilla::SelectionText& /*selectedText*/) override
	{
	}


	virtual void SetMouseCapture(bool /*on*/) override
	{
	}

	virtual bool HaveMouseCapture() override
	{
		return false;
	}

	virtual sptr_t DefWndProc(unsigned int /*iMessage*/, uptr_t /*wParam*/, sptr_t /*lParam*/) override
	{
		return 0;
	}

	intptr_t command(unsigned int _msg, uintptr_t _p0 = 0, intptr_t _p1 = 0)
	{
		return WndProc(_msg, _p0, _p1);
	}

	void getText(char* buffer, int buffer_size)
	{
		int len = command(SCI_GETTEXTLENGTH, 0, 0) + 1;
		len = len > buffer_size ? buffer_size : len;
		
		command(SCI_GETTEXT, len, (intptr_t)buffer);

	}

	void draw()
	{
		ImVec2 cursorPos = ImGui::GetCursorPos();
		ImVec2 regionMax = ImGui::GetWindowContentRegionMax();
		ImVec2 size = ImVec2( regionMax.x - cursorPos.x - 32
							, regionMax.y - cursorPos.y
							);

		Resize(0, 0, (int)size.x, (int)size.y);

		const bool shift = ImGui::GetIO().KeyShift;
		const bool ctrl = ImGui::GetIO().KeyCtrl;
		const bool alt = ImGui::GetIO().KeyAlt;

		if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::TAB) )
		{
			Editor::KeyDown(SCK_TAB, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::LEFT) )
		{
			Editor::KeyDown(SCK_LEFT, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::RIGHT) )
		{
			Editor::KeyDown(SCK_RIGHT, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::UP) )
		{
			Editor::KeyDown(SCK_UP, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::DOWN) )
		{
			Editor::KeyDown(SCK_DOWN, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::PAGE_UP) )
		{
			Editor::KeyDown(SCK_PRIOR, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::PAGE_DOWN) )
		{
			Editor::KeyDown(SCK_NEXT, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::HOME) )
		{
			Editor::KeyDown(SCK_HOME, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::END) )
		{
			Editor::KeyDown(SCK_END, shift, ctrl, alt);
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::DEL) )
		{
			Editor::KeyDown(SCK_DELETE, shift, ctrl, alt);
		}
		/*else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::BACKSPACE) )
		{
			Editor::KeyDown(SCK_BACK, shift, ctrl, alt); inputGetChar();
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::RETURN) )
		{
			Editor::KeyDown(SCK_RETURN, shift, ctrl, alt); inputGetChar();
		}
		else if (ImGui::IsKeyPressed((int)PlatformInterface::Keys::ESC) 
		{
			Editor::KeyDown(SCK_ESCAPE, shift, ctrl, alt);
		}
		else if (ctrl && ImGui::IsKeyPressed((int)'A'))
		{
			Editor::KeyDown('A', shift, ctrl, alt); inputGetChar();
		}
		else if (ctrl && ImGui::IsKeyPressed((int)'C') )
		{
			Editor::KeyDown('C', shift, ctrl, alt); inputGetChar();
		}
		else if (ctrl && ImGui::IsKeyPressed((int)'V') )
		{
			Editor::KeyDown('V', shift, ctrl, alt); inputGetChar();
		}
		else if (ctrl && ImGui::IsKeyPressed((int)'X') )
		{
			Editor::KeyDown('X', shift, ctrl, alt); inputGetChar();
		}
		else if (ctrl && ImGui::IsKeyPressed((int)'Y') )
		{
			Editor::KeyDown('Y', shift, ctrl, alt); inputGetChar();
		}
		else if (ctrl && ImGui::IsKeyPressed((int)'Z') )
		{
			Editor::KeyDown('Z', shift, ctrl, alt);	inputGetChar();
		}*/
		else if (ctrl || alt)
		{
			// ignore...
		}
		else
		{
			for (const uint8_t* ch = (const uint8_t*)ImGui::GetIO().InputCharacters; 0 != *ch; ++ch)
			{
				switch (*ch)
				{
				case '\b': Editor::KeyDown(SCK_BACK,   shift, ctrl, alt); break;
				case '\n': Editor::KeyDown(SCK_RETURN, shift, ctrl, alt); break;
				default:   Editor::AddCharUTF( (const char*)ch, 1);       break;
				}
			}
		}

		int32_t lineCount = int32_t(command(SCI_GETLINECOUNT) );
		int32_t firstVisibleLine = int32_t(command(SCI_GETFIRSTVISIBLELINE) );
		float fontHeight = ImGui::GetWindowFontSize();

		ImGuiIO& io = ImGui::GetIO();
		auto cp = io.MouseClickedPos[0];
		cp.x = cp.x - ImGui::GetCursorScreenPos().x;
		cp.y = cp.y - ImGui::GetCursorScreenPos().y;
		Scintilla::Point pt = Scintilla::Point::FromInts((int)cp.x, (int)cp.y);
		if (ImGui::IsMouseClicked(0))
		{
			ButtonDown(pt, (unsigned int)io.MouseDownDuration[0], false, false, false);
		}

		if(ImGui::IsMouseDown(0)) ButtonMove(pt);

		if (ImGui::IsMouseReleased(0))
		{
			ButtonUp(pt, 0, false);
		}

		Tick();

		ImGui::BeginGroup();
			ImGui::BeginChild("##editor", ImVec2(size.x, size.y-20) );
				Scintilla::AutoSurface surfaceWindow(this);
				if (surfaceWindow)
				{
					Paint(surfaceWindow, GetClientRectangle() );
					surfaceWindow->Release();
				}
			ImGui::EndChild();

			ImGui::SameLine();

			ImGui::BeginChild("##scroll");
				ImGuiListClipper clipper;
				clipper.Begin(lineCount, fontHeight*2.0f);

				if (m_lastFirstVisibleLine != firstVisibleLine)
				{
					m_lastFirstVisibleLine = firstVisibleLine;
					ImGui::SetScrollY(firstVisibleLine * fontHeight*2.0f);
				}
				else if (firstVisibleLine != clipper.DisplayStart)
				{
					command(SCI_SETFIRSTVISIBLELINE, clipper.DisplayStart);
				}

				clipper.End();
			ImGui::EndChild();

		ImGui::EndGroup();
	}

	void setStyle(int style, Scintilla::ColourDesired fore, Scintilla::ColourDesired back = UINT32_MAX, int size = -1, const char* face = NULL)
	{
		command(SCI_STYLESETFORE, uptr_t(style), fore.AsLong() );
		command(SCI_STYLESETBACK, uptr_t(style), back.AsLong() );

		if (size >= 1)
		{
			command(SCI_STYLESETSIZE, uptr_t(style), size);
		}

		if (face)
		{
			command(SCI_STYLESETFONT, uptr_t(style), reinterpret_cast<sptr_t>(face) );
		}
	}

	bool m_is_text_changed;

private:
	int m_width;
	int m_height;
	int m_lastFirstVisibleLine;

	Scintilla::ColourDesired m_searchResultIndication;
	Scintilla::ColourDesired m_filteredSearchResultIndication;
	Scintilla::ColourDesired m_occurrenceIndication;
	Scintilla::ColourDesired m_writeOccurrenceIndication;
	Scintilla::ColourDesired m_findScope;
	Scintilla::ColourDesired m_sourceHoverBackground;
	Scintilla::ColourDesired m_singleLineComment;
	Scintilla::ColourDesired m_multiLineComment;
	Scintilla::ColourDesired m_commentTaskTag;
	Scintilla::ColourDesired m_javadoc;
	Scintilla::ColourDesired m_javadocLink;
	Scintilla::ColourDesired m_javadocTag;
	Scintilla::ColourDesired m_javadocKeyword;
	Scintilla::ColourDesired m_class;
	Scintilla::ColourDesired m_interface;
	Scintilla::ColourDesired m_method;
	Scintilla::ColourDesired m_methodDeclaration;
	Scintilla::ColourDesired m_bracket;
	Scintilla::ColourDesired m_number;
	Scintilla::ColourDesired m_string;
	Scintilla::ColourDesired m_operator;
	Scintilla::ColourDesired m_keyword;
	Scintilla::ColourDesired m_annotation;
	Scintilla::ColourDesired m_staticMethod;
	Scintilla::ColourDesired m_localVariable;
	Scintilla::ColourDesired m_localVariableDeclaration;
	Scintilla::ColourDesired m_field;
	Scintilla::ColourDesired m_staticField;
	Scintilla::ColourDesired m_staticFinalField;
	Scintilla::ColourDesired m_deprecatedMember;
	Scintilla::ColourDesired m_background;
	Scintilla::ColourDesired m_currentLine;
	Scintilla::ColourDesired m_foreground;
	Scintilla::ColourDesired m_lineNumber;
	Scintilla::ColourDesired m_selectionBackground;
	Scintilla::ColourDesired m_selectionForeground;
};


// Scintilla hooks
namespace Scintilla
{
	Font::Font()
		: fid(0)
	{
	}

	Font::~Font()
	{
	}

	void Font::Create(const FontParameters& fp)
	{
		FontInt* newFont = (FontInt*)ImGui::MemAlloc(sizeof(FontInt) );
		fid = newFont;
		newFont->m_font = ImGui::GetIO().Fonts->Fonts[0];
		newFont->m_fontSize = fp.size;
		newFont->m_scale = fp.size / newFont->m_font->FontSize;
	}

	void Font::Release()
	{
		if (fid)
		{
			ImGui::MemFree( (FontInt*)fid);
		}
	}

	ColourDesired Platform::Chrome()
	{
		return makeRgba(0xe0, 0xe0, 0xe0);
	}

	ColourDesired Platform::ChromeHighlight()
	{
		return makeRgba(0xff, 0xff, 0xff);
	}

	const char* Platform::DefaultFont()
	{
		return "";
	}

	int Platform::DefaultFontSize()
	{
		return 15;
	}

	unsigned int Platform::DoubleClickTime()
	{
		return 500;
	}

	bool Platform::MouseButtonBounce()
	{
		return true;
	}

	void Platform::Assert(const char* _error, const char* _filename, int _line)
	{
		DebugPrintf("%s(%d): %s", _filename, _line, _error);
	}

	int Platform::Minimum(int a, int b)
	{
		return a < b ? a : b;
	}

	int Platform::Maximum(int a, int b)
	{
		return a > b ? a : b;
	}

	int Platform::Clamp(int val, int minVal, int maxVal)
	{
		return Maximum(Minimum(val, maxVal), minVal);
	}

	void Platform::DebugPrintf(const char* _format, ...)
	{
		char temp[8192];
		char* out = temp;
		va_list argList;
		va_start(argList, _format);
		int32_t len = vsnprintf(out, sizeof(temp), _format, argList);
		if ( (int32_t)sizeof(temp) < len)
		{
			out = (char*)alloca(len+1);
			len = vsnprintf(out, len, _format, argList);
		}
		va_end(argList);
		out[len] = '\0';
		Lumix::Debug::debugOutput(out);
	}

	Menu::Menu()
		: mid(0)
	{
	}

	void Menu::CreatePopUp()
	{
		Destroy();
		mid = MenuID(1);
	}

	void Menu::Destroy()
	{
		mid = 0;
	}

	void Menu::Show(Point /*_pt*/, Window& /*_w*/)
	{
		Destroy();
	}

	Surface* Surface::Allocate(int)
	{
		return IMGUI_NEW(SurfaceInt);
	}

	Window::~Window()
	{
	}

	void Window::Destroy()
	{
		if (wid)
		{
			Show(false);
			WindowInt* wi = GetWindow(wid);
			IMGUI_DELETE(WindowInt, wi);
		}

		wid = 0;
	}

	bool Window::HasFocus()
	{
		return true;
	}

	PRectangle Window::GetPosition()
	{
		if (0 == wid)
		{
			return PRectangle();
		}

		return GetWindow(wid)->position;
	}

	void Window::SetPosition(PRectangle rc)
	{
		GetWindow(wid)->position = rc;
	}

	void Window::SetPositionRelative(PRectangle _rc, Window /*_w*/)
	{
		SetPosition(_rc);
	}

	PRectangle Window::GetClientPosition()
	{
		if (0 == wid)
		{
			return PRectangle();
		}

		return GetWindow(wid)->position;
	}

	void Window::Show(bool _show)
	{
		if (0 != wid)
		{
			GetWindow(wid)->m_show = _show;
		}
	}

	void Window::InvalidateAll()
	{
	}

	void Window::InvalidateRectangle(PRectangle /*_rc*/)
	{
	}

	void Window::SetFont(Font& /*_font*/)
	{
	}

	void Window::SetCursor(Cursor /*_curs*/)
	{
		cursorLast = cursorText;
	}

	void Window::SetTitle(const char* /*_str*/)
	{
	}

	PRectangle Window::GetMonitorRect(Point /*_pt*/)
	{
		return PRectangle();
	}

	ListBox::ListBox()
	{
	}

	ListBox::~ListBox()
	{
	}

	ListBox* ListBox::Allocate()
	{
		return IMGUI_NEW(ListBoxInt);
	}

} // namespace Scintilla

namespace ImGui
{


void Scintilla(const char* _name, char* buffer, int buffer_size, const ImVec2& size, bool update)
{
	ImGuiStorage* storage = ImGui::GetStateStorage();

	ImGuiID id = ImGui::GetID(_name);
	auto* editor = (Editor*)storage->GetVoidPtr(id);
	if (NULL == editor)
	{
		editor = IMGUI_NEW(Editor);
		editor->Initialise();
		editor->Resize(0, 0, size.x, size.y);

		editor->command(SCI_SETTEXT, 0, (intptr_t)buffer);
		storage->SetVoidPtr(id, (void*)editor);
	}

	if (update) editor->command(SCI_SETTEXT, 0, (intptr_t)buffer);

	editor->draw();
	if (editor->m_is_text_changed)
	{
		editor->m_is_text_changed = false;
		editor->getText(buffer, buffer_size);
	}

}


} // namespace ImGui
