#include "html.h"
#include "el_body.h"
#include "document.h"

litehtml::el_body::el_body(const std::shared_ptr<litehtml::document>& doc) : litehtml::html_tag(doc)
{
}

litehtml::el_body::~el_body()
{

}

bool litehtml::el_body::is_body()  const
{
	return true;
}
