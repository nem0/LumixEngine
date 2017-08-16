#include "html.h"
#include "el_para.h"
#include "document.h"

litehtml::el_para::el_para(const std::shared_ptr<litehtml::document>& doc) : litehtml::html_tag(doc)
{
}

litehtml::el_para::~el_para()
{

}

void litehtml::el_para::parse_attributes()
{
	const tchar_t* str = get_attr(_t("align"));
	if(str)
	{
		m_style.add_property(_t("text-align"), str, 0, false);
	}

	html_tag::parse_attributes();
}
