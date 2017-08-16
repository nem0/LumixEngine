#pragma once


#include "draw_list.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "imgui/imgui.h"
#include "litehtml.h"
#include "stb/stb_image.h"


namespace Lumix
{

static const ResourceType MATERIAL_TYPE("material");
static const ResourceType TEXTURE_TYPE("texture");


struct HTMLDocumentContainer : litehtml::document_container
{
	HTMLDocumentContainer(Engine& engine)
		: m_engine(engine)
		, m_draw_list(engine.getAllocator())
		, m_font_atlas(engine.getAllocator())
		, m_images(engine.getAllocator())
	{
	}


	litehtml::uint_ptr create_font(const litehtml::tchar_t* faceName,
		int size,
		int weight,
		litehtml::font_style italic,
		unsigned int decoration,
		litehtml::font_metrics* fm) override
	{
		Font* font = m_font_atlas.AddFontFromFileTTF("bin/veramono.ttf", (float)size);

		unsigned char* pixels;
		int width, height;
		m_font_atlas.GetTexDataAsRGBA32(&pixels, &width, &height);
		auto* material_manager = m_engine.getResourceManager().get(MATERIAL_TYPE);
		Resource* resource = material_manager->load(Path("pipelines/gui/gui.mat"));
		Material* material = (Material*)resource;

		Texture* old_texture = material->getTexture(0);
		Texture* texture = LUMIX_NEW(m_engine.getAllocator(), Texture)(
			Path("font"), *m_engine.getResourceManager().get(TEXTURE_TYPE), m_engine.getAllocator());

		texture->create(width, height, pixels);
		material->setTexture(0, texture);
		if (old_texture)
		{
			old_texture->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), old_texture);
		}

		m_font_atlas.TexID = &texture->handle;

		fm->height = int(font->Ascent - font->Descent);
		fm->ascent = (int)font->Ascent;
		fm->descent = (int)-font->Descent;
		//bool underline = (decoration & litehtml::font_decoration_underline) != 0;
		m_draw_list.FontTexUvWhitePixel = m_font_atlas.TexUvWhitePixel;
		return (litehtml::uint_ptr)font;
	}


	void delete_font(litehtml::uint_ptr hFont) override {}


	int text_width(const litehtml::tchar_t* text, litehtml::uint_ptr hFont) override
	{
		return 50;
	}


	void draw_text(litehtml::uint_ptr hdc,
		const litehtml::tchar_t* text,
		litehtml::uint_ptr hFont,
		litehtml::web_color color,
		const litehtml::position& pos) override
	{
		Font* font = (Font*)hFont;
		Vec2 imgui_pos = { m_pos.x + (float)pos.x, m_pos.y + pos.y };
		ImColor col(color.red, color.green, color.blue, color.alpha);
		m_draw_list.PushTextureID(font->ContainerAtlas->TexID);
		m_draw_list.AddText(font, font->FontSize, imgui_pos, col, text);
		m_draw_list.PopTextureID();
	}


	int pt_to_px(int pt) override { /*TODO*/ return pt; }
	int get_default_font_size() const override { return 16; }
	const litehtml::tchar_t* get_default_font_name() const override { return _t("Times New Roman"); }
	void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override {}


	bool loadFile(const char* path, Array<u8>* out_data)
	{
		ASSERT(out_data);
		FS::OsFile file;
		IAllocator& allocator = m_engine.getAllocator();
		if (!file.open(path, FS::Mode::OPEN_AND_READ, allocator)) return false;

		out_data->resize((int)file.size());
		bool success = file.read(&(*out_data)[0], out_data->size());

		file.close();
		return success;
	}


	void load_image(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, bool redraw_on_ready) override
	{
		IAllocator& allocator = m_engine.getAllocator();
		Array<u8> data(allocator);
		if (!loadFile(src, &data)) return;
		int channels;
		Image img;
		stbi_uc* pixels = stbi_load_from_memory(&data[0], data.size(), &img.w, &img.h, &channels, 4);
		if (!pixels) return;
		
		auto& rm = m_engine.getResourceManager();
		Texture* texture = LUMIX_NEW(allocator, Texture)(Path(src), *rm.get(TEXTURE_TYPE), allocator);
		texture->create(img.w, img.h, pixels);
		img.texture = texture;
		m_images.insert(crc32(src), img);
	}


	void get_image_size(const litehtml::tchar_t* src, const litehtml::tchar_t* baseurl, litehtml::size& sz) override
	{
		auto iter = m_images.find(crc32(src));
		if (iter.isValid())
		{
			sz.width = iter.value().w;
			sz.height = iter.value().h;
		}
		else
		{
			sz.width = sz.height = 100;
		}
	}


	void draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint& bg) override
	{
		Vec2 a(m_pos.x + bg.clip_box.left(), m_pos.y + bg.clip_box.top());
		Vec2 b(m_pos.x + bg.clip_box.right(), m_pos.y + bg.clip_box.bottom());
		if (bg.image.empty())
		{
			ImColor col(bg.color.red, bg.color.green, bg.color.blue, bg.color.alpha);
			m_draw_list.AddRectFilled(a, b, col);
			return;
		}

		auto iter = m_images.find(crc32(bg.image.c_str()));
		if (!iter.isValid()) return;

		auto img = iter.value();

		switch (bg.repeat)
		{
			case litehtml::background_repeat_no_repeat: m_draw_list.AddImage(&img.texture->handle, a, b); break;
			case litehtml::background_repeat_repeat_x:
			{
				Vec2 uv((b.x - a.x) / img.w, 0);
				m_draw_list.AddImage(&img.texture->handle, a, b, Vec2(0, 0), uv);
				break;
			}
			break;
			case litehtml::background_repeat_repeat_y:
			{
				Vec2 uv(0, (b.y - a.y) / img.h);
				m_draw_list.AddImage(&img.texture->handle, a, b, Vec2(0, 0), uv);
				break;
			}
			case litehtml::background_repeat_repeat:
			{
				Vec2 uv((b.x - a.x) / img.w, (b.y - a.y) / img.h);
				m_draw_list.AddImage(&img.texture->handle, a, b, Vec2(0, 0), uv);
				break;
			}
		}
	}


	void draw_borders(litehtml::uint_ptr hdc,
		const litehtml::borders& borders,
		const litehtml::position& draw_pos,
		bool root) override
	{
		if (borders.bottom.width != 0 && borders.bottom.style > litehtml::border_style_hidden)
		{
			Vec2 a(m_pos.x + draw_pos.left(), m_pos.y + draw_pos.bottom());
			Vec2 b(m_pos.x + draw_pos.right(), m_pos.y + draw_pos.bottom());
			ImColor col(borders.bottom.color.red, borders.bottom.color.green, borders.bottom.color.blue, borders.bottom.color.alpha);

			for (int x = 0; x < borders.bottom.width; x++)
			{
				m_draw_list.AddLine(a, b, col);
				++a.y;
				++b.y;
			}
		}

		if (borders.top.width != 0 && borders.top.style > litehtml::border_style_hidden)
		{
			Vec2 a(m_pos.x + draw_pos.left(), m_pos.y + draw_pos.top());
			Vec2 b(m_pos.x + draw_pos.right(), m_pos.y + draw_pos.top());
			ImColor col(borders.top.color.red, borders.top.color.green, borders.top.color.blue, borders.top.color.alpha);

			for (int x = 0; x < borders.top.width; x++)
			{
				m_draw_list.AddLine(a, b, col);
				++a.y;
				++b.y;
			}
		}

		if (borders.right.width != 0 && borders.right.style > litehtml::border_style_hidden)
		{
			Vec2 a(m_pos.x + draw_pos.right(), m_pos.y + draw_pos.top());
			Vec2 b(m_pos.x + draw_pos.right(), m_pos.y + draw_pos.bottom());
			ImColor col(borders.right.color.red, borders.right.color.green, borders.right.color.blue, borders.right.color.alpha);

			for (int x = 0; x < borders.right.width; x++)
			{
				m_draw_list.AddLine(a, b, col);
				--a.x;
				--b.x;
			}
		}

		if (borders.left.width != 0 && borders.left.style > litehtml::border_style_hidden)
		{
			Vec2 a(m_pos.x + draw_pos.left(), m_pos.y + draw_pos.top());
			Vec2 b(m_pos.x + draw_pos.left(), m_pos.y + draw_pos.bottom());
			ImColor col(borders.left.color.red, borders.left.color.green, borders.left.color.blue, borders.left.color.alpha);

			for (int x = 0; x < borders.left.width; x++)
			{
				m_draw_list.AddLine(a, b, col);
				++a.x;
				++b.x;
			}
		}
	}


	void set_caption(const litehtml::tchar_t* caption) override {}
	void set_base_url(const litehtml::tchar_t* base_url) override {

	}
	void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override {}
	void on_anchor_click(const litehtml::tchar_t* url, const litehtml::element::ptr& el) override {}
	void set_cursor(const litehtml::tchar_t* cursor) override {}
	void transform_text(litehtml::tstring& text, litehtml::text_transform tt) override {}
	void import_css(litehtml::tstring& text, const litehtml::tstring& url, litehtml::tstring& baseurl) override
	{
		//Array<u8> data(m_app.getWorldEditor()->getAllocator());
		//download(m_host, url.c_str(), &data);
		//text = (const char*)&data[0];
	}
	void set_clip(const litehtml::position& pos,
		const litehtml::border_radiuses& bdr_radius,
		bool valid_x,
		bool valid_y) override
	{
	}
	void del_clip() override {}
	void get_client_rect(litehtml::position& client) const override {
		client.height = 1024;
		client.width = 1024;
		client.x = (int)m_pos.x;
		client.y = (int)m_pos.y;
	}
	std::shared_ptr<litehtml::element> create_element(const litehtml::tchar_t* tag_name,
		const litehtml::string_map& attributes,
		const std::shared_ptr<litehtml::document>& doc) override
	{
		return nullptr;
	}


	void get_media_features(litehtml::media_features& media) const override {
		/* TODO */

		litehtml::position client;
		get_client_rect(client);

		media.type = litehtml::media_type_screen;
		media.width = client.width;
		media.height = client.height;
		media.color = 8;
		media.monochrome = 0;
		media.color_index = 256;
		media.resolution = 96;
		media.device_width = 1024;
		media.device_height = 1024;
	}


	void get_language(litehtml::tstring& language, litehtml::tstring& culture) const override {}


	struct Image
	{
		int w;
		int h;
		Texture* texture;
	};


	Engine& m_engine;
	Vec2 m_pos;
	DrawList m_draw_list;
	FontAtlas m_font_atlas;
	HashMap<u32, Image> m_images;
};


}
