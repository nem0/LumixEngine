#include "serializer.h"
#include "engine/math.h"
#include "engine/stream.h"
#include "engine/string.h"


namespace Lumix
{

	
static u64 asU64(double v)
{
	return *(u64*)&v;
}


static u32 asU32(float v)
{
	return *(u32*)&v;
}


static float asFloat(u32 v)
{
	return *(float*)&v;
}


static double asDouble(u64 v)
{
	return *(double*)&v;
}


void TextSerializer::write(const char* label, EntityPtr entity)
{
	blob << "#" << label << "\n\t" << entity.index << "\n";
}


void TextSerializer::write(const char* label, EntityRef entity)
{
	blob << "#" << label << "\n\t" << entity.index << "\n";
}


void TextSerializer::write(const char* label, const RigidTransform& value)
{
	blob << "#" << label << " (" << value.pos.x << ", " << value.pos.y << ", " << value.pos.z << ") "
		<< " (" << value.rot.x << ", " << value.rot.y << ", " << value.rot.z << ", " << value.rot.w << ")\n\t"
		<< asU64(value.pos.x) << "\n\t" << asU64(value.pos.y) << "\n\t" << asU64(value.pos.z) << "\n\t"
		<< asU32(value.rot.x) << "\n\t" << asU32(value.rot.y) << "\n\t" << asU32(value.rot.z) << "\n\t"
		<< asU32(value.rot.w) << "\n";
}


void TextSerializer::write(const char* label, const LocalRigidTransform& value)
{
	blob << "#" << label << " (" << value.pos.x << ", " << value.pos.y << ", " << value.pos.z << ") "
		<< " (" << value.rot.x << ", " << value.rot.y << ", " << value.rot.z << ", " << value.rot.w << ")\n\t"
		<< asU32(value.pos.x) << "\n\t" << asU32(value.pos.y) << "\n\t" << asU32(value.pos.z) << "\n\t"
		<< asU32(value.rot.x) << "\n\t" << asU32(value.rot.y) << "\n\t" << asU32(value.rot.z) << "\n\t"
		<< asU32(value.rot.w) << "\n";
}

void TextSerializer::write(const char* label, const Transform& value)
{
	blob << "#" << label << " (" << value.pos.x << ", " << value.pos.y << ", " << value.pos.z << ") "
		<< " (" << value.rot.x << ", " << value.rot.y << ", " << value.rot.z << ", " << value.rot.w << ") " << value.scale <<  "\n\t"
		<< asU64(value.pos.x) << "\n\t" << asU64(value.pos.y) << "\n\t" << asU64(value.pos.z) << "\n\t"
		<< asU32(value.rot.x) << "\n\t" << asU32(value.rot.y) << "\n\t" << asU32(value.rot.z) << "\n\t"
		<< asU32(value.rot.w) << "\n\t" << asU32(value.scale) << "\n";
}
void TextSerializer::write(const char* label, const Vec3& value)
{
	blob << "#" << label << " (" << value.x << ", " << value.y << ", " << value.z << ")\n\t" << asU32(value.x) << "\n\t"
		 << asU32(value.y) << "\n\t" << asU32(value.z) << "\n";
}

void TextSerializer::write(const char* label, const DVec3& value)
{
	blob << "#" << label << " (" << value.x << ", " << value.y << ", " << value.z << ")\n\t" << asU64(value.x) << "\n\t"
		 << asU64(value.y) << "\n\t" << asU64(value.z) << "\n";
}

void TextSerializer::write(const char* label, const IVec3& value)
{
	blob << "#" << label << "\n\t" << value.x << "\n\t" << value.y << "\n\t" << value.z << "\n";
}

void TextSerializer::write(const char* label, const Vec4& value)
{
	blob << "#" << label << " (" << value.x << ", " << value.y << ", " << value.z << ", " << value.w << ")\n\t"
		<< asU32(value.x) << "\n\t" << asU32(value.y) << "\n\t" << asU32(value.z) << "\n\t" << asU32(value.w) << "\n";
}

void TextSerializer::write(const char* label, const Quat& value)
{
	blob << "#" << label << " (" << value.x << ", " << value.y << ", " << value.z << ", " << value.w << ")\n\t"
		<< asU32(value.x) << "\n\t" << asU32(value.y) << "\n\t" << asU32(value.z) << "\n\t" << asU32(value.w) << "\n";
}

void TextSerializer::write(const char* label, float value)
{
	blob << "#" << label << " " << value << "\n\t" << asU32(value) << "\n";
}

void TextSerializer::write(const char* label, double value)
{
	blob << "#" << label << " " << value << "\n\t" << asU64(value) << "\n";
}

void TextSerializer::write(const char* label, bool value)
{
	blob << "#" << label << "\n\t" << (u32)value << "\n";
}

void TextSerializer::write(const char* label, const char* value)
{
	blob << "#" << label << "\n\t\"" << value << "\"\n";
}

void TextSerializer::write(const char* label, u32 value)
{
	blob << "#" << label << "\n\t" << value << "\n";
}

void TextSerializer::write(const char* label, u16 value)
{
	blob << "#" << label << "\n\t" << value << "\n";
}

void TextSerializer::write(const char* label, i64 value)
{
	blob << "#" << label << "\n\t" << value << "\n";
}

void TextSerializer::write(const char* label, u64 value)
{
	blob << "#" << label << "\n\t" << value << "\n";
}

void TextSerializer::write(const char* label, i32 value)
{
	blob << "#" << label << "\n\t" << value << "\n";
}

void TextSerializer::write(const char* label, i8 value)
{
	blob << "#" << label << "\n\t" << value << "\n";
}

void TextSerializer::write(const char* label, u8 value)
{
	blob << "#" << label << "\n\t" << value << "\n";
}


void TextDeserializer::read(Ref<EntityPtr> entity)
{
	read(Ref(entity->index));
}


void TextDeserializer::read(Ref<EntityRef> entity)
{
	read(Ref(entity->index));
}

void TextDeserializer::read(Ref<RigidTransform> value)
{
	skip();
	value->pos.x = asDouble(readU64());
	skip();
	value->pos.y = asDouble(readU64());
	skip();
	value->pos.z = asDouble(readU64());
	skip();
	value->rot.x = asFloat(readU32());
	skip();
	value->rot.y = asFloat(readU32());
	skip();
	value->rot.z = asFloat(readU32());
	skip();
	value->rot.w = asFloat(readU32());
}

void TextDeserializer::read(Ref<LocalRigidTransform> value)
{
	skip();
	value->pos.x = asFloat(readU32());
	skip();
	value->pos.y = asFloat(readU32());
	skip();
	value->pos.z = asFloat(readU32());
	skip();
	value->rot.x = asFloat(readU32());
	skip();
	value->rot.y = asFloat(readU32());
	skip();
	value->rot.z = asFloat(readU32());
	skip();
	value->rot.w = asFloat(readU32());
}


void TextDeserializer::read(Ref<Transform> value)
{
	skip();
	value->pos.x = asDouble(readU64());
	skip();
	value->pos.y = asDouble(readU64());
	skip();
	value->pos.z = asDouble(readU64());
	skip();
	value->rot.x = asFloat(readU32());
	skip();
	value->rot.y = asFloat(readU32());
	skip();
	value->rot.z = asFloat(readU32());
	skip();
	value->rot.w = asFloat(readU32());
	skip();
	value->scale = asFloat(readU32());
}

i32 TextDeserializer::readI32()
{
	char tmp[20];
	char* c = tmp;
	*c = blob.readChar();
	if (*c == '-')
	{
		++c;
		*c = blob.readChar();
	}
	while (*c >= '0' && *c <= '9' && (c - tmp) < lengthOf(tmp))
	{
		++c;
		*c = blob.readChar();
	}
	*c = 0;
	i32 value;
	fromCString(Span(tmp), Ref(value));
	return value;
}

void TextDeserializer::read(Ref<IVec3> value)
{
	skip();
	value->x = readI32();
	skip();
	value->y = readI32();
	skip();
	value->z = readI32();
}

void TextDeserializer::read(Ref<Vec3> value)
{
	skip();
	value->x = asFloat(readU32());
	skip();
	value->y = asFloat(readU32());
	skip();
	value->z = asFloat(readU32());
}


void TextDeserializer::read(Ref<DVec3> value)
{
	skip();
	value->x = asDouble(readU64());
	skip();
	value->y = asDouble(readU64());
	skip();
	value->z = asDouble(readU64());
}


void TextDeserializer::read(Ref<Vec4> value)
{
	skip();
	value->x = asFloat(readU32());
	skip();
	value->y = asFloat(readU32());
	skip();
	value->z = asFloat(readU32());
	skip();
	value->w = asFloat(readU32());
}


void TextDeserializer::read(Ref<Quat> value)
{
	skip();
	value->x = asFloat(readU32());
	skip();
	value->y = asFloat(readU32());
	skip();
	value->z = asFloat(readU32());
	skip();
	value->w = asFloat(readU32());
}


void TextDeserializer::read(Ref<float> value)
{
	skip();
	value = asFloat(readU32());
}


void TextDeserializer::read(Ref<double> value)
{
	skip();
	value = asDouble(readU64());
}


void TextDeserializer::read(Ref<bool> value)
{
	skip();
	value = readU32() != 0;
}


void TextDeserializer::read(Ref<u32> value)
{
	skip();
	value = readU32();
}


void TextDeserializer::read(Ref<u16> value)
{
	skip();
	char tmp[40];
	char* c = tmp;
	*c = blob.readChar();
	while (*c >= '0' && *c <= '9' && (c - tmp) < lengthOf(tmp))
	{
		++c;
		*c = blob.readChar();
	}
	*c = 0;
	fromCString(Span(tmp), value);
}


void TextDeserializer::read(Ref<u64> value)
{
	skip();
	char tmp[40];
	char* c = tmp;
	*c = blob.readChar();
	while (*c >= '0' && *c <= '9' && (c - tmp) < lengthOf(tmp))
	{
		++c;
		*c = blob.readChar();
	}
	*c = 0;
	fromCString(Span(tmp), value);
}


void TextDeserializer::read(Ref<i64> value)
{
	skip();
	char tmp[40];
	char* c = tmp;
	*c = blob.readChar();
	if (*c == '-')
	{
		++c;
		*c = blob.readChar();
	}
	while (*c >= '0' && *c <= '9' && (c - tmp) < lengthOf(tmp))
	{
		++c;
		*c = blob.readChar();
	}
	*c = 0;
	fromCString(Span(tmp), value);
}


void TextDeserializer::read(Ref<i32> value)
{
	skip();
	char tmp[20];
	char* c = tmp;
	*c = blob.readChar();
	if (*c == '-')
	{
		++c;
		*c = blob.readChar();
	}
	while (*c >= '0' && *c <= '9' && (c - tmp) < lengthOf(tmp))
	{
		++c;
		*c = blob.readChar();
	}
	*c = 0;
	fromCString(Span(tmp), value);
}


void TextDeserializer::read(Ref<u8> value)
{
	skip();
	value = (u8)readU32();
}


void TextDeserializer::read(Ref<i8> value)
{
	skip();
	value = (i8)readU32();
}


static int getStringLength(const InputMemoryStream& blob)
{
	u8* string_start = (u8*)blob.getData() + blob.getPosition();
	u8* c = string_start;
	u8* end = (u8*)blob.getData() + blob.size();

	if (*c != '"') return 0;
	++c;

	while(*c != '"' && c != end)
	{
		++c;
	}
	if (*c != '"') return 0;
	return int(c - string_start) - 1;
}


void TextDeserializer::read(Ref<String> value)
{
	skip();
	value->resize(getStringLength(blob) + 1);

	u8 c = blob.readChar();
	ASSERT(c == '"');
	
	blob.read(value->getData(), value->length());
	value->getData()[value->length()] = '\0';
	c = blob.readChar();
	ASSERT(c == '"');
}


void TextDeserializer::read(Span<char> value)
{
	skip();
	u8 c = blob.readChar();
	ASSERT(c == '"');
	char* out = value.m_begin;
	*out = blob.readChar();
	while (*out != '"' && out - value.m_begin < value.length() - 1)
	{
		++out;
		*out = blob.readChar();
	}
	ASSERT(*out == '"');
	*out = 0;
}


u32 TextDeserializer::readU32()
{
	char tmp[20];
	char* c = tmp;
	*c = blob.readChar();
	while (*c >= '0' && *c <= '9' && (c - tmp) < lengthOf(tmp))
	{
		++c;
		*c = blob.readChar();
	}
	*c = 0;
	u32 v;
	fromCString(Span<const char>(tmp), Ref(v));
	return v;
}

u64 TextDeserializer::readU64()
{
	char tmp[40];
	char* c = tmp;
	*c = blob.readChar();
	while (*c >= '0' && *c <= '9' && (c - tmp) < lengthOf(tmp))
	{
		++c;
		*c = blob.readChar();
	}
	*c = 0;
	u64 v;
	fromCString(Span(tmp), Ref(v));
	return v;
}

void TextDeserializer::skip()
{
	u8 c = blob.readChar();
	if (c == '#')
		while (blob.readChar() != '\n')
			;
	if (c == '\t') return;
	while (blob.readChar() != '\t')
		;
}
}