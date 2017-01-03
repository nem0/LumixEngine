#include "serializer.h"
#include "engine/blob.h"
#include "engine/matrix.h"


namespace Lumix
{


static u32 asU32(float v)
{
	return *(u32*)&v;
}


static float asFloat(u32 v)
{
	return *(float*)&v;
}


void TextSerializer::write(const char* label, Entity entity)
{
	EntityGUID guid = entity_map.get(entity);
	blob << "#" << label << "\n\t" << guid.value << "\n";
}

void TextSerializer::write(const char* label, ComponentHandle value)
{
	blob << "#" << label << "\n\t" << value.index << "\n";
}

void TextSerializer::write(const char* label, const Transform& value)
{
	blob << "#" << label << " (" << value.pos.x << ", " << value.pos.y << ", " << value.pos.z << ") "
		 << " (" << value.rot.x << ", " << value.rot.y << ", " << value.rot.z << ", " << value.rot.w << ")\n\t"
		 << asU32(value.pos.x) << "\n\t" << asU32(value.pos.y) << "\n\t" << asU32(value.pos.z) << "\n\t"
		 << asU32(value.rot.x) << "\n\t" << asU32(value.rot.y) << "\n\t" << asU32(value.rot.z) << "\n\t"
		 << asU32(value.rot.w) << "\n";
}

void TextSerializer::write(const char* label, const Vec3& value)
{
	blob << "#" << label << " (" << value.x << ", " << value.y << ", " << value.z << ")\n\t" << asU32(value.x) << "\n\t"
		 << asU32(value.y) << "\n\t" << asU32(value.z) << "\n";
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


void TextDeserializer::read(Entity* entity)
{
	EntityGUID guid;
	read(&guid.value);
	*entity = entity_map.get(guid);
}


void TextDeserializer::read(Transform* value)
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


void TextDeserializer::read(Vec3* value)
{
	skip();
	value->x = asFloat(readU32());
	skip();
	value->y = asFloat(readU32());
	skip();
	value->z = asFloat(readU32());
}


void TextDeserializer::read(Vec4* value)
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


void TextDeserializer::read(Quat* value)
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


void TextDeserializer::read(ComponentHandle* value)
{
	skip();
	value->index = readU32();
}


void TextDeserializer::read(float* value)
{
	skip();
	*value = asFloat(readU32());
}


void TextDeserializer::read(bool* value)
{
	skip();
	*value = readU32() != 0;
}


void TextDeserializer::read(u32* value)
{
	skip();
	*value = readU32();
}


void TextDeserializer::read(u64* value)
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
	fromCString(tmp, lengthOf(tmp), value);
}


void TextDeserializer::read(i64* value)
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
	fromCString(tmp, lengthOf(tmp), value);
}


void TextDeserializer::read(i32* value)
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
	fromCString(tmp, lengthOf(tmp), value);
}


void TextDeserializer::read(u8* value)
{
	skip();
	*value = (u8)readU32();
}


void TextDeserializer::read(i8* value)
{
	skip();
	*value = (i8)readU32();
}


void TextDeserializer::read(char* value, int max_size)
{
	skip();
	u8 c = blob.readChar();
	ASSERT(c == '"');
	char* out = value;
	*out = blob.readChar();
	while (*out != '"' && out - value < max_size - 1)
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
	fromCString(tmp, lengthOf(tmp), &v);
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