#include "ofbx.h"
#include "miniz.h"
#include <cassert>
#include <cmath>
#include <ctype.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>


namespace ofbx
{


struct Error
{
	Error() {}
	Error(const char* msg) { s_message = msg; }

	static const char* s_message;
};


const char* Error::s_message = "";


template <typename T> struct OptionalError
{
	OptionalError(Error error)
		: is_error(true)
	{
	}


	OptionalError(T _value)
		: value(_value)
		, is_error(false)
	{
	}


	T getValue() const
	{
#ifdef _DEBUG
		assert(error_checked);
#endif
		return value;
	}


	bool isError()
	{
#ifdef _DEBUG
		error_checked = true;
#endif
		return is_error;
	}


private:
	T value;
	bool is_error;
#ifdef _DEBUG
	bool error_checked = false;
#endif
};


#pragma pack(1)
struct Header
{
	u8 magic[21];
	u8 reserved[2];
	u32 version;
};
#pragma pack()


struct Cursor
{
	const u8* current;
	const u8* begin;
	const u8* end;
};


static void setTranslation(const Vec3& t, Matrix* mtx)
{
	mtx->m[12] = t.x;
	mtx->m[13] = t.y;
	mtx->m[14] = t.z;
}


static Vec3 operator-(const Vec3& v)
{
	return {-v.x, -v.y, -v.z};
}


static Matrix operator*(const Matrix& lhs, const Matrix& rhs)
{
	Matrix res;
	for (int j = 0; j < 4; ++j)
	{
		for (int i = 0; i < 4; ++i)
		{
			double tmp = 0;
			for (int k = 0; k < 4; ++k)
			{
				tmp += lhs.m[i + k * 4] * rhs.m[k + j * 4];
			}
			res.m[i + j * 4] = tmp;
		}
	}
	return res;
}


static Matrix makeIdentity()
{
	return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}


static Matrix rotationX(double angle)
{
	Matrix m = makeIdentity();
	double c = cos(angle);
	double s = sin(angle);

	m.m[5] = m.m[10] = c;
	m.m[9] = -s;
	m.m[6] = s;

	return m;
}


static Matrix rotationY(double angle)
{
	Matrix m = makeIdentity();
	double c = cos(angle);
	double s = sin(angle);

	m.m[0] = m.m[10] = c;
	m.m[8] = s;
	m.m[2] = -s;

	return m;
}


static Matrix rotationZ(double angle)
{
	Matrix m = makeIdentity();
	double c = cos(angle);
	double s = sin(angle);

	m.m[0] = m.m[5] = c;
	m.m[4] = -s;
	m.m[1] = s;

	return m;
}


static Matrix getRotationMatrix(const Vec3& euler, RotationOrder order)
{
	const double TO_RAD = 3.1415926535897932384626433832795028 / 180.0;
	Matrix rx = rotationX(euler.x * TO_RAD);
	Matrix ry = rotationY(euler.y * TO_RAD);
	Matrix rz = rotationZ(euler.z * TO_RAD);
	switch (order) {
		default:
		case RotationOrder::SPHERIC_XYZ:
			assert(false);
		case RotationOrder::EULER_XYZ:
			return rz * ry * rx;
		case RotationOrder::EULER_XZY:
			return ry * rz * rx;
		case RotationOrder::EULER_YXZ:
			return rz * rx * ry;
		case RotationOrder::EULER_YZX:
			return rx * rz * ry;
		case RotationOrder::EULER_ZXY:
			return ry * rx * rz;
		case RotationOrder::EULER_ZYX:
			return rx * ry * rz;
	}
}


static double fbxTimeToSeconds(i64 value)
{
	return double(value) / 46186158000L;
}


static i64 secondsToFbxTime(double value)
{
	return i64(value * 46186158000L);
}


static Vec3 operator*(const Vec3& v, float f)
{
	return {v.x * f, v.y * f, v.z * f};
}


static Vec3 operator+(const Vec3& a, const Vec3& b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}


template <int SIZE> static bool copyString(char (&destination)[SIZE], const char* source)
{
	const char* src = source;
	char* dest = destination;
	int length = SIZE;
	if (!src) return false;

	while (*src && length > 1)
	{
		*dest = *src;
		--length;
		++dest;
		++src;
	}
	*dest = 0;
	return *src == '\0';
}


u64 DataView::toU64() const
{
	if (is_binary)
	{
		assert(end - begin == sizeof(u64));
		return *(u64*)begin;
	}
	static_assert(sizeof(unsigned long long) >= sizeof(u64), "can't use strtoull");
	return strtoull((const char*)begin, nullptr, 10);
}


i64 DataView::toI64() const
{
	if (is_binary)
	{
		assert(end - begin == sizeof(i64));
		return *(i64*)begin;
	}
	static_assert(sizeof(long long) >= sizeof(i64), "can't use atoll");
	return atoll((const char*)begin);
}


int DataView::toInt() const
{
	if (is_binary)
	{
		assert(end - begin == sizeof(int));
		return *(int*)begin;
	}
	return atoi((const char*)begin);
}


u32 DataView::toU32() const
{
	if (is_binary)
	{
		assert(end - begin == sizeof(u32));
		return *(u32*)begin;
	}
	return (u32)atoll((const char*)begin);
}


double DataView::toDouble() const
{
	if (is_binary)
	{
		assert(end - begin == sizeof(double));
		return *(double*)begin;
	}
	return atof((const char*)begin);
}


float DataView::toFloat() const
{
	if (is_binary)
	{
		assert(end - begin == sizeof(float));
		return *(float*)begin;
	}
	return (float)atof((const char*)begin);
}


bool DataView::operator==(const char* rhs) const
{
	const char* c = rhs;
	const char* c2 = (const char*)begin;
	while (*c && c2 != (const char*)end)
	{
		if (*c != *c2) return 0;
		++c;
		++c2;
	}
	return c2 == (const char*)end && *c == '\0';
}


struct Property;
template <typename T> static bool parseArrayRaw(const Property& property, T* out, int max_size);
template <typename T> static bool parseBinaryArray(const Property& property, std::vector<T>* out);


struct Property : IElementProperty
{
	~Property() { delete next; }
	Type getType() const override { return (Type)type; }
	IElementProperty* getNext() const override { return next; }
	DataView getValue() const override { return value; }
	int getCount() const override
	{
		assert(type == ARRAY_DOUBLE || type == ARRAY_INT || type == ARRAY_FLOAT || type == ARRAY_LONG);
		if (value.is_binary)
		{
			return int(*(u32*)value.begin);
		}
		return count;
	}

	bool getValues(double* values, int max_size) const override { return parseArrayRaw(*this, values, max_size); }

	bool getValues(float* values, int max_size) const override { return parseArrayRaw(*this, values, max_size); }

	bool getValues(u64* values, int max_size) const override { return parseArrayRaw(*this, values, max_size); }
	
	bool getValues(i64* values, int max_size) const override { return parseArrayRaw(*this, values, max_size); }

	bool getValues(int* values, int max_size) const override { return parseArrayRaw(*this, values, max_size); }

	int count;
	u8 type;
	DataView value;
	Property* next = nullptr;
};


struct Element : IElement
{
	IElement* getFirstChild() const override { return child; }
	IElement* getSibling() const override { return sibling; }
	DataView getID() const override { return id; }
	IElementProperty* getFirstProperty() const override { return first_property; }
	IElementProperty* getProperty(int idx) const
	{
		IElementProperty* prop = first_property;
		for (int i = 0; i < idx; ++i)
		{
			if (prop == nullptr) return nullptr;
			prop = prop->getNext();
		}
		return prop;
	}

	DataView id;
	Element* child = nullptr;
	Element* sibling = nullptr;
	Property* first_property = nullptr;
};


static const Element* findChild(const Element& element, const char* id)
{
	Element* const* iter = &element.child;
	while (*iter)
	{
		if ((*iter)->id == id) return *iter;
		iter = &(*iter)->sibling;
	}
	return nullptr;
}


static IElement* resolveProperty(const Object& obj, const char* name)
{
	const Element* props = findChild((const Element&)obj.element, "Properties70");
	if (!props) return nullptr;

	Element* prop = props->child;
	while (prop)
	{
		if (prop->first_property && prop->first_property->value == name)
		{
			return prop;
		}
		prop = prop->sibling;
	}
	return nullptr;
}


static int resolveEnumProperty(const Object& object, const char* name, int default_value)
{
	Element* element = (Element*)resolveProperty(object, name);
	if (!element) return default_value;
	Property* x = (Property*)element->getProperty(4);
	if (!x) return default_value;

	return x->value.toInt();
}


static Vec3 resolveVec3Property(const Object& object, const char* name, const Vec3& default_value)
{
	Element* element = (Element*)resolveProperty(object, name);
	if (!element) return default_value;
	Property* x = (Property*)element->getProperty(4);
	if (!x || !x->next || !x->next->next) return default_value;

	return {x->value.toDouble(), x->next->value.toDouble(), x->next->next->value.toDouble()};
}


Object::Object(const Scene& _scene, const IElement& _element)
	: scene(_scene)
	, element(_element)
	, is_node(false)
	, node_attribute(nullptr)
{
	auto& e = (Element&)_element;
	if (e.first_property && e.first_property->next)
	{
		e.first_property->next->value.toString(name);
	}
	else
	{
		name[0] = '\0';
	}
}


static bool decompress(const u8* in, size_t in_size, u8* out, size_t out_size)
{
	mz_stream stream = {};
	mz_inflateInit(&stream);

	stream.avail_in = (int)in_size;
	stream.next_in = in;
	stream.avail_out = (int)out_size;
	stream.next_out = out;

	int status = mz_inflate(&stream, Z_SYNC_FLUSH);

	if (status != Z_STREAM_END) return false;

	return mz_inflateEnd(&stream) == Z_OK;
}


template <typename T> static OptionalError<T> read(Cursor* cursor)
{
	if (cursor->current + sizeof(T) > cursor->end) return Error("Reading past the end");
	T value = *(const T*)cursor->current;
	cursor->current += sizeof(T);
	return value;
}


static OptionalError<DataView> readShortString(Cursor* cursor)
{
	DataView value;
	OptionalError<u8> length = read<u8>(cursor);
	if (length.isError()) return Error();

	if (cursor->current + length.getValue() > cursor->end) return Error("Reading past the end");
	value.begin = cursor->current;
	cursor->current += length.getValue();

	value.end = cursor->current;

	return value;
}


static OptionalError<DataView> readLongString(Cursor* cursor)
{
	DataView value;
	OptionalError<u32> length = read<u32>(cursor);
	if (length.isError()) return Error();

	if (cursor->current + length.getValue() > cursor->end) return Error("Reading past the end");
	value.begin = cursor->current;
	cursor->current += length.getValue();

	value.end = cursor->current;

	return value;
}


static OptionalError<Property*> readProperty(Cursor* cursor)
{
	if (cursor->current == cursor->end) return Error("Reading past the end");

	std::unique_ptr<Property> prop(new Property());
	prop->next = nullptr;
	prop->type = *cursor->current;
	++cursor->current;
	prop->value.begin = cursor->current;

	switch (prop->type)
	{
		case 'S':
		{
			OptionalError<DataView> val = readLongString(cursor);
			if (val.isError()) return Error();
			prop->value = val.getValue();
			break;
		}
		case 'Y': cursor->current += 2; break;
		case 'C': cursor->current += 1; break;
		case 'I': cursor->current += 4; break;
		case 'F': cursor->current += 4; break;
		case 'D': cursor->current += 8; break;
		case 'L': cursor->current += 8; break;
		case 'R':
		{
			OptionalError<u32> len = read<u32>(cursor);
			if (len.isError()) return Error();
			if (cursor->current + len.getValue() > cursor->end) return Error("Reading past the end");
			cursor->current += len.getValue();
			break;
		}
		case 'b':
		case 'f':
		case 'd':
		case 'l':
		case 'i':
		{
			OptionalError<u32> length = read<u32>(cursor);
			OptionalError<u32> encoding = read<u32>(cursor);
			OptionalError<u32> comp_len = read<u32>(cursor);
			if (length.isError() | encoding.isError() | comp_len.isError()) return Error();
			if (cursor->current + comp_len.getValue() > cursor->end) return Error("Reading past the end");
			cursor->current += comp_len.getValue();
			break;
		}
		default: return Error("Unknown property type");
	}
	prop->value.end = cursor->current;
	return prop.release();
}


static void deleteElement(Element* el)
{
	if (!el) return;

	Element* iter = el;
	// do not use recursion to delete siblings to avoid stack overflow
	do
	{
		Element* next = iter->sibling;
		delete iter->first_property;
		deleteElement(iter->child);
		delete iter;
		iter = next;
	} while (iter);
}



static OptionalError<u64> readElementOffset(Cursor* cursor, u16 version)
{
	if (version >= 7500)
	{
		OptionalError<u64> tmp = read<u64>(cursor);
		if (tmp.isError()) return Error();
		return tmp.getValue();
	}

	OptionalError<u32> tmp = read<u32>(cursor);
	if (tmp.isError()) return Error();
	return tmp.getValue();
}


static OptionalError<Element*> readElement(Cursor* cursor, u32 version)
{
	OptionalError<u64> end_offset = readElementOffset(cursor, version);
	if (end_offset.isError()) return Error();
	if (end_offset.getValue() == 0) return nullptr;

	OptionalError<u64> prop_count = readElementOffset(cursor, version);
	OptionalError<u64> prop_length = readElementOffset(cursor, version);
	if (prop_count.isError() || prop_length.isError()) return Error();

	const char* sbeg = 0;
	const char* send = 0;
	OptionalError<DataView> id = readShortString(cursor);
	if (id.isError()) return Error();

	Element* element = new Element();
	element->first_property = nullptr;
	element->id = id.getValue();

	element->child = nullptr;
	element->sibling = nullptr;

	Property** prop_link = &element->first_property;
	for (u32 i = 0; i < prop_count.getValue(); ++i)
	{
		OptionalError<Property*> prop = readProperty(cursor);
		if (prop.isError())
		{
			deleteElement(element);
			return Error();
		}

		*prop_link = prop.getValue();
		prop_link = &(*prop_link)->next;
	}

	if (cursor->current - cursor->begin >= (ptrdiff_t)end_offset.getValue()) return element;

	int BLOCK_SENTINEL_LENGTH = version >= 7500 ? 25 : 13;

	Element** link = &element->child;
	while (cursor->current - cursor->begin < ((ptrdiff_t)end_offset.getValue() - BLOCK_SENTINEL_LENGTH))
	{
		OptionalError<Element*> child = readElement(cursor, version);
		if (child.isError())
		{
			deleteElement(element);
			return Error();
		}

		*link = child.getValue();
		link = &(*link)->sibling;
	}

	if (cursor->current + BLOCK_SENTINEL_LENGTH > cursor->end)
	{
		deleteElement(element); 
		return Error("Reading past the end");
	}

	cursor->current += BLOCK_SENTINEL_LENGTH;
	return element;
}


static bool isEndLine(const Cursor& cursor)
{
	return *cursor.current == '\n';
}


static void skipInsignificantWhitespaces(Cursor* cursor)
{
	while (cursor->current < cursor->end && isspace(*cursor->current) && *cursor->current != '\n')
	{
		++cursor->current;
	}
}


static void skipLine(Cursor* cursor)
{
	while (cursor->current < cursor->end && !isEndLine(*cursor))
	{
		++cursor->current;
	}
	if (cursor->current < cursor->end) ++cursor->current;
	skipInsignificantWhitespaces(cursor);
}


static void skipWhitespaces(Cursor* cursor)
{
	while (cursor->current < cursor->end && isspace(*cursor->current))
	{
		++cursor->current;
	}
	while (cursor->current < cursor->end && *cursor->current == ';') skipLine(cursor);
}


static bool isTextTokenChar(char c)
{
	return isalnum(c) || c == '_';
}


static DataView readTextToken(Cursor* cursor)
{
	DataView ret;
	ret.begin = cursor->current;
	while (cursor->current < cursor->end && isTextTokenChar(*cursor->current))
	{
		++cursor->current;
	}
	ret.end = cursor->current;
	return ret;
}


static OptionalError<Property*> readTextProperty(Cursor* cursor)
{
	std::unique_ptr<Property> prop(new Property());
	prop->value.is_binary = false;
	prop->next = nullptr;
	if (*cursor->current == '"')
	{
		prop->type = 'S';
		++cursor->current;
		prop->value.begin = cursor->current;
		while (cursor->current < cursor->end && *cursor->current != '"')
		{
			++cursor->current;
		}
		prop->value.end = cursor->current;
		if (cursor->current < cursor->end) ++cursor->current; // skip '"'
		return prop.release();
	}
	
	if (isdigit(*cursor->current) || *cursor->current == '-')
	{
		prop->type = 'L';
		prop->value.begin = cursor->current;
		if (*cursor->current == '-') ++cursor->current;
		while (cursor->current < cursor->end && isdigit(*cursor->current))
		{
			++cursor->current;
		}
		prop->value.end = cursor->current;

		if (cursor->current < cursor->end && *cursor->current == '.')
		{
			prop->type = 'D';
			++cursor->current;
			while (cursor->current < cursor->end && isdigit(*cursor->current))
			{
				++cursor->current;
			}
			if (cursor->current < cursor->end && (*cursor->current == 'e' || *cursor->current == 'E'))
			{
				// 10.5e-013
				++cursor->current;
				if (cursor->current < cursor->end && *cursor->current == '-') ++cursor->current;
				while (cursor->current < cursor->end && isdigit(*cursor->current)) ++cursor->current;
			}


			prop->value.end = cursor->current;
		}
		return prop.release();
	}
	
	if (*cursor->current == 'T' || *cursor->current == 'Y')
	{
		// WTF is this
		prop->type = *cursor->current;
		prop->value.begin = cursor->current;
		++cursor->current;
		prop->value.end = cursor->current;
		return prop.release();
	}

	if (*cursor->current == '*')
	{
		prop->type = 'l';
		++cursor->current;
		// Vertices: *10740 { a: 14.2760353088379,... }
		while (cursor->current < cursor->end && *cursor->current != ':')
		{
			++cursor->current;
		}
		if (cursor->current < cursor->end) ++cursor->current; // skip ':'
		skipInsignificantWhitespaces(cursor);
		prop->value.begin = cursor->current;
		prop->count = 0;
		bool is_any = false;
		while (cursor->current < cursor->end && *cursor->current != '}')
		{
			if (*cursor->current == ',')
			{
				if (is_any) ++prop->count;
				is_any = false;
			}
			else if (!isspace(*cursor->current) && *cursor->current != '\n') is_any = true;
			if (*cursor->current == '.') prop->type = 'd';
			++cursor->current;
		}
		if (is_any) ++prop->count;
		prop->value.end = cursor->current;
		if (cursor->current < cursor->end) ++cursor->current; // skip '}'
		return prop.release();
	}

	assert(false);
	return Error("TODO");
}


static OptionalError<Element*> readTextElement(Cursor* cursor)
{
	DataView id = readTextToken(cursor);
	if (cursor->current == cursor->end) return Error("Unexpected end of file");
	if(*cursor->current != ':') return Error("Unexpected end of file");
	++cursor->current;

	skipWhitespaces(cursor);
	if (cursor->current == cursor->end) return Error("Unexpected end of file");

	Element* element = new Element;
	element->id = id;

	Property** prop_link = &element->first_property;
	while (cursor->current < cursor->end && *cursor->current != '\n' && *cursor->current != '{')
	{
		OptionalError<Property*> prop = readTextProperty(cursor);
		if (prop.isError())
		{
			deleteElement(element);
			return Error();
		}
		if (cursor->current < cursor->end && *cursor->current == ',')
		{
			++cursor->current;
			skipWhitespaces(cursor);
		}
		skipInsignificantWhitespaces(cursor);

		*prop_link = prop.getValue();
		prop_link = &(*prop_link)->next;
	}
	
	Element** link = &element->child;
	if (*cursor->current == '{')
	{
		++cursor->current;
		skipWhitespaces(cursor);
		while (cursor->current < cursor->end && *cursor->current != '}')
		{
			OptionalError<Element*> child = readTextElement(cursor);
			if (child.isError())
			{
				deleteElement(element);
				return Error();
			}
			skipWhitespaces(cursor);

			*link = child.getValue();
			link = &(*link)->sibling;
		}
		if (cursor->current < cursor->end) ++cursor->current; // skip '}'
	}
	return element;
}


static OptionalError<Element*> tokenizeText(const u8* data, size_t size)
{
	Cursor cursor;
	cursor.begin = data;
	cursor.current = data;
	cursor.end = data + size;

	Element* root = new Element();
	root->first_property = nullptr;
	root->id.begin = nullptr;
	root->id.end = nullptr;
	root->child = nullptr;
	root->sibling = nullptr;

	Element** element = &root->child;
	while (cursor.current < cursor.end)
	{
		if (*cursor.current == ';' || *cursor.current == '\r' || *cursor.current == '\n')
		{
			skipLine(&cursor);
		}
		else
		{
			OptionalError<Element*> child = readTextElement(&cursor);
			if (child.isError())
			{
				deleteElement(root);
				return Error();
			}
			*element = child.getValue();
			if (!*element) return root;
			element = &(*element)->sibling;
		}
	}

	return root;
}


static OptionalError<Element*> tokenize(const u8* data, size_t size)
{
	Cursor cursor;
	cursor.begin = data;
	cursor.current = data;
	cursor.end = data + size;

	const Header* header = (const Header*)cursor.current;
	cursor.current += sizeof(*header);

	Element* root = new Element();
	root->first_property = nullptr;
	root->id.begin = nullptr;
	root->id.end = nullptr;
	root->child = nullptr;
	root->sibling = nullptr;

	Element** element = &root->child;
	for (;;)
	{
		OptionalError<Element*> child = readElement(&cursor, header->version);
		if (child.isError())
		{
			deleteElement(root);
			return Error();
		}
		*element = child.getValue();
		if (!*element) return root;
		element = &(*element)->sibling;
	}
}


static void parseTemplates(const Element& root)
{
	const Element* defs = findChild(root, "Definitions");
	if (!defs) return;

	std::unordered_map<std::string, Element*> templates;
	Element* def = defs->child;
	while (def)
	{
		if (def->id == "ObjectType")
		{
			Element* subdef = def->child;
			while (subdef)
			{
				if (subdef->id == "PropertyTemplate")
				{
					DataView prop1 = def->first_property->value;
					DataView prop2 = subdef->first_property->value;
					std::string key((const char*)prop1.begin, prop1.end - prop1.begin);
					key += std::string((const char*)prop1.begin, prop1.end - prop1.begin);
					templates[key] = subdef;
				}
				subdef = subdef->sibling;
			}
		}
		def = def->sibling;
	}
	// TODO
}


struct Scene;


Mesh::Mesh(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct MeshImpl : Mesh
{
	MeshImpl(const Scene& _scene, const IElement& _element)
		: Mesh(_scene, _element)
		, scene(_scene)
	{
		is_node = true;
	}


	Matrix getGeometricMatrix() const override
	{
		Vec3 translation = resolveVec3Property(*this, "GeometricTranslation", {0, 0, 0});
		Vec3 rotation = resolveVec3Property(*this, "GeometricRotation", {0, 0, 0});
		Vec3 scale = resolveVec3Property(*this, "GeometricScaling", {1, 1, 1});

		Matrix scale_mtx = makeIdentity();
		scale_mtx.m[0] = (float)scale.x;
		scale_mtx.m[5] = (float)scale.y;
		scale_mtx.m[10] = (float)scale.z;
		Matrix mtx = getRotationMatrix(rotation, RotationOrder::EULER_XYZ);
		setTranslation(translation, &mtx);

		return scale_mtx * mtx;
	}


	Type getType() const override { return Type::MESH; }


	const Geometry* getGeometry() const override { return geometry; }
	const Material* getMaterial(int index) const override { return materials[index]; }
	int getMaterialCount() const override { return (int)materials.size(); }


	const Geometry* geometry = nullptr;
	const Scene& scene;
	std::vector<const Material*> materials;
};


Material::Material(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct MaterialImpl : Material
{
	MaterialImpl(const Scene& _scene, const IElement& _element)
		: Material(_scene, _element)
	{
		for (const Texture*& tex : textures) tex = nullptr;
	}

	Type getType() const override { return Type::MATERIAL; }


	const Texture* getTexture(Texture::TextureType type) const override { return textures[type]; }
	Color getDiffuseColor() const override { return diffuse_color; }

	const Texture* textures[Texture::TextureType::COUNT];
	Color diffuse_color;
};


struct LimbNodeImpl : Object
{
	LimbNodeImpl(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
		is_node = true;
	}
	Type getType() const override { return Type::LIMB_NODE; }
};


struct NullImpl : Object
{
	NullImpl(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
		is_node = true;
	}
	Type getType() const override { return Type::NULL_NODE; }
};


NodeAttribute::NodeAttribute(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct NodeAttributeImpl : NodeAttribute
{
	NodeAttributeImpl(const Scene& _scene, const IElement& _element)
		: NodeAttribute(_scene, _element)
	{
	}
	Type getType() const override { return Type::NODE_ATTRIBUTE; }
	DataView getAttributeType() const override { return attribute_type; }


	DataView attribute_type;
};


Geometry::Geometry(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct GeometryImpl : Geometry
{
	enum VertexDataMapping
	{
		BY_POLYGON_VERTEX,
		BY_POLYGON,
		BY_VERTEX
	};

	struct NewVertex
	{
		~NewVertex() { delete next; }

		int index = -1;
		NewVertex* next = nullptr;
	};

	std::vector<Vec3> vertices;
	std::vector<Vec3> normals;
	std::vector<Vec2> uvs[s_uvs_max];
	std::vector<Vec4> colors;
	std::vector<Vec3> tangents;
	std::vector<int> materials;

	const Skin* skin = nullptr;

	std::vector<int> to_old_vertices;
	std::vector<NewVertex> to_new_vertices;

	GeometryImpl(const Scene& _scene, const IElement& _element)
		: Geometry(_scene, _element)
	{
	}


	Type getType() const override { return Type::GEOMETRY; }
	int getVertexCount() const override { return (int)vertices.size(); }
	const Vec3* getVertices() const override { return &vertices[0]; }
	const Vec3* getNormals() const override { return normals.empty() ? nullptr : &normals[0]; }
	const Vec2* getUVs(int index = 0) const override { return index < 0 || index >= s_uvs_max || uvs[index].empty() ? nullptr : &uvs[index][0]; }
	const Vec4* getColors() const override { return colors.empty() ? nullptr : &colors[0]; }
	const Vec3* getTangents() const override { return tangents.empty() ? nullptr : &tangents[0]; }
	const Skin* getSkin() const override { return skin; }
	const int* getMaterials() const override { return materials.empty() ? nullptr : &materials[0]; }


	void triangulate(const std::vector<int>& old_indices, std::vector<int>* indices, std::vector<int>* to_old)
	{
		assert(indices);
		assert(to_old);

		auto getIdx = [&old_indices](int i) -> int {
			int idx = old_indices[i];
			return idx < 0 ? -idx - 1 : idx;
		};

		int in_polygon_idx = 0;
		for (int i = 0; i < old_indices.size(); ++i)
		{
			int idx = getIdx(i);
			if (in_polygon_idx <= 2)
			{
				indices->push_back(idx);
				to_old->push_back(i);
			}
			else
			{
				indices->push_back(old_indices[i - in_polygon_idx]);
				to_old->push_back(i - in_polygon_idx);
				indices->push_back(old_indices[i - 1]);
				to_old->push_back(i - 1);
				indices->push_back(idx);
				to_old->push_back(i);
			}
			++in_polygon_idx;
			if (old_indices[i] < 0)
			{
				in_polygon_idx = 0;
			}
		}
	}
};


Cluster::Cluster(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct ClusterImpl : Cluster
{
	ClusterImpl(const Scene& _scene, const IElement& _element)
		: Cluster(_scene, _element)
	{
	}

	const int* getIndices() const override { return &indices[0]; }
	int getIndicesCount() const override { return (int)indices.size(); }
	const double* getWeights() const override { return &weights[0]; }
	int getWeightsCount() const override { return (int)weights.size(); }
	Matrix getTransformMatrix() const override { return transform_matrix; }
	Matrix getTransformLinkMatrix() const override { return transform_link_matrix; }
	Object* getLink() const override { return link; }


	bool postprocess()
	{
		assert(skin);

		GeometryImpl* geom = (GeometryImpl*)skin->resolveObjectLinkReverse(Object::Type::GEOMETRY);
		if (!geom) return false;

		std::vector<int> old_indices;
		const Element* indexes = findChild((const Element&)element, "Indexes");
		if (indexes && indexes->first_property)
		{
			if (!parseBinaryArray(*indexes->first_property, &old_indices)) return false;
		}

		std::vector<double> old_weights;
		const Element* weights_el = findChild((const Element&)element, "Weights");
		if (weights_el && weights_el->first_property)
		{
			if (!parseBinaryArray(*weights_el->first_property, &old_weights)) return false;
		}

		if (old_indices.size() != old_weights.size()) return false;

		indices.reserve(old_indices.size());
		weights.reserve(old_indices.size());
		int* ir = old_indices.empty() ? nullptr : &old_indices[0];
		double* wr = old_weights.empty() ? nullptr : &old_weights[0];
		for (int i = 0, c = (int)old_indices.size(); i < c; ++i)
		{
			int old_idx = ir[i];
			double w = wr[i];
			GeometryImpl::NewVertex* n = &geom->to_new_vertices[old_idx];
			if (n->index == -1) continue; // skip vertices which aren't indexed.
			while (n)
			{
				indices.push_back(n->index);
				weights.push_back(w);
				n = n->next;
			}
		}

		return true;
	}


	Object* link = nullptr;
	Skin* skin = nullptr;
	std::vector<int> indices;
	std::vector<double> weights;
	Matrix transform_matrix;
	Matrix transform_link_matrix;
	Type getType() const override { return Type::CLUSTER; }
};


AnimationStack::AnimationStack(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


AnimationLayer::AnimationLayer(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


AnimationCurve::AnimationCurve(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


AnimationCurveNode::AnimationCurveNode(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct AnimationStackImpl : AnimationStack
{
	AnimationStackImpl(const Scene& _scene, const IElement& _element)
		: AnimationStack(_scene, _element)
	{
	}


	const AnimationLayer* getLayer(int index) const override
	{
		return resolveObjectLink<AnimationLayer>(index);
	}


	Type getType() const override { return Type::ANIMATION_STACK; }
};


struct AnimationCurveImpl : AnimationCurve
{
	AnimationCurveImpl(const Scene& _scene, const IElement& _element)
		: AnimationCurve(_scene, _element)
	{
	}

	int getKeyCount() const override { return (int)times.size(); }
	const i64* getKeyTime() const override { return &times[0]; }
	const float* getKeyValue() const override { return &values[0]; }

	std::vector<i64> times;
	std::vector<float> values;
	Type getType() const override { return Type::ANIMATION_CURVE; }
};


Skin::Skin(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct SkinImpl : Skin
{
	SkinImpl(const Scene& _scene, const IElement& _element)
		: Skin(_scene, _element)
	{
	}

	int getClusterCount() const override { return (int)clusters.size(); }
	const Cluster* getCluster(int idx) const override { return clusters[idx]; }

	Type getType() const override { return Type::SKIN; }

	std::vector<Cluster*> clusters;
};


Texture::Texture(const Scene& _scene, const IElement& _element)
	: Object(_scene, _element)
{
}


struct TextureImpl : Texture
{
	TextureImpl(const Scene& _scene, const IElement& _element)
		: Texture(_scene, _element)
	{
	}

	DataView getRelativeFileName() const override { return relative_filename; }
	DataView getFileName() const override { return filename; }

	DataView filename;
	DataView relative_filename;
	Type getType() const override { return Type::TEXTURE; }
};


struct Root : Object
{
	Root(const Scene& _scene, const IElement& _element)
		: Object(_scene, _element)
	{
		copyString(name, "RootNode");
		is_node = true;
	}
	Type getType() const override { return Type::ROOT; }
};


struct Scene : IScene
{
	struct Connection
	{
		enum Type
		{
			OBJECT_OBJECT,
			OBJECT_PROPERTY
		};

		Type type;
		u64 from;
		u64 to;
		DataView property;
	};

	struct ObjectPair
	{
		const Element* element;
		Object* object;
	};


	int getAnimationStackCount() const override { return (int)m_animation_stacks.size(); }
	int getMeshCount() const override { return (int)m_meshes.size(); }
	float getSceneFrameRate() const override { return m_scene_frame_rate; }
	const GlobalSettings* getGlobalSettings() const override { return &m_settings; }

	const Object* const* getAllObjects() const override { return m_all_objects.empty() ? nullptr : &m_all_objects[0]; }


	int getAllObjectCount() const override { return (int)m_all_objects.size(); }


	const AnimationStack* getAnimationStack(int index) const override
	{
		assert(index >= 0);
		assert(index < m_animation_stacks.size());
		return m_animation_stacks[index];
	}


	const Mesh* getMesh(int index) const override
	{
		assert(index >= 0);
		assert(index < m_meshes.size());
		return m_meshes[index];
	}


	const TakeInfo* getTakeInfo(const char* name) const override
	{
		for (const TakeInfo& info : m_take_infos)
		{
			if (info.name == name) return &info;
		}
		return nullptr;
	}


	const IElement* getRootElement() const override { return m_root_element; }
	const Object* getRoot() const override { return m_root; }


	void destroy() override { delete this; }


	~Scene()
	{
		for (auto iter : m_object_map)
		{
			delete iter.second.object;
		}
		
		deleteElement(m_root_element);
	}


	Element* m_root_element = nullptr;
	Root* m_root = nullptr;
	float m_scene_frame_rate = -1;
	GlobalSettings m_settings;
	std::unordered_map<u64, ObjectPair> m_object_map;
	std::vector<Object*> m_all_objects;
	std::vector<Mesh*> m_meshes;
	std::vector<AnimationStack*> m_animation_stacks;
	std::vector<Connection> m_connections;
	std::vector<u8> m_data;
	std::vector<TakeInfo> m_take_infos;
};


struct AnimationCurveNodeImpl : AnimationCurveNode
{
	AnimationCurveNodeImpl(const Scene& _scene, const IElement& _element)
		: AnimationCurveNode(_scene, _element)
	{
	}


	const Object* getBone() const override
	{
		return bone;
	}


	Vec3 getNodeLocalTransform(double time) const override
	{
		i64 fbx_time = secondsToFbxTime(time);

		auto getCoord = [](const Curve& curve, i64 fbx_time) {
			if (!curve.curve) return 0.0f;

			const i64* times = curve.curve->getKeyTime();
			const float* values = curve.curve->getKeyValue();
			int count = curve.curve->getKeyCount();

			if (fbx_time < times[0]) fbx_time = times[0];
			if (fbx_time > times[count - 1]) fbx_time = times[count - 1];
			for (int i = 1; i < count; ++i)
			{
				if (times[i] >= fbx_time)
				{
					float t = float(double(fbx_time - times[i - 1]) / double(times[i] - times[i - 1]));
					return values[i - 1] * (1 - t) + values[i] * t;
				}
			}
			return values[0];
		};

		return {getCoord(curves[0], fbx_time), getCoord(curves[1], fbx_time), getCoord(curves[2], fbx_time)};
	}


	struct Curve
	{
		const AnimationCurve* curve = nullptr;
		const Scene::Connection* connection = nullptr;
	};


	Curve curves[3];
	Object* bone = nullptr;
	DataView bone_link_property;
	Type getType() const override { return Type::ANIMATION_CURVE_NODE; }
	enum Mode
	{
		TRANSLATION,
		ROTATION,
		SCALE
	} mode = TRANSLATION;
};


struct AnimationLayerImpl : AnimationLayer
{
	AnimationLayerImpl(const Scene& _scene, const IElement& _element)
		: AnimationLayer(_scene, _element)
	{
	}


	Type getType() const override { return Type::ANIMATION_LAYER; }


	const AnimationCurveNode* getCurveNode(int index) const override
	{
		if (index >= curve_nodes.size() || index < 0) return nullptr;
		return curve_nodes[index];
	}


	const AnimationCurveNode* getCurveNode(const Object& bone, const char* prop) const override
	{
		for (const AnimationCurveNodeImpl* node : curve_nodes)
		{
			if (node->bone_link_property == prop && node->bone == &bone) return node;
		}
		return nullptr;
	}


	std::vector<AnimationCurveNodeImpl*> curve_nodes;
};


struct OptionalError<Object*> parseTexture(const Scene& scene, const Element& element)
{
	TextureImpl* texture = new TextureImpl(scene, element);
	const Element* texture_filename = findChild(element, "FileName");
	if (texture_filename && texture_filename->first_property)
	{
		texture->filename = texture_filename->first_property->value;
	}
	const Element* texture_relative_filename = findChild(element, "RelativeFilename");
	if (texture_relative_filename && texture_relative_filename->first_property)
	{
		texture->relative_filename = texture_relative_filename->first_property->value;
	}
	return texture;
}


template <typename T> static OptionalError<Object*> parse(const Scene& scene, const Element& element)
{
	T* obj = new T(scene, element);
	return obj;
}


static OptionalError<Object*> parseCluster(const Scene& scene, const Element& element)
{
	std::unique_ptr<ClusterImpl> obj(new ClusterImpl(scene, element));

	const Element* transform_link = findChild(element, "TransformLink");
	if (transform_link && transform_link->first_property)
	{
		if (!parseArrayRaw(
				*transform_link->first_property, &obj->transform_link_matrix, sizeof(obj->transform_link_matrix)))
		{
			return Error("Failed to parse TransformLink");
		}
	}
	const Element* transform = findChild(element, "Transform");
	if (transform && transform->first_property)
	{
		if (!parseArrayRaw(*transform->first_property, &obj->transform_matrix, sizeof(obj->transform_matrix)))
		{
			return Error("Failed to parse Transform");

		}
	}

	return obj.release();
}


static OptionalError<Object*> parseNodeAttribute(const Scene& scene, const Element& element)
{
	NodeAttributeImpl* obj = new NodeAttributeImpl(scene, element);
	const Element* type_flags = findChild(element, "TypeFlags");
	if (type_flags && type_flags->first_property)
	{
		obj->attribute_type = type_flags->first_property->value;
	}
	return obj;
}


static OptionalError<Object*> parseLimbNode(const Scene& scene, const Element& element)
{
	if (!element.first_property
		|| !element.first_property->next
		|| !element.first_property->next->next
		|| element.first_property->next->next->value != "LimbNode")
	{
		return Error("Invalid limb node");
	}

	LimbNodeImpl* obj = new LimbNodeImpl(scene, element);
	return obj;
}


static OptionalError<Object*> parseMesh(const Scene& scene, const Element& element)
{
	if (!element.first_property
		|| !element.first_property->next
		|| !element.first_property->next->next
		|| element.first_property->next->next->value != "Mesh")
	{
		return Error("Invalid mesh");
	}

	return new MeshImpl(scene, element);
}


static OptionalError<Object*> parseMaterial(const Scene& scene, const Element& element)
{
	MaterialImpl* material = new MaterialImpl(scene, element);
	const Element* prop = findChild(element, "Properties70");
	material->diffuse_color = { 1, 1, 1 };
	if (prop) prop = prop->child;
	while (prop)
	{
		if (prop->id == "P" && prop->first_property)
		{
			if (prop->first_property->value == "DiffuseColor")
			{
				material->diffuse_color.r = (float)prop->getProperty(4)->getValue().toDouble();
				material->diffuse_color.g = (float)prop->getProperty(5)->getValue().toDouble();
				material->diffuse_color.b = (float)prop->getProperty(6)->getValue().toDouble();
			}
		}
		prop = prop->sibling;
	}
	return material;
}


template<typename T> static bool parseTextArrayRaw(const Property& property, T* out, int max_size);

template <typename T> static bool parseArrayRaw(const Property& property, T* out, int max_size)
{
	if (property.value.is_binary)
	{
		assert(out);

		int elem_size = 1;
		switch (property.type)
		{
			case 'l': elem_size = 8; break;
			case 'd': elem_size = 8; break;
			case 'f': elem_size = 4; break;
			case 'i': elem_size = 4; break;
			default: return false;
		}

		const u8* data = property.value.begin + sizeof(u32) * 3;
		if (data > property.value.end) return false;

		u32 count = property.getCount();
		u32 enc = *(const u32*)(property.value.begin + 4);
		u32 len = *(const u32*)(property.value.begin + 8);

		if (enc == 0)
		{
			if ((int)len > max_size) return false;
			if (data + len > property.value.end) return false;
			memcpy(out, data, len);
			return true;
		}
		else if (enc == 1)
		{
			if (int(elem_size * count) > max_size) return false;
			return decompress(data, len, (u8*)out, elem_size * count);
		}

		return false;
	}

	return parseTextArrayRaw(property, out, max_size);
}


template <typename T> const char* fromString(const char* str, const char* end, T* val);
template <> const char* fromString<int>(const char* str, const char* end, int* val)
{
	*val = atoi(str);
	const char* iter = str;
	while (iter < end && *iter != ',') ++iter;
	if (iter < end) ++iter; // skip ','
	return (const char*)iter;
}


template <> const char* fromString<u64>(const char* str, const char* end, u64* val)
{
	*val = strtoull(str, nullptr, 10);
	const char* iter = str;
	while (iter < end && *iter != ',') ++iter;
	if (iter < end) ++iter; // skip ','
	return (const char*)iter;
}


template <> const char* fromString<i64>(const char* str, const char* end, i64* val)
{
	*val = atoll(str);
	const char* iter = str;
	while (iter < end && *iter != ',') ++iter;
	if (iter < end) ++iter; // skip ','
	return (const char*)iter;
}


template <> const char* fromString<double>(const char* str, const char* end, double* val)
{
	*val = atof(str);
	const char* iter = str;
	while (iter < end && *iter != ',') ++iter;
	if (iter < end) ++iter; // skip ','
	return (const char*)iter;
}


template <> const char* fromString<float>(const char* str, const char* end, float* val)
{
	*val = (float)atof(str);
	const char* iter = str;
	while (iter < end && *iter != ',') ++iter;
	if (iter < end) ++iter; // skip ','
	return (const char*)iter;
}


const char* fromString(const char* str, const char* end, double* val, int count)
{
	const char* iter = str;
	for (int i = 0; i < count; ++i)
	{
		*val = atof(iter);
		++val;
		while (iter < end && *iter != ',') ++iter;
		if (iter < end) ++iter; // skip ','

		if (iter == end) return iter;

	}
	return (const char*)iter;
}


template <> const char* fromString<Vec2>(const char* str, const char* end, Vec2* val)
{
	return fromString(str, end, &val->x, 2);
}


template <> const char* fromString<Vec3>(const char* str, const char* end, Vec3* val)
{
	return fromString(str, end, &val->x, 3);
}


template <> const char* fromString<Vec4>(const char* str, const char* end, Vec4* val)
{
	return fromString(str, end, &val->x, 4);
}


template <> const char* fromString<Matrix>(const char* str, const char* end, Matrix* val)
{
	return fromString(str, end, &val->m[0], 16);
}


template<typename T> static void parseTextArray(const Property& property, std::vector<T>* out)
{
	const u8* iter = property.value.begin;
	for(int i = 0; i < property.count; ++i)
	{
		T val;
		iter = (const u8*)fromString<T>((const char*)iter, (const char*)property.value.end, &val);
		out->push_back(val);
	}
}


template<typename T> static bool parseTextArrayRaw(const Property& property, T* out_raw, int max_size)
{
	const u8* iter = property.value.begin;
	
	T* out = out_raw;
	while (iter < property.value.end)
	{
		iter = (const u8*)fromString<T>((const char*)iter, (const char*)property.value.end, out);
		++out;
		if (out - out_raw == max_size / sizeof(T)) return true;
	}
	return out - out_raw == max_size / sizeof(T);
}


template <typename T> static bool parseBinaryArray(const Property& property, std::vector<T>* out)
{
	assert(out);
	if (property.value.is_binary)
	{
		u32 count = property.getCount();
		int elem_size = 1;
		switch (property.type)
		{
			case 'd': elem_size = 8; break;
			case 'f': elem_size = 4; break;
			case 'i': elem_size = 4; break;
			default: return false;
		}
		int elem_count = sizeof(T) / elem_size;
		out->resize(count / elem_count);

		if (count == 0) return true;
		return parseArrayRaw(property, &(*out)[0], int(sizeof((*out)[0]) * out->size()));
	}
	else
	{
		parseTextArray(property, out);
		return true;
	}
}


template <typename T> static bool parseDoubleVecData(Property& property, std::vector<T>* out_vec)
{
	assert(out_vec);
	if (!property.value.is_binary)
	{
		parseTextArray(property, out_vec);
		return true;
	}

	if (property.type == 'd')
	{
		return parseBinaryArray(property, out_vec);
	}

	assert(property.type == 'f');
	assert(sizeof((*out_vec)[0].x) == sizeof(double));
	std::vector<float> tmp;
	if (!parseBinaryArray(property, &tmp)) return false;
	int elem_count = sizeof((*out_vec)[0]) / sizeof((*out_vec)[0].x);
	out_vec->resize(tmp.size() / elem_count);
	double* out = &(*out_vec)[0].x;
	for (int i = 0, c = (int)tmp.size(); i < c; ++i)
	{
		out[i] = tmp[i];
	}
	return true;
}


template <typename T>
static bool parseVertexData(const Element& element,
	const char* name,
	const char* index_name,
	std::vector<T>* out,
	std::vector<int>* out_indices,
	GeometryImpl::VertexDataMapping* mapping)
{
	assert(out);
	assert(mapping);
	const Element* data_element = findChild(element, name);
	if (!data_element || !data_element->first_property) 	return false;

	const Element* mapping_element = findChild(element, "MappingInformationType");
	const Element* reference_element = findChild(element, "ReferenceInformationType");

	if (mapping_element && mapping_element->first_property)
	{
		if (mapping_element->first_property->value == "ByPolygonVertex")
		{
			*mapping = GeometryImpl::BY_POLYGON_VERTEX;
		}
		else if (mapping_element->first_property->value == "ByPolygon")
		{
			*mapping = GeometryImpl::BY_POLYGON;
		}
		else if (mapping_element->first_property->value == "ByVertice" ||
					mapping_element->first_property->value == "ByVertex")
		{
			*mapping = GeometryImpl::BY_VERTEX;
		}
		else
		{
			return false;
		}
	}
	if (reference_element && reference_element->first_property)
	{
		if (reference_element->first_property->value == "IndexToDirect")
		{
			const Element* indices_element = findChild(element, index_name);
			if (indices_element && indices_element->first_property)
			{
				if (!parseBinaryArray(*indices_element->first_property, out_indices)) return false;
			}
		}
		else if (reference_element->first_property->value != "Direct")
		{
			return false;
		}
	}
	return parseDoubleVecData(*data_element->first_property, out);
}


template <typename T>
static void splat(std::vector<T>* out,
	GeometryImpl::VertexDataMapping mapping,
	const std::vector<T>& data,
	const std::vector<int>& indices,
	const std::vector<int>& original_indices)
{
	assert(out);
	assert(!data.empty());

	if (mapping == GeometryImpl::BY_POLYGON_VERTEX)
	{
		if (indices.empty())
		{
			out->resize(data.size());
			memcpy(&(*out)[0], &data[0], sizeof(data[0]) * data.size());
		}
		else
		{
			out->resize(indices.size());
			int data_size = (int)data.size();
			for (int i = 0, c = (int)indices.size(); i < c; ++i)
			{
				if(indices[i] < data_size) (*out)[i] = data[indices[i]];
				else (*out)[i] = T();
			}
		}
	}
	else if (mapping == GeometryImpl::BY_VERTEX)
	{
		//  v0  v1 ...
		// uv0 uv1 ...
		assert(indices.empty());

		out->resize(original_indices.size());

		int data_size = (int)data.size();
		for (int i = 0, c = (int)original_indices.size(); i < c; ++i)
		{
			int idx = original_indices[i];
			if (idx < 0) idx = -idx - 1;
			if(idx < data_size) (*out)[i] = data[idx];
			else (*out)[i] = T();
		}
	}
	else
	{
		assert(false);
	}
}


template <typename T> static void remap(std::vector<T>* out, const std::vector<int>& map)
{
	if (out->empty()) return;

	std::vector<T> old;
	old.swap(*out);
	int old_size = (int)old.size();
	for (int i = 0, c = (int)map.size(); i < c; ++i)
	{
		if(map[i] < old_size) out->push_back(old[map[i]]);
		else out->push_back(T());
	}
}


static OptionalError<Object*> parseAnimationCurve(const Scene& scene, const Element& element)
{
	std::unique_ptr<AnimationCurveImpl> curve(new AnimationCurveImpl(scene, element));

	const Element* times = findChild(element, "KeyTime");
	const Element* values = findChild(element, "KeyValueFloat");

	if (times && times->first_property)
	{
		curve->times.resize(times->first_property->getCount());
		if (!times->first_property->getValues(&curve->times[0], (int)curve->times.size() * sizeof(curve->times[0])))
		{
			return Error("Invalid animation curve");
		}
	}

	if (values && values->first_property)
	{
		curve->values.resize(values->first_property->getCount());
		if (!values->first_property->getValues(&curve->values[0], (int)curve->values.size() * sizeof(curve->values[0])))
		{
			return Error("Invalid animation curve");
		}
	}

	if (curve->times.size() != curve->values.size()) return Error("Invalid animation curve");

	return curve.release();
}


static int getTriCountFromPoly(const std::vector<int>& indices, int* idx)
{
	int count = 1;
	while (indices[*idx + 1 + count] >= 0)
	{
		++count;
	}

	*idx = *idx + 2 + count;
	return count;
}


static void add(GeometryImpl::NewVertex& vtx, int index)
{
	if (vtx.index == -1)
	{
		vtx.index = index;
	}
	else if (vtx.next)
	{
		add(*vtx.next, index);
	}
	else
	{
		vtx.next = new GeometryImpl::NewVertex;
		vtx.next->index = index;
	}
}


static OptionalError<Object*> parseGeometry(const Scene& scene, const Element& element)
{
	assert(element.first_property);

	const Element* vertices_element = findChild(element, "Vertices");
	if (!vertices_element || !vertices_element->first_property) {
		return new GeometryImpl(scene, element);
	}

	const Element* polys_element = findChild(element, "PolygonVertexIndex");
	if (!polys_element || !polys_element->first_property) return Error("Indices missing");

	std::unique_ptr<GeometryImpl> geom(new GeometryImpl(scene, element));

	std::vector<Vec3> vertices;
	if (!parseDoubleVecData(*vertices_element->first_property, &vertices)) return Error("Failed to parse vertices");
	std::vector<int> original_indices;
	if (!parseBinaryArray(*polys_element->first_property, &original_indices)) return Error("Failed to parse indices");

	std::vector<int> to_old_indices;
	geom->triangulate(original_indices, &geom->to_old_vertices, &to_old_indices);
	geom->vertices.resize(geom->to_old_vertices.size());

	for (int i = 0, c = (int)geom->to_old_vertices.size(); i < c; ++i)
	{
		geom->vertices[i] = vertices[geom->to_old_vertices[i]];
	}

	geom->to_new_vertices.resize(vertices.size()); // some vertices can be unused, so this isn't necessarily the same size as to_old_vertices.
	const int* to_old_vertices = geom->to_old_vertices.empty() ? nullptr : &geom->to_old_vertices[0];
	for (int i = 0, c = (int)geom->to_old_vertices.size(); i < c; ++i)
	{
		int old = to_old_vertices[i];
		add(geom->to_new_vertices[old], i);
	}

	const Element* layer_material_element = findChild(element, "LayerElementMaterial");
	if (layer_material_element)
	{
		const Element* mapping_element = findChild(*layer_material_element, "MappingInformationType");
		const Element* reference_element = findChild(*layer_material_element, "ReferenceInformationType");

		std::vector<int> tmp;

		if (!mapping_element || !reference_element) return Error("Invalid LayerElementMaterial");

		if (mapping_element->first_property->value == "ByPolygon" &&
			reference_element->first_property->value == "IndexToDirect")
		{
			geom->materials.reserve(geom->vertices.size() / 3);
			for (int& i : geom->materials) i = -1;

			const Element* indices_element = findChild(*layer_material_element, "Materials");
			if (!indices_element || !indices_element->first_property) return Error("Invalid LayerElementMaterial");

			if (!parseBinaryArray(*indices_element->first_property, &tmp)) return Error("Failed to parse material indices");

			int tmp_i = 0;
			for (int poly = 0, c = (int)tmp.size(); poly < c; ++poly)
			{
				int tri_count = getTriCountFromPoly(original_indices, &tmp_i);
				for (int i = 0; i < tri_count; ++i)
				{
					geom->materials.push_back(tmp[poly]);
				}
			}
		}
		else
		{
			if (mapping_element->first_property->value != "AllSame") return Error("Mapping not supported");
		}
	}

	const Element* layer_uv_element = findChild(element, "LayerElementUV");
    while (layer_uv_element)
    {
        const int uv_index = layer_uv_element->first_property ? layer_uv_element->first_property->getValue().toInt() : 0;
        if (uv_index >= 0 && uv_index < Geometry::s_uvs_max)
        {
            std::vector<Vec2>& uvs = geom->uvs[uv_index];

            std::vector<Vec2> tmp;
            std::vector<int> tmp_indices;
            GeometryImpl::VertexDataMapping mapping;
            if (!parseVertexData(*layer_uv_element, "UV", "UVIndex", &tmp, &tmp_indices, &mapping)) return Error("Invalid UVs");
            if (!tmp.empty())
            {
                uvs.resize(tmp_indices.empty() ? tmp.size() : tmp_indices.size());
                splat(&uvs, mapping, tmp, tmp_indices, original_indices);
                remap(&uvs, to_old_indices);
            }
        }

        do
        {
            layer_uv_element = layer_uv_element->sibling;
        } while (layer_uv_element && layer_uv_element->id != "LayerElementUV");
    }

	const Element* layer_tangent_element = findChild(element, "LayerElementTangents");
	if (layer_tangent_element)
	{
		std::vector<Vec3> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		if (findChild(*layer_tangent_element, "Tangents"))
		{
			if (!parseVertexData(*layer_tangent_element, "Tangents", "TangentsIndex", &tmp, &tmp_indices, &mapping)) return Error("Invalid tangets");
		}
		else
		{
			if (!parseVertexData(*layer_tangent_element, "Tangent", "TangentIndex", &tmp, &tmp_indices, &mapping))  return Error("Invalid tangets");
		}
		if (!tmp.empty())
		{
			splat(&geom->tangents, mapping, tmp, tmp_indices, original_indices);
			remap(&geom->tangents, to_old_indices);
		}
	}

	const Element* layer_color_element = findChild(element, "LayerElementColor");
	if (layer_color_element)
	{
		std::vector<Vec4> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		if (!parseVertexData(*layer_color_element, "Colors", "ColorIndex", &tmp, &tmp_indices, &mapping)) return Error("Invalid colors");
		if (!tmp.empty())
		{
			splat(&geom->colors, mapping, tmp, tmp_indices, original_indices);
			remap(&geom->colors, to_old_indices);
		}
	}

	const Element* layer_normal_element = findChild(element, "LayerElementNormal");
	if (layer_normal_element)
	{
		std::vector<Vec3> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		if (!parseVertexData(*layer_normal_element, "Normals", "NormalsIndex", &tmp, &tmp_indices, &mapping)) return Error("Invalid normals");
		if (!tmp.empty())
		{
			splat(&geom->normals, mapping, tmp, tmp_indices, original_indices);
			remap(&geom->normals, to_old_indices);
		}
	}

	return geom.release();
}


static bool isString(const Property* prop)
{
	if (!prop) return false;
	return prop->getType() == Property::STRING;
}


static bool isLong(const Property* prop)
{
	if (!prop) return false;
	return prop->getType() == Property::LONG;
}


static bool parseConnections(const Element& root, Scene* scene)
{
	assert(scene);

	const Element* connections = findChild(root, "Connections");
	if (!connections) return true;

	const Element* connection = connections->child;
	while (connection)
	{
		if (!isString(connection->first_property)
			|| !isLong(connection->first_property->next)
			|| !isLong(connection->first_property->next->next))
		{
			Error::s_message = "Invalid connection";
			return false;
		}

		Scene::Connection c;
		c.from = connection->first_property->next->value.toU64();
		c.to = connection->first_property->next->next->value.toU64();
		if (connection->first_property->value == "OO")
		{
			c.type = Scene::Connection::OBJECT_OBJECT;
		}
		else if (connection->first_property->value == "OP")
		{
			c.type = Scene::Connection::OBJECT_PROPERTY;
			if (!connection->first_property->next->next->next)
			{
				Error::s_message = "Invalid connection";
				return false;
			}
			c.property = connection->first_property->next->next->next->value;
		}
		else
		{
			assert(false);
			Error::s_message = "Not supported";
			return false;
		}
		scene->m_connections.push_back(c);

		connection = connection->sibling;
	}
	return true;
}


static bool parseTakes(Scene* scene)
{
	const Element* takes = findChild((const Element&)*scene->getRootElement(), "Takes");
	if (!takes) return true;

	const Element* object = takes->child;
	while (object)
	{
		if (object->id == "Take")
		{
			if (!isString(object->first_property))
			{
				Error::s_message = "Invalid name in take";
				return false;
			}

			TakeInfo take;
			take.name = object->first_property->value;
			const Element* filename = findChild(*object, "FileName");
			if (filename)
			{
				if (!isString(filename->first_property))
				{
					Error::s_message = "Invalid filename in take";
					return false;
				}
				take.filename = filename->first_property->value;
			}
			const Element* local_time = findChild(*object, "LocalTime");
			if (local_time)
			{
				if (!isLong(local_time->first_property) || !isLong(local_time->first_property->next))
				{
					Error::s_message = "Invalid local time in take";
					return false;
				}

				take.local_time_from = fbxTimeToSeconds(local_time->first_property->value.toI64());
				take.local_time_to = fbxTimeToSeconds(local_time->first_property->next->value.toI64());
			}
			const Element* reference_time = findChild(*object, "ReferenceTime");
			if (reference_time)
			{
				if (!isLong(reference_time->first_property) || !isLong(reference_time->first_property->next))
				{
					Error::s_message = "Invalid reference time in take";
					return false;
				}

				take.reference_time_from = fbxTimeToSeconds(reference_time->first_property->value.toI64());
				take.reference_time_to = fbxTimeToSeconds(reference_time->first_property->next->value.toI64());
			}

			scene->m_take_infos.push_back(take);
		}

		object = object->sibling;
	}

	return true;
}


static float getFramerateFromTimeMode(FrameRate time_mode, float custom_frame_rate)
{
	switch (time_mode)
	{
		case FrameRate_DEFAULT: return 1;
		case FrameRate_120: return 120;
		case FrameRate_100: return 100;
		case FrameRate_60: return 60;
		case FrameRate_50: return 50;
		case FrameRate_48: return 48;
		case FrameRate_30: return 30;
		case FrameRate_30_DROP: return 30;
		case FrameRate_NTSC_DROP_FRAME: return 29.9700262f;
		case FrameRate_NTSC_FULL_FRAME: return 29.9700262f;
		case FrameRate_PAL: return 25;
		case FrameRate_CINEMA: return 24;
		case FrameRate_1000: return 1000;
		case FrameRate_CINEMA_ND: return 23.976f;
		case FrameRate_CUSTOM: return custom_frame_rate;
	}
	return -1;
}


static void parseGlobalSettings(const Element& root, Scene* scene)
{
	for (ofbx::Element* settings = root.child; settings; settings = settings->sibling)
	{
		if (settings->id == "GlobalSettings")
		{
			for (ofbx::Element* props70 = settings->child; props70; props70 = props70->sibling)
			{
				if (props70->id == "Properties70")
				{
					for (ofbx::Element* node = props70->child; node; node = node->sibling)
					{
						if (!node->first_property)
							continue;

#define get_property(name, field, type) if(node->first_property->value == name) \
						{ \
							ofbx::IElementProperty* prop = node->getProperty(4); \
							if (prop) \
							{ \
								ofbx::DataView value = prop->getValue(); \
								scene->m_settings.field = *(type*)value.begin; \
							} \
						}

						get_property("UpAxis", UpAxis, UpVector);
						get_property("UpAxisSign", UpAxisSign, int);
						get_property("FrontAxis", FrontAxis, FrontVector);
						get_property("FrontAxisSign", FrontAxisSign, int);
						get_property("CoordAxis", CoordAxis, CoordSystem);
						get_property("CoordAxisSign", CoordAxisSign, int);
						get_property("OriginalUpAxis", OriginalUpAxis, int);
						get_property("OriginalUpAxisSign", OriginalUpAxisSign, int);
						get_property("UnitScaleFactor", UnitScaleFactor, float);
						get_property("OriginalUnitScaleFactor", OriginalUnitScaleFactor, float);
						get_property("TimeSpanStart", TimeSpanStart, u64);
						get_property("TimeSpanStop", TimeSpanStop, u64);
						get_property("TimeMode", TimeMode, FrameRate);
						get_property("CustomFrameRate", CustomFrameRate, float);

#undef get_property

						scene->m_scene_frame_rate = getFramerateFromTimeMode(scene->m_settings.TimeMode, scene->m_settings.CustomFrameRate);
					}
					break;
				}
			}
			break;
		}
	}
}


static bool parseObjects(const Element& root, Scene* scene)
{
	const Element* objs = findChild(root, "Objects");
	if (!objs) return true;

	scene->m_root = new Root(*scene, root);
	scene->m_root->id = 0;
	scene->m_object_map[0] = {&root, scene->m_root};

	const Element* object = objs->child;
	while (object)
	{
		if (!isLong(object->first_property))
		{
			Error::s_message = "Invalid";
			return false;
		}

		u64 id = object->first_property->value.toU64();
		scene->m_object_map[id] = {object, nullptr};
		object = object->sibling;
	}

	for (auto iter : scene->m_object_map)
	{
		OptionalError<Object*> obj = nullptr;

		if (iter.second.object == scene->m_root) continue;

		if (iter.second.element->id == "Geometry")
		{
			Property* last_prop = iter.second.element->first_property;
			while (last_prop->next) last_prop = last_prop->next;
			if (last_prop && last_prop->value == "Mesh")
			{
				obj = parseGeometry(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "Material")
		{
			obj = parseMaterial(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "AnimationStack")
		{
			obj = parse<AnimationStackImpl>(*scene, *iter.second.element);
			if (!obj.isError())
			{
				AnimationStackImpl* stack = (AnimationStackImpl*)obj.getValue();
				scene->m_animation_stacks.push_back(stack);
			}
		}
		else if (iter.second.element->id == "AnimationLayer")
		{
			obj = parse<AnimationLayerImpl>(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "AnimationCurve")
		{
			obj = parseAnimationCurve(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "AnimationCurveNode")
		{
			obj = parse<AnimationCurveNodeImpl>(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "Deformer")
		{
			IElementProperty* class_prop = iter.second.element->getProperty(2);

			if (class_prop)
			{
				if (class_prop->getValue() == "Cluster")
					obj = parseCluster(*scene, *iter.second.element);
				else if (class_prop->getValue() == "Skin")
					obj = parse<SkinImpl>(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "NodeAttribute")
		{
			obj = parseNodeAttribute(*scene, *iter.second.element);
		}
		else if (iter.second.element->id == "Model")
		{
			IElementProperty* class_prop = iter.second.element->getProperty(2);

			if (class_prop)
			{
				if (class_prop->getValue() == "Mesh")
				{
					obj = parseMesh(*scene, *iter.second.element);
					if (!obj.isError())
					{
						Mesh* mesh = (Mesh*)obj.getValue();
						scene->m_meshes.push_back(mesh);
						obj = mesh;
					}
				}
				else if (class_prop->getValue() == "LimbNode")
					obj = parseLimbNode(*scene, *iter.second.element);
				else
					obj = parse<NullImpl>(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "Texture")
		{
			obj = parseTexture(*scene, *iter.second.element);
		}

		if (obj.isError()) return false;

		scene->m_object_map[iter.first].object = obj.getValue();
		if (obj.getValue())
		{
			scene->m_all_objects.push_back(obj.getValue());
			obj.getValue()->id = iter.first;
		}
	}

	for (const Scene::Connection& con : scene->m_connections)
	{
		Object* parent = scene->m_object_map[con.to].object;
		Object* child = scene->m_object_map[con.from].object;
		if (!child) continue;
		if (!parent) continue;

		switch (child->getType())
		{
			case Object::Type::NODE_ATTRIBUTE:
				if (parent->node_attribute)
				{
					Error::s_message = "Invalid node attribute";
					return false;
				}
				parent->node_attribute = (NodeAttribute*)child;
				break;
			case Object::Type::ANIMATION_CURVE_NODE:
				if (parent->isNode())
				{
					AnimationCurveNodeImpl* node = (AnimationCurveNodeImpl*)child;
					node->bone = parent;
					node->bone_link_property = con.property;
				}
				break;
		}

		switch (parent->getType())
		{
			case Object::Type::MESH:
			{
				MeshImpl* mesh = (MeshImpl*)parent;
				switch (child->getType())
				{
					case Object::Type::GEOMETRY:
						if (mesh->geometry)
						{
							Error::s_message = "Invalid mesh";
							return false;
						}
						mesh->geometry = (Geometry*)child;
						break;
					case Object::Type::MATERIAL: mesh->materials.push_back((Material*)child); break;
				}
				break;
			}
			case Object::Type::SKIN:
			{
				SkinImpl* skin = (SkinImpl*)parent;
				if (child->getType() == Object::Type::CLUSTER)
				{
					ClusterImpl* cluster = (ClusterImpl*)child;
					skin->clusters.push_back(cluster);
					if (cluster->skin)
					{
						Error::s_message = "Invalid cluster";
						return false;
					}
					cluster->skin = skin;
				}
				break;
			}
			case Object::Type::MATERIAL:
			{
				MaterialImpl* mat = (MaterialImpl*)parent;
				if (child->getType() == Object::Type::TEXTURE)
				{
					Texture::TextureType type = Texture::COUNT;
					if (con.property == "NormalMap")
						type = Texture::NORMAL;
					else if (con.property == "DiffuseColor")
						type = Texture::DIFFUSE;
					if (type == Texture::COUNT) break;

					if (mat->textures[type])
					{
						break;// This may happen for some models (eg. 2 normal maps in use)
						Error::s_message = "Invalid material";
						return false;
					}

					mat->textures[type] = (Texture*)child;
				}
				break;
			}
			case Object::Type::GEOMETRY:
			{
				GeometryImpl* geom = (GeometryImpl*)parent;
				if (child->getType() == Object::Type::SKIN) geom->skin = (Skin*)child;
				break;
			}
			case Object::Type::CLUSTER:
			{
				ClusterImpl* cluster = (ClusterImpl*)parent;
				if (child->getType() == Object::Type::LIMB_NODE || child->getType() == Object::Type::MESH || child->getType() == Object::Type::NULL_NODE)
				{
					if (cluster->link)
					{
						Error::s_message = "Invalid cluster";
						return false;
					}

					cluster->link = child;
				}
				break;
			}
			case Object::Type::ANIMATION_LAYER:
			{
				if (child->getType() == Object::Type::ANIMATION_CURVE_NODE)
				{
					((AnimationLayerImpl*)parent)->curve_nodes.push_back((AnimationCurveNodeImpl*)child);
				}
			}
			break;
			case Object::Type::ANIMATION_CURVE_NODE:
			{
				AnimationCurveNodeImpl* node = (AnimationCurveNodeImpl*)parent;
				if (child->getType() == Object::Type::ANIMATION_CURVE)
				{
					if (!node->curves[0].curve)
					{
						node->curves[0].connection = &con;
						node->curves[0].curve = (AnimationCurve*)child;
					}
					else if (!node->curves[1].curve)
					{
						node->curves[1].connection = &con;
						node->curves[1].curve = (AnimationCurve*)child;
					}
					else if (!node->curves[2].curve)
					{
						node->curves[2].connection = &con;
						node->curves[2].curve = (AnimationCurve*)child;
					}
					else
					{
						Error::s_message = "Invalid animation node";
						return false;
					}
				}
				break;
			}
		}
	}

	for (auto iter : scene->m_object_map)
	{
		Object* obj = iter.second.object;
		if (!obj) continue;
		if(obj->getType() == Object::Type::CLUSTER)
		{
			if (!((ClusterImpl*)iter.second.object)->postprocess())
			{
				Error::s_message = "Failed to postprocess cluster";
				return false;
			}
		}
	}

	return true;
}


RotationOrder Object::getRotationOrder() const
{
	// This assumes that the default rotation order is EULER_XYZ.
	return (RotationOrder) resolveEnumProperty(*this, "RotationOrder", (int) RotationOrder::EULER_XYZ);
}


Vec3 Object::getRotationOffset() const
{
	return resolveVec3Property(*this, "RotationOffset", {0, 0, 0});
}


Vec3 Object::getRotationPivot() const
{
	return resolveVec3Property(*this, "RotationPivot", {0, 0, 0});
}


Vec3 Object::getPostRotation() const
{
	return resolveVec3Property(*this, "PostRotation", {0, 0, 0});
}


Vec3 Object::getScalingOffset() const
{
	return resolveVec3Property(*this, "ScalingOffset", {0, 0, 0});
}


Vec3 Object::getScalingPivot() const
{
	return resolveVec3Property(*this, "ScalingPivot", {0, 0, 0});
}


Matrix Object::evalLocal(const Vec3& translation, const Vec3& rotation) const
{
	return evalLocal(translation, rotation, getLocalScaling());
}


Matrix Object::evalLocal(const Vec3& translation, const Vec3& rotation, const Vec3& scaling) const
{
	Vec3 rotation_pivot = getRotationPivot();
	Vec3 scaling_pivot = getScalingPivot();
	RotationOrder rotation_order = getRotationOrder();

	Matrix s = makeIdentity();
	s.m[0] = scaling.x;
	s.m[5] = scaling.y;
	s.m[10] = scaling.z;

	Matrix t = makeIdentity();
	setTranslation(translation, &t);

	Matrix r = getRotationMatrix(rotation, rotation_order);
	Matrix r_pre = getRotationMatrix(getPreRotation(), RotationOrder::EULER_XYZ);
	Matrix r_post_inv = getRotationMatrix(-getPostRotation(), RotationOrder::EULER_ZYX);

	Matrix r_off = makeIdentity();
	setTranslation(getRotationOffset(), &r_off);

	Matrix r_p = makeIdentity();
	setTranslation(rotation_pivot, &r_p);

	Matrix r_p_inv = makeIdentity();
	setTranslation(-rotation_pivot, &r_p_inv);

	Matrix s_off = makeIdentity();
	setTranslation(getScalingOffset(), &s_off);

	Matrix s_p = makeIdentity();
	setTranslation(scaling_pivot, &s_p);

	Matrix s_p_inv = makeIdentity();
	setTranslation(-scaling_pivot, &s_p_inv);

	// http://help.autodesk.com/view/FBX/2017/ENU/?guid=__files_GUID_10CDD63C_79C1_4F2D_BB28_AD2BE65A02ED_htm
	return t * r_off * r_p * r_pre * r * r_post_inv * r_p_inv * s_off * s_p * s * s_p_inv;
}


Vec3 Object::getLocalTranslation() const
{
	return resolveVec3Property(*this, "Lcl Translation", {0, 0, 0});
}


Vec3 Object::getPreRotation() const
{
	return resolveVec3Property(*this, "PreRotation", {0, 0, 0});
}


Vec3 Object::getLocalRotation() const
{
	return resolveVec3Property(*this, "Lcl Rotation", {0, 0, 0});
}


Vec3 Object::getLocalScaling() const
{
	return resolveVec3Property(*this, "Lcl Scaling", {1, 1, 1});
}


Matrix Object::getGlobalTransform() const
{
	const Object* parent = getParent();
	if (!parent) return evalLocal(getLocalTranslation(), getLocalRotation());

	return parent->getGlobalTransform() * evalLocal(getLocalTranslation(), getLocalRotation());
}


Matrix Object::getLocalTransform() const
{
    return evalLocal(getLocalTranslation(), getLocalRotation(), getLocalScaling());
}


Object* Object::resolveObjectLinkReverse(Object::Type type) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toU64() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.from == id && connection.to != 0)
		{
			Object* obj = scene.m_object_map.find(connection.to)->second.object;
			if (obj && obj->getType() == type) return obj;
		}
	}
	return nullptr;
}


const IScene& Object::getScene() const
{
	return scene;
}


Object* Object::resolveObjectLink(int idx) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toU64() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj)
			{
				if (idx == 0) return obj;
				--idx;
			}
		}
	}
	return nullptr;
}


Object* Object::resolveObjectLink(Object::Type type, const char* property, int idx) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toU64() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj && obj->getType() == type)
			{
				if (property == nullptr || connection.property == property)
				{
					if (idx == 0) return obj;
					--idx;
				}
			}
		}
	}
	return nullptr;
}


Object* Object::getParent() const
{
	Object* parent = nullptr;
	for (auto& connection : scene.m_connections)
	{
		if (connection.from == id)
		{
			Object* obj = scene.m_object_map.find(connection.to)->second.object;
			if (obj && obj->is_node)
			{
				assert(parent == nullptr);
				parent = obj;
			}
		}
	}
	return parent;
}


IScene* load(const u8* data, int size)
{
	std::unique_ptr<Scene> scene(new Scene());
	scene->m_data.resize(size);
	memcpy(&scene->m_data[0], data, size);
	OptionalError<Element*> root = tokenize(&scene->m_data[0], size);
	if (root.isError())
	{
		Error::s_message = "";
		root = tokenizeText(&scene->m_data[0], size);
		if (root.isError()) return nullptr;
	}

	scene->m_root_element = root.getValue();
	assert(scene->m_root_element);

	//if (parseTemplates(*root.getValue()).isError()) return nullptr;
	if(!parseConnections(*root.getValue(), scene.get())) return nullptr;
	if(!parseTakes(scene.get())) return nullptr;
	if(!parseObjects(*root.getValue(), scene.get())) return nullptr;
	parseGlobalSettings(*root.getValue(), scene.get());

	return scene.release();
}


const char* getError()
{
	return Error::s_message;
}


} // namespace ofbx
