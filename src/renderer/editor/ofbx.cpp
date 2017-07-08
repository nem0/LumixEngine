#include "ofbx.h"
#include "miniz.h"
#include <cassert>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>


namespace ofbx
{


struct Error
{
	Error() {}
	Error(const char* msg) { s_message = msg; }

	static const char* s_message;
};


const char* Error::s_message = "";


template <typename T>
struct OptionalError
{
	OptionalError(Error error)
		: is_error(true)
	{}


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

	
	#ifdef _DEBUG
		~OptionalError()
		{
			assert(error_checked);
		}
	#endif

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


static Matrix getRotationMatrix(const Vec3& euler)
{
	const double TO_RAD = 3.1415926535897932384626433832795028 / 180.0;
	Matrix rx = rotationX(euler.x * TO_RAD);
	Matrix ry = rotationY(euler.y * TO_RAD);
	Matrix rz = rotationZ(euler.z * TO_RAD);
	return rz * ry * rx;
}


static double fbxTimeToSeconds(u64 value)
{
	return double(value) / 46186158000L;
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


u64 DataView::toLong() const
{
	assert(end - begin == sizeof(u64));
	return *(u64*)begin;
}


double DataView::toDouble() const
{
	assert(end - begin == sizeof(double));
	return *(double*)begin;
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
template <typename T> void parseBinaryArrayRaw(const Property& property, T* out, int max_size);
template <typename T> void parseBinaryArray(Property& property, std::vector<T>* out);


struct Property : IElementProperty
{
	~Property() { delete next; }
	Type getType() const override { return (Type)type; }
	IElementProperty* getNext() const override { return next; }
	DataView getValue() const override { return value; }
	int getCount() const override
	{
		assert(type == ARRAY_DOUBLE || type == ARRAY_INT || type == ARRAY_FLOAT || type == ARRAY_LONG);
		return int(*(u32*)value.begin);
	}

	void getValues(double* values, int max_size) const override { parseBinaryArrayRaw(*this, values, max_size); }

	void getValues(float* values, int max_size) const override { parseBinaryArrayRaw(*this, values, max_size); }

	void getValues(u64* values, int max_size) const override { parseBinaryArrayRaw(*this, values, max_size); }

	void getValues(int* values, int max_size) const override { parseBinaryArrayRaw(*this, values, max_size); }

	u8 type;
	DataView value;
	Property* next = nullptr;
};


struct Element : IElement
{
	~Element()
	{
		delete child;
		delete sibling;
		delete first_property;
	}

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


static Vec3 resolveVec3Property(const Object& object, const char* name, const Vec3& default_value)
{
	Element* element = (Element*)object.resolveProperty(name);
	if (!element) return default_value;
	Property* x = (Property*)element->getProperty(4);
	if (!x || !x->next || !x->next->next) return default_value;

	return {x->value.toDouble(), x->next->value.toDouble(), x->next->next->value.toDouble()};
}


Object::Object(const Scene& _scene, const IElement& _element)
	: scene(_scene)
	, element(_element)
	, is_node(false)
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


static void decompress(const u8* in, size_t in_size, u8* out, size_t out_size)
{
	mz_stream stream = {};
	mz_inflateInit(&stream);

	int status;
	stream.avail_in = (int)in_size;
	stream.next_in = in;
	stream.avail_out = (int)out_size;
	stream.next_out = out;

	status = mz_inflate(&stream, Z_SYNC_FLUSH);

	assert(status == Z_STREAM_END);

	if (mz_inflateEnd(&stream) != Z_OK)
	{
		printf("inflateEnd() failed!\n");
	}
}


template <typename T>
static OptionalError<T> read(Cursor* cursor)
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

	std::unique_ptr<Property> prop = std::make_unique<Property>();
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
		default: 
			return Error("Unknown property type");
	}
	prop->value.end = cursor->current;
	return prop.release();
}


static OptionalError<Element*> readElement(Cursor* cursor)
{
	OptionalError<u32> end_offset = read<u32>(cursor);
	if (end_offset.isError()) return Error();
	if (end_offset.getValue() == 0) return nullptr;

	OptionalError<u32> prop_count = read<u32>(cursor);
	OptionalError<u32> prop_length = read<u32>(cursor);
	if (prop_count.isError() || prop_length.isError()) return Error();

	const char* sbeg = 0;
	const char* send = 0;
	OptionalError<DataView> id = readShortString(cursor);
	if (id.isError()) return Error();

	std::unique_ptr<Element> element = std::make_unique<Element>();
	element->first_property = nullptr;
	element->id = id.getValue();

	element->child = nullptr;
	element->sibling = nullptr;

	Property** prop_link = &element->first_property;
	for (u32 i = 0; i < prop_count.getValue(); ++i)
	{
		OptionalError<Property*> prop = readProperty(cursor);
		if (prop.isError()) return Error();
		
		*prop_link = prop.getValue();
		prop_link = &(*prop_link)->next;
	}

	if (cursor->current - cursor->begin >= end_offset.getValue()) return element.release();

	constexpr int BLOCK_SENTINEL_LENGTH = 13;

	Element** link = &element->child;
	while (cursor->current - cursor->begin < (end_offset.getValue() - BLOCK_SENTINEL_LENGTH))
	{
		OptionalError<Element*> child = readElement(cursor);
		if (child.isError()) return Error();

		*link = child.getValue();
		link = &(*link)->sibling;
	}

	if (cursor->current + BLOCK_SENTINEL_LENGTH > cursor->end) return Error("Reading past the end");
	
	cursor->current += BLOCK_SENTINEL_LENGTH;
	return element.release();
}


static OptionalError<Element*> tokenize(const u8* data, size_t size)
{
	Cursor cursor;
	cursor.begin = data;
	cursor.current = data;
	cursor.end = data + size;

	const Header* header = (const Header*)cursor.current;
	cursor.current += sizeof(*header);

	std::unique_ptr<Element> root = std::make_unique<Element>();
	root->first_property = nullptr;
	root->id.begin = nullptr;
	root->id.end = nullptr;
	root->child = nullptr;
	root->sibling = nullptr;

	Element** element = &root->child;
	for (;;)
	{
		OptionalError<Element*> child = readElement(&cursor);
		if (child.isError()) return Error();
		*element = child.getValue();
		if (!*element) return root.release();
		element = &(*element)->sibling;
	}
	return root.release();
}


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
		Matrix mtx = getRotationMatrix(rotation);
		setTranslation(translation, &mtx);

		return scale_mtx * mtx;
	}


	Type getType() const override { return Type::MESH; }


	const Geometry* getGeometry() const override { return geometry; }
	const Material* getMaterial(int index) const override { return materials[index]; }
	int getMaterialCount() const override { return (int)materials.size(); }


	void postprocess()
	{
		const Material* material = nullptr;
		for (int i = 0; material = resolveObjectLink<Material>(i); ++i)
		{
			materials.push_back(material);
		}
		geometry = resolveObjectLink<Geometry>(0);
		
		assert(!resolveObjectLink<Geometry>(1));
	}


	const Geometry* geometry;
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


	const Texture* getTexture(Texture::TextureType type) const override
	{
		return textures[type];
	}


	void postprocess()
	{
		textures[Texture::DIFFUSE] = (const Texture*)resolveObjectLink(Object::Type::TEXTURE, "DiffuseColor", 0);
		textures[Texture::NORMAL] = (const Texture*)resolveObjectLink(Object::Type::TEXTURE, "NormalMap", 0);

		assert(!resolveObjectLink(Object::Type::TEXTURE, "DiffuseColor", 1));
		assert(!resolveObjectLink(Object::Type::TEXTURE, "NormalMap", 1));
	}

	const Texture* textures[Texture::TextureType::COUNT];
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
	Type getType() const override { return Type::NOTE_ATTRIBUTE; }
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

	std::vector<Vec3> vertices;
	std::vector<Vec3> normals;
	std::vector<Vec2> uvs;
	std::vector<Vec4> colors;
	std::vector<Vec3> tangents;
	std::vector<int> materials;

	const Skin* skin = nullptr;

	std::vector<int> to_old_vertices;

	GeometryImpl(const Scene& _scene, const IElement& _element)
		: Geometry(_scene, _element)
	{
	}


	Type getType() const override { return Type::GEOMETRY; }
	int getVertexCount() const override { return (int)vertices.size(); }
	const Vec3* getVertices() const override { return &vertices[0]; }
	const Vec3* getNormals() const override { return normals.empty() ? nullptr : &normals[0]; }
	const Vec2* getUVs() const override { return uvs.empty() ? nullptr : &uvs[0]; }
	const Vec4* getColors() const override { return colors.empty() ? nullptr : &colors[0]; }
	const Vec3* getTangents() const override { return tangents.empty() ? nullptr : &tangents[0]; }
	const Skin* getSkin() const override { return skin; }


	void postprocess()
	{
		skin = resolveObjectLink<Skin>(0);
		assert(resolveObjectLink<Skin>(1) == nullptr);
	}


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
	virtual int getIndicesCount() const override { return (int)indices.size(); }
	const double* getWeights() const override { return &weights[0]; }
	int getWeightsCount() const override { return (int)weights.size(); }
	Matrix getTransformMatrix() const { return transform_matrix; }
	Matrix getTransformLinkMatrix() const { return transform_link_matrix; }
	Object* getLink() const override { return resolveObjectLink(Object::Type::LIMB_NODE, nullptr, 0); }


	void postprocess()
	{
		Object* skin = resolveObjectLinkReverse(Object::Type::SKIN);
		if (!skin) return;

		GeometryImpl* geom = (GeometryImpl*)skin->resolveObjectLinkReverse(Object::Type::GEOMETRY);
		if (!geom) return;

		std::vector<int> old_indices;
		const Element* indexes = findChild((const Element&)element, "Indexes");
		if (indexes && indexes->first_property)
		{
			parseBinaryArray(*indexes->first_property, &old_indices);
		}

		std::vector<double> old_weights;
		const Element* weights_el = findChild((const Element&)element, "Weights");
		if (weights_el && weights_el->first_property)
		{
			parseBinaryArray(*weights_el->first_property, &old_weights);
		}

		assert(old_indices.size() == old_weights.size());

		struct NewNode
		{
			int value = -1;
			NewNode* next = nullptr;
		};

		struct Pool
		{
			NewNode* pool = nullptr;
			int pool_index = 0;

			Pool(size_t count) { pool = new NewNode[count]; }
			~Pool() { delete[] pool; }

			void add(NewNode& node, int i)
			{
				if (node.value == -1)
				{
					node.value = i;
				}
				else if (node.next)
				{
					add(*node.next, i);
				}
				else
				{
					node.next = &pool[pool_index];
					++pool_index;
					node.next->value = i;
				}
			}
		} pool(geom->to_old_vertices.size());

		std::vector<NewNode> to_new;

		to_new.resize(geom->to_old_vertices.size());
		for (int i = 0, c = (int)geom->to_old_vertices.size(); i < c; ++i)
		{
			int old = geom->to_old_vertices[i];
			pool.add(to_new[old], i);
		}

		for (int i = 0, c = (int)old_indices.size(); i < c; ++i)
		{
			int old_idx = old_indices[i];
			double w = old_weights[i];
			NewNode* n = &to_new[old_idx];
			while (n)
			{
				indices.push_back(n->value);
				weights.push_back(w);
				n = n->next;
			}
		}
	}


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


	const AnimationLayer* getLayer(int index) const
	{
		assert(index == 0);
		return resolveObjectLink<AnimationLayer>(index);
	}


	Type getType() const override { return Type::ANIMATION_STACK; }
};


struct AnimationLayerImpl : AnimationLayer
{
	AnimationLayerImpl(const Scene& _scene, const IElement& _element)
		: AnimationLayer(_scene, _element)
	{
	}

	Type getType() const override { return Type::ANIMATION_LAYER; }
};


struct AnimationCurveImpl : AnimationCurve
{
	AnimationCurveImpl(const Scene& _scene, const IElement& _element)
		: AnimationCurve(_scene, _element)
	{
	}

	int getKeyCount() const override { return (int)times.size(); }
	const u64* getKeyTime() const override { return &times[0]; }
	const float* getKeyValue() const override { return &values[0]; }

	std::vector<u64> times;
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

	int getClusterCount() const override { return resolveObjectLinkCount(Type::CLUSTER); }
	Cluster* getCluster(int idx) const override { return resolveObjectLink<Cluster>(idx); }

	Type getType() const override { return Type::SKIN; }
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


	int getAnimationStackCount() const { return (int)m_animation_stacks.size(); }
	int getMeshCount() const override { return (int)m_meshes.size(); }


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


	IElement* getRootElement() const override { return m_root_element; }
	Object* getRoot() const override { return m_root; }


	void destroy() override { delete this; }


	~Scene()
	{
		for (auto iter : m_object_map)
		{
			delete iter.second.object;
		}
		delete m_root_element;
	}


	Element* m_root_element = nullptr;
	Root* m_root = nullptr;
	std::unordered_map<u64, ObjectPair> m_object_map;
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


	Vec3 getNodeLocalTransform(double time) const override
	{
		if (time < key_times[0]) time = key_times[0];
		if (time > key_times.back()) time = key_times.back();

		const Vec3* values = (const Vec3*)&key_values[0];
		for (int i = 1, c = (int)key_times.size(); i < c; ++i)
		{
			if (key_times[i] >= time)
			{
				float t = float((time - key_times[i - 1]) / (key_times[i] - key_times[i - 1]));
				return values[i - 1] * (1 - t) + values[i] * t;
			}
		}

		assert(false);
		return {0, 0, 0};
	}


	struct Curve
	{
		const AnimationCurve* curve;
		const Scene::Connection* connection;
	};


	Curve getConnection(const Object& obj, int idx) const
	{
		u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
		for (auto& connection : scene.m_connections)
		{
			if (connection.to == id && connection.from != 0)
			{
				Object* obj = scene.m_object_map.find(connection.from)->second.object;
				if (obj)
				{
					if (idx == 0)
					{
						return {(const AnimationCurve*)obj, &connection};
					}
					--idx;
				}
			}
		}
		return {nullptr, nullptr};
	}


	void postprocess()
	{
		Curve curves[3];
		curves[0] = getConnection(*this, 0);
		curves[1] = getConnection(*this, 1);
		curves[2] = getConnection(*this, 2);
		assert(getConnection(*this, 3).curve == nullptr);

		int count = curves[0].curve->getKeyCount();
		const u64* times = curves[0].curve->getKeyTime();
		const float* values_x = curves[0].curve ? curves[0].curve->getKeyValue() : nullptr;
		const float* values_y = curves[1].curve ? curves[1].curve->getKeyValue() : nullptr;
		const float* values_z = curves[2].curve ? curves[2].curve->getKeyValue() : nullptr;
		for (int i = 0; i < count; ++i)
		{
			key_times.push_back(fbxTimeToSeconds(times[i]));
			key_values.push_back(values_x[i]);
			if (values_y) key_values.push_back(values_y[i]);
			if (values_z) key_values.push_back(values_z[i]);
		}
	}


	Type getType() const override { return Type::ANIMATION_CURVE_NODE; }
	std::vector<double> key_values;
	std::vector<double> key_times;
	enum Mode
	{
		TRANSLATION,
		ROTATION,
		SCALE
	} mode = TRANSLATION;
};


struct Texture* parseTexture(const Scene& scene, const Element& element)
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


template <typename T> static T* parse(const Scene& scene, const Element& element)
{
	T* obj = new T(scene, element);
	return obj;
}


static Object* parseCluster(const Scene& scene, const Element& element)
{
	ClusterImpl* obj = new ClusterImpl(scene, element);

	const Element* transform_link = findChild(element, "TransformLink");
	if (transform_link && transform_link->first_property)
	{
		parseBinaryArrayRaw(
			*transform_link->first_property, &obj->transform_link_matrix, sizeof(obj->transform_link_matrix));
	}
	const Element* transform = findChild(element, "Transform");
	if (transform && transform->first_property)
	{
		parseBinaryArrayRaw(*transform->first_property, &obj->transform_matrix, sizeof(obj->transform_matrix));
	}
	return obj;
}


static Object* parseNodeAttribute(const Scene& scene, const Element& element)
{
	NodeAttributeImpl* obj = new NodeAttributeImpl(scene, element);
	const Element* type_flags = findChild(element, "TypeFlags");
	if (type_flags && type_flags->first_property)
	{
		obj->attribute_type = type_flags->first_property->value;
	}
	return obj;
}


static Object* parseLimbNode(const Scene& scene, const Element& element)
{

	assert(element.first_property);
	assert(element.first_property->next);
	assert(element.first_property->next->next);
	assert(element.first_property->next->next->value == "LimbNode");

	LimbNodeImpl* obj = new LimbNodeImpl(scene, element);
	return obj;
}


static Mesh* parseMesh(const Scene& scene, const Element& element)
{
	assert(element.first_property);
	assert(element.first_property->next);
	assert(element.first_property->next->next);
	assert(element.first_property->next->next->value == "Mesh");

	return new MeshImpl(scene, element);
}


static Material* parseMaterial(const Scene& scene, const Element& element)
{
	assert(element.first_property);
	MaterialImpl* material = new MaterialImpl(scene, element);
	const Element* prop = findChild(element, "Properties70");
	if (prop) prop = prop->child;
	while (prop)
	{
		if (prop->id == "P" && prop->first_property)
		{
			if (prop->first_property->value == "DiffuseColor")
			{
				// TODO
			}
		}
		prop = prop->sibling;
	}
	return material;
}


static u32 getArrayCount(const Property& property)
{
	return *(const u32*)property.value.begin;
}


template <typename T> static void parseBinaryArrayRaw(const Property& property, T* out, int max_size)
{
	assert(out);
	u32 count = getArrayCount(property);
	u32 enc = *(const u32*)(property.value.begin + 4);
	u32 len = *(const u32*)(property.value.begin + 8);

	int elem_size = 1;
	switch (property.type)
	{
		case 'l': elem_size = 8; break;
		case 'd': elem_size = 8; break;
		case 'f': elem_size = 4; break;
		case 'i': elem_size = 4; break;
		default: assert(false);
	}

	const u8* data = property.value.begin + sizeof(u32) * 3;
	if (enc == 0)
	{
		assert((int)len <= max_size);
		memcpy(out, data, len);
	}
	else if (enc == 1)
	{
		assert(int(elem_size * count) <= max_size);
		decompress(data, len, (u8*)out, elem_size * count);
	}
	else
	{
		assert(false);
	}
}


template <typename T> static void parseBinaryArray(Property& property, std::vector<T>* out)
{
	assert(out);
	u32 count = getArrayCount(property);
	int elem_size = 1;
	switch (property.type)
	{
		case 'd': elem_size = 8; break;
		case 'f': elem_size = 4; break;
		case 'i': elem_size = 4; break;
		default: assert(false);
	}
	int elem_count = sizeof(T) / elem_size;
	out->resize(count / elem_count);

	parseBinaryArrayRaw(property, &(*out)[0], int(sizeof((*out)[0]) * out->size()));
}


template <typename T> static void parseDoubleVecData(Property& property, std::vector<T>* out_vec)
{
	assert(out_vec);
	if (property.type == 'd')
	{
		parseBinaryArray(property, out_vec);
	}
	else
	{
		assert(property.type == 'f');
		assert(sizeof((*out_vec)[0].x) == sizeof(double));
		std::vector<float> tmp;
		parseBinaryArray(property, &tmp);
		int elem_count = sizeof((*out_vec)[0]) / sizeof((*out_vec)[0].x);
		out_vec->resize(tmp.size() / elem_count);
		double* out = &(*out_vec)[0].x;
		for (int i = 0, c = (int)tmp.size(); i < c; ++i)
		{
			out[i] = tmp[i];
		}
	}
}


template <typename T>
static void parseVertexData(const Element& element,
	const char* name,
	const char* index_name,
	std::vector<T>* out,
	std::vector<int>* out_indices,
	GeometryImpl::VertexDataMapping* mapping)
{
	assert(out);
	assert(mapping);
	const Element* data_element = findChild(element, name);
	if (data_element && data_element->first_property)
	{
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
				assert(false);
			}
		}
		if (reference_element && reference_element->first_property)
		{
			if (reference_element->first_property->value == "IndexToDirect")
			{
				const Element* indices_element = findChild(element, index_name);
				if (indices_element && indices_element->first_property)
				{
					parseBinaryArray(*indices_element->first_property, out_indices);
				}
			}
			else if (reference_element->first_property->value != "Direct")
			{
				assert(false);
			}
		}
		parseDoubleVecData(*data_element->first_property, out);
	}
}


template <typename T>
static void splat(std::vector<T>* out,
	GeometryImpl::VertexDataMapping mapping,
	const std::vector<T>& data,
	const std::vector<int>& indices,
	const std::vector<int>& to_old_vertices)
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
			for (int i = 0, c = (int)indices.size(); i < c; ++i)
			{
				(*out)[i] = data[indices[i]];
			}
		}
	}
	else if (mapping == GeometryImpl::BY_VERTEX)
	{
		//  v0  v1 ...
		// uv0 uv1 ...
		assert(indices.empty());

		out->resize(to_old_vertices.size());

		for (int i = 0, c = (int)to_old_vertices.size(); i < c; ++i)
		{
			(*out)[i] = data[to_old_vertices[i]];
		}
	}
	else
	{
		assert(false);
	}
}


template <typename T> static void remap(std::vector<T>* out, std::vector<int> map)
{
	if (out->empty()) return;

	std::vector<T> old;
	old.swap(*out);
	for (int i = 0, c = (int)map.size(); i < c; ++i)
	{
		out->push_back(old[map[i]]);
	}
}


static AnimationCurve* parseAnimationCurve(const Scene& scene, const Element& element)
{
	AnimationCurveImpl* curve = new AnimationCurveImpl(scene, element);

	const Element* times = findChild(element, "KeyTime");
	const Element* values = findChild(element, "KeyValueFloat");

	if (times)
	{
		curve->times.resize(times->first_property->getCount());
		times->first_property->getValues(&curve->times[0], (int)curve->times.size() * sizeof(curve->times[0]));
	}

	if (values)
	{
		curve->values.resize(values->first_property->getCount());
		values->first_property->getValues(&curve->values[0], (int)curve->values.size() * sizeof(curve->values[0]));
	}

	return curve;
}


static int getTriCountFromPoly(const std::vector<int>& indices, int* idx)
{
	int count = 1;
	while (indices[*idx + 1 + count] >= 0)
	{
		++count;
	};

	*idx = *idx + 2 + count;
	return count;
}


static Geometry* parseGeometry(const Scene& scene, const Element& element)
{
	assert(element.first_property);

	const Element* vertices_element = findChild(element, "Vertices");
	if (!vertices_element || !vertices_element->first_property) return nullptr;

	const Element* polys_element = findChild(element, "PolygonVertexIndex");
	if (!polys_element || !polys_element->first_property) return nullptr;

	GeometryImpl* geom = new GeometryImpl(scene, element);

	std::vector<Vec3> vertices;
	parseDoubleVecData(*vertices_element->first_property, &vertices);
	std::vector<int> original_indices;
	parseBinaryArray(*polys_element->first_property, &original_indices);

	std::vector<int> to_old_indices;
	geom->triangulate(original_indices, &geom->to_old_vertices, &to_old_indices);
	geom->vertices.resize(geom->to_old_vertices.size());

	for (int i = 0, c = (int)geom->to_old_vertices.size(); i < c; ++i)
	{
		geom->vertices[i] = vertices[geom->to_old_vertices[i]];
	}


	const Element* layer_material_element = findChild(element, "LayerElementMaterial");
	if (layer_material_element)
	{
		const Element* mapping_element = findChild(*layer_material_element, "MappingInformationType");
		const Element* reference_element = findChild(*layer_material_element, "ReferenceInformationType");

		std::vector<int> tmp;

		assert(mapping_element);
		assert(reference_element);
		if (mapping_element->first_property->value == "ByPolygon" && reference_element->first_property->value == "IndexToDirect")
		{
			geom->materials.reserve(geom->vertices.size() / 3);
			for (int& i : geom->materials) i = -1;

			const Element* indices_element = findChild(*layer_material_element, "Materials");
			if (indices_element && indices_element->first_property)
			{
				parseBinaryArray(*indices_element->first_property, &tmp);

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
		}
		else
		{
			assert(mapping_element->first_property->value == "AllSame");
		}
		/*
		parseVertexData(*layer_material_element, "UV", "Materials", &tmp, &tmp_indices, &mapping);
		geom->uvs.resize(tmp_indices.empty() ? tmp.size() : tmp_indices.size());
		splat(&geom->uvs, mapping, tmp, tmp_indices, geom->to_old_vertices);
		remap(&geom->uvs, to_old_indices);*/
}

	const Element* layer_uv_element = findChild(element, "LayerElementUV");
	if (layer_uv_element)
	{
		std::vector<Vec2> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		parseVertexData(*layer_uv_element, "UV", "UVIndex", &tmp, &tmp_indices, &mapping);
		geom->uvs.resize(tmp_indices.empty() ? tmp.size() : tmp_indices.size());
		splat(&geom->uvs, mapping, tmp, tmp_indices, geom->to_old_vertices);
		remap(&geom->uvs, to_old_indices);
	}

	const Element* layer_tangent_element = findChild(element, "LayerElementTangents");
	if (layer_tangent_element)
	{
		std::vector<Vec3> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		if (findChild(*layer_tangent_element, "Tangents"))
		{
			parseVertexData(*layer_tangent_element, "Tangents", "TangentsIndex", &tmp, &tmp_indices, &mapping);
		}
		else
		{
			parseVertexData(*layer_tangent_element, "Tangent", "TangentIndex", &tmp, &tmp_indices, &mapping);
		}
		splat(&geom->tangents, mapping, tmp, tmp_indices, geom->to_old_vertices);
		remap(&geom->tangents, to_old_indices);
	}

	const Element* layer_color_element = findChild(element, "LayerElementColor");
	if (layer_color_element)
	{
		std::vector<Vec4> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		parseVertexData(*layer_color_element, "Colors", "ColorIndex", &tmp, &tmp_indices, &mapping);
		splat(&geom->colors, mapping, tmp, tmp_indices, geom->to_old_vertices);
		remap(&geom->colors, to_old_indices);
	}

	const Element* layer_normal_element = findChild(element, "LayerElementNormal");
	if (layer_normal_element)
	{
		std::vector<Vec3> tmp;
		std::vector<int> tmp_indices;
		GeometryImpl::VertexDataMapping mapping;
		parseVertexData(*layer_normal_element, "Normals", "NormalsIndex", &tmp, &tmp_indices, &mapping);
		splat(&geom->normals, mapping, tmp, tmp_indices, geom->to_old_vertices);
		remap(&geom->normals, to_old_indices);
	}

	return geom;
}


static void parseConnections(const Element& root, Scene* scene)
{
	assert(scene);

	const Element* connections = findChild(root, "Connections");
	if (!connections) return;

	const Element* connection = connections->child;
	while (connection)
	{
		assert(connection->first_property);
		assert(connection->first_property->next);
		assert(connection->first_property->next->next);

		Scene::Connection c;
		c.from = connection->first_property->next->value.toLong();
		c.to = connection->first_property->next->next->value.toLong();
		if (connection->first_property->value == "OO")
		{
			c.type = Scene::Connection::OBJECT_OBJECT;
		}
		else if (connection->first_property->value == "OP")
		{
			c.type = Scene::Connection::OBJECT_PROPERTY;
			assert(connection->first_property->next->next->next);
			c.property = connection->first_property->next->next->next->value;
		}
		else
		{
			assert(false);
		}
		scene->m_connections.push_back(c);

		connection = connection->sibling;
	}
}


static void parseTakes(Scene* scene)
{
	const Element* takes = findChild((const Element&)*scene->getRootElement(), "Takes");
	if (!takes) return;

	const Element* object = takes->child;
	while (object)
	{
		if (object->id == "Take")
		{
			TakeInfo take;
			take.name = object->first_property->value;
			const Element* filename = findChild(*object, "FileName");
			if (filename) take.filename = filename->first_property->value;
			const Element* local_time = findChild(*object, "LocalTime");
			if (local_time)
			{
				take.local_time_from = fbxTimeToSeconds(local_time->first_property->value.toLong());
				take.local_time_to = fbxTimeToSeconds(local_time->first_property->next->value.toLong());
			}
			const Element* reference_time = findChild(*object, "ReferenceTime");
			if (reference_time)
			{
				take.reference_time_from = fbxTimeToSeconds(reference_time->first_property->value.toLong());
				take.reference_time_to = fbxTimeToSeconds(reference_time->first_property->next->value.toLong());
			}

			scene->m_take_infos.push_back(take);
		}

		object = object->sibling;
	}
}


static void parseObjects(const Element& root, Scene* scene)
{
	const Element* objs = findChild(root, "Objects");
	if (!objs) return;

	scene->m_root = new Root(*scene, root);
	scene->m_root->id = 0;
	scene->m_object_map[0] = {&root, scene->m_root};

	const Element* object = objs->child;
	while (object)
	{
		assert(object->first_property);
		assert(object->first_property->type == 'L');
		u64 id = *(u64*)object->first_property->value.begin;

		scene->m_object_map[id] = {object, nullptr};
		object = object->sibling;
	}

	for (auto iter : scene->m_object_map)
	{
		Object* obj = nullptr;

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
			AnimationStack* stack = parse<AnimationStackImpl>(*scene, *iter.second.element);
			obj = stack;
			scene->m_animation_stacks.push_back(stack);
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
					Mesh* mesh = parseMesh(*scene, *iter.second.element);
					scene->m_meshes.push_back(mesh);
					obj = mesh;
				}
				else if (class_prop->getValue() == "LimbNode")
					obj = parseLimbNode(*scene, *iter.second.element);
				else if (class_prop->getValue() == "Null")
					obj = parse<NullImpl>(*scene, *iter.second.element);
			}
		}
		else if (iter.second.element->id == "Texture")
		{
			obj = parseTexture(*scene, *iter.second.element);
		}

		scene->m_object_map[iter.first].object = obj;
		if (obj) obj->id = iter.first;
	}

	for (auto iter : scene->m_object_map)
	{
		Object* obj = iter.second.object;
		if (!obj) continue;
		switch (obj->getType())
		{
			case Object::Type::MATERIAL: ((MaterialImpl*)iter.second.object)->postprocess(); break;
			case Object::Type::GEOMETRY: ((GeometryImpl*)iter.second.object)->postprocess(); break;
			case Object::Type::CLUSTER: ((ClusterImpl*)iter.second.object)->postprocess(); break;
			case Object::Type::MESH: ((MeshImpl*)iter.second.object)->postprocess(); break;
			case Object::Type::ANIMATION_CURVE_NODE:
				((AnimationCurveNodeImpl*)iter.second.object)->postprocess();
				break;
		}
	}
}


template <typename T>
static int getVertexDataCount(GeometryImpl::VertexDataMapping mapping,
	const std::vector<T>& data,
	const std::vector<int>& indices)
{
	if (data.empty()) return 0;
	assert(mapping == GeometryImpl::BY_POLYGON_VERTEX);

	if (indices.empty())
	{
		return (int)data.size();
	}
	else
	{
		return (int)indices.size();
	}
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


const AnimationCurveNode* Object::getCurveNode(const char* prop, const AnimationLayer& layer) const
{
	const AnimationCurveNode* curve_node = nullptr;
	for (int i = 0;
		 curve_node = (const AnimationCurveNode*)resolveObjectLink(Object::Type::ANIMATION_CURVE_NODE, prop, i);
		 ++i)
	{
		Object* curve_node_layer = curve_node->resolveObjectLinkReverse(Object::Type::ANIMATION_LAYER);
		if (curve_node_layer == &layer) return curve_node;
	}
	return nullptr;
}


Matrix Object::evalLocal(const Vec3& translation, const Vec3& rotation) const
{
	Vec3 scaling = getLocalScaling();
	Vec3 rotation_pivot = getRotationPivot();
	Vec3 scaling_pivot = getScalingPivot();

	Matrix s = makeIdentity();
	s.m[0] = scaling.x;
	s.m[5] = scaling.y;
	s.m[10] = scaling.z;

	Matrix t = makeIdentity();
	setTranslation(translation, &t);

	Matrix r = getRotationMatrix(rotation);
	Matrix r_pre = getRotationMatrix(getPreRotation());
	Matrix r_post_inv = getRotationMatrix(getPostRotation());

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


IElement* Object::resolveProperty(const char* name) const
{
	const Element* props = findChild((const Element&)element, "Properties70");
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


Object* Object::resolveObjectLinkReverse(Object::Type type) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
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


int Object::resolveObjectLinkCount(Object::Type type) const
{
	int count = 0;
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj && obj->getType() == type) ++count;
		}
	}
	return count;
}


const IScene& Object::getScene() const
{
	return scene;
}


int Object::resolveObjectLinkCount() const
{
	int count = 0;
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
	for (auto& connection : scene.m_connections)
	{
		if (connection.to == id && connection.from != 0)
		{
			Object* obj = scene.m_object_map.find(connection.from)->second.object;
			if (obj)
			{
				++count;
			}
		}
	}
	return count;
}


Object* Object::resolveObjectLink(int idx) const
{
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
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
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
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
	u64 id = element.getFirstProperty() ? element.getFirstProperty()->getValue().toLong() : 0;
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
	Scene* scene = new Scene;
	scene->m_data.resize(size);
	memcpy(&scene->m_data[0], data, size);
	OptionalError<Element*> root = tokenize(&scene->m_data[0], size);
	if (root.isError())
	{
		delete scene;
		return nullptr;
	}
	assert(root.getValue());
	scene->m_root_element = root.getValue();
	parseTemplates(*root.getValue());
	parseConnections(*root.getValue(), scene);
	parseTakes(scene);
	parseObjects(*root.getValue(), scene);
	return scene;
}


const char* getError()
{
	return Error::s_message;
}


} // namespace ofbx
