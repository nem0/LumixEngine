#pragma once

#include "universe\universe.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/delegate.h"
#include "core/string.h"


namespace Lumix
{


struct Vec3;
class Blob;


class IPropertyDescriptor
{
	public:
		enum Type
		{
			RESOURCE = 0,
			FILE,
			DECIMAL,
			BOOL,
			VEC3,
			INTEGER,
			STRING,
			ARRAY,
			COLOR
		};

	public:
		virtual ~IPropertyDescriptor() {}

		virtual void set(Component cmp, Blob& stream) const = 0;
		virtual void get(Component cmp, Blob& stream) const = 0;
		virtual void set(Component cmp, int index, Blob& stream) const = 0;
		virtual void get(Component cmp, int index, Blob& stream) const = 0;

		Type getType() const { return m_type; }
		uint32_t getNameHash() const { return m_name_hash; }
		const char* getName() const { return m_name.c_str(); }
		void setName(const char* name) { m_name = name; m_name_hash = crc32(name); }
		void addChild(IPropertyDescriptor* child) { m_children.push(child); }
		const Array<IPropertyDescriptor*>& getChildren() const { return m_children; }
		Array<IPropertyDescriptor*>& getChildren() { return m_children; }

	protected:
		uint32_t m_name_hash;
		string m_name;
		Type m_type;
		Array<IPropertyDescriptor*> m_children;
};


class IIntPropertyDescriptor : public IPropertyDescriptor
{
	public:
		IIntPropertyDescriptor()
		{
			m_min = INT_MIN;
			m_max = INT_MAX;
		}

		void setLimit(int min, int max) { m_min = min; m_max = max; }
		int getMin() const { return m_min; }
		int getMax() const { return m_max; }

	private:
		int m_min;
		int m_max;
};


template <class S>
class IntArrayObjectDescriptor : public IIntPropertyDescriptor
{
	public:
		typedef int (S::*IntegerGetter)(Component, int);
		typedef void (S::*IntegerSetter)(Component, int, int);

	public:
		IntArrayObjectDescriptor(const char* name, IntegerGetter _getter, IntegerSetter _setter) 
		{
			setName(name);
			m_integer_getter = _getter;
			m_integer_setter = _setter;
			m_type = INTEGER;
		}


		virtual void set(Component cmp, int index, Blob& stream) const override
		{
			int32_t i;
			stream.read(&i, sizeof(i));
			(static_cast<S*>(cmp.scene)->*m_integer_setter)(cmp, index, i);
		}


		virtual void get(Component cmp, int index, Blob& stream) const override
		{
			int32_t i = (static_cast<S*>(cmp.scene)->*m_integer_getter)(cmp, index);
			int len = sizeof(i);
			stream.write(&i, len);
		}


		virtual void set(Component, Blob&) const {};
		virtual void get(Component, Blob&) const {};

	private:
		IntegerGetter m_integer_getter;
		IntegerSetter m_integer_setter;
};


template <class S>
class BoolArrayObjectDescriptor : public IPropertyDescriptor
{
	public:
		typedef void (S::*Getter)(Component, int, int&);
		typedef void (S::*Setter)(Component, int, const int&);

	public:
		BoolArrayObjectDescriptor(const char* name, Getter _getter, Setter _setter)
		{
			setName(name);
			m_getter = _getter;
			m_setter = _setter;
			m_type = BOOL;
		}


		virtual void set(Component cmp, int index, Blob& stream) const override
		{
			bool b;
			stream.read(&b, sizeof(b));
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, index, b);
		}


		virtual void get(Component cmp, int index, Blob& stream) const override
		{
			bool b;
			(static_cast<S*>(cmp.scene)->*m_getter)(cmp, index, b);
			stream.write(&b, sizeof(b));
		}


		virtual void set(Component, Blob&) const { ASSERT(false); };
		virtual void get(Component, Blob&) const { ASSERT(false); };

	private:
		Getter m_getter;
		Setter m_setter;
};


template <class S>
class DecimalArrayObjectDescriptor : public IPropertyDescriptor
{
	public:
		typedef float (S::*Getter)(Component, int);
		typedef void (S::*Setter)(Component, int, float);

	public:
		DecimalArrayObjectDescriptor(const char* name, Getter _getter, Setter _setter)
		{
			setName(name);
			m_getter = _getter;
			m_setter = _setter;
			m_type = DECIMAL;
		}


		virtual void set(Component cmp, int index, Blob& stream) const override
		{
			float f;
			stream.read(&f, sizeof(f));
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, index, f);
		}


		virtual void get(Component cmp, int index, Blob& stream) const override
		{
			float f = (static_cast<S*>(cmp.scene)->*m_getter)(cmp, index);
			stream.write(&f, sizeof(f));
		}


		virtual void set(Component, Blob&) const { ASSERT(false); };
		virtual void get(Component, Blob&) const { ASSERT(false); };

	private:
		Getter m_getter;
		Setter m_setter;
};


template <class S>
class StringArrayObjectDescriptor : public IPropertyDescriptor
{
	public:
		typedef void (S::*Getter)(Component, int, string&);
		typedef void (S::*Setter)(Component, int, const string&);

	public:
		StringArrayObjectDescriptor(const char* name, Getter _getter, Setter _setter)
		{
			setName(name);
			m_getter = _getter;
			m_setter = _setter;
			m_type = STRING;
		}


		virtual void set(Component cmp, int index, Blob& stream) const override
		{
			char tmp[300];
			char* c = tmp;
			do
			{
				stream.read(c, 1);
				++c;
			}
			while (*(c - 1) && (c - 1) - tmp < 300);
			string s((char*)tmp);
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, index, s);
		}


		virtual void get(Component cmp, int index, Blob& stream) const override
		{
			string value;
			(static_cast<S*>(cmp.scene)->*m_getter)(cmp, index, value);
			int len = value.length() + 1;
			stream.write(value.c_str(), len);
		}


		virtual void set(Component, Blob&) const { ASSERT(false); };
		virtual void get(Component, Blob&) const { ASSERT(false); };

	private:
		Getter m_getter;
		Setter m_setter;
};


template <class S>
class FileArrayObjectDescriptor : public StringArrayObjectDescriptor<S>, public IFilePropertyDescriptor
{
	public:
		FileArrayObjectDescriptor(const char* name, Getter getter, Setter setter, const char* file_type)
			: StringArrayObjectDescriptor(name, getter, setter)
			, m_file_type(file_type)
		{
			m_type = IPropertyDescriptor::FILE;
		}

		virtual const char* getFileType() override
		{
			return m_file_type.c_str();
		}

	private:
		string m_file_type;
};


template <class S>
class ResourceArrayObjectDescriptor : public FileArrayObjectDescriptor<S>
{
	public:
		ResourceArrayObjectDescriptor(const char* name, Getter getter, Setter setter, const char* file_type)
			: FileArrayObjectDescriptor(name, getter, setter, file_type)
		{
			m_type = IPropertyDescriptor::RESOURCE;
		}
};



template <class S>
class Vec3ArrayObjectDescriptor : public IPropertyDescriptor
{
	public:
		typedef Vec3 (S::*Getter)(Component, int);
		typedef void (S::*Setter)(Component, int, const Vec3&);

	public:
		Vec3ArrayObjectDescriptor(const char* name, Getter _getter, Setter _setter) { setName(name); m_vec3_getter = _getter; m_vec3_setter = _setter; m_type = VEC3; }
		
		
		virtual void set(Component cmp, int index, Blob& stream) const override
		{
			Vec3 v;
			stream.read(&v, sizeof(v));
			(static_cast<S*>(cmp.scene)->*m_vec3_setter)(cmp, index, v);
		}


		virtual void get(Component cmp, int index, Blob& stream) const override
		{
			Vec3 v = (static_cast<S*>(cmp.scene)->*m_vec3_getter)(cmp, index);
			len = sizeof(v);
			stream.write(&v, len);
		}


		virtual void set(Component, Blob&) const {};
		virtual void get(Component, Blob&) const {};

	private:
		Getter m_getter;
		Setter m_setter;
};


class IArrayDescriptor : public IPropertyDescriptor
{
	public:
		virtual void removeArrayItem(Component cmp, int index) const = 0;
		virtual void addArrayItem(Component cmp, int index) const = 0;
		virtual int getCount(Component cmp) const = 0;
};


template <class S>
class ArrayDescriptor : public IArrayDescriptor
{
	public:
		typedef int (S::*Counter)(Component);
		typedef void (S::*Adder)(Component, int);
		typedef void (S::*Remover)(Component, int);

	public:
		ArrayDescriptor(const char* name, Counter counter, Adder adder, Remover remover) { setName(name); m_type = ARRAY; m_counter = counter; m_adder = adder; m_remover = remover; }
		~ArrayDescriptor()
		{
			for(int i = 0; i < m_children.size(); ++i)
			{
				LUMIX_DELETE(m_children[i]);
			}
		}


		virtual void set(Component cmp, Blob& stream) const override
		{
			int count;
			stream.read(count);
			while (getCount(cmp) < count)
			{
				addArrayItem(cmp, -1);
			}
			while (getCount(cmp) > count)
			{
				removeArrayItem(cmp, getCount(cmp) - 1);
			}
			for (int i = 0; i < count; ++i)
			{
				for (int j = 0, cj = getChildren().size(); j < cj; ++j)
				{
					getChildren()[j]->set(cmp, i, stream);
				}
			}
		}


		virtual void get(Component cmp, Blob& stream) const override
		{
			int count = getCount(cmp);
			stream.write(count);
			for (int i = 0; i < count; ++i)
			{
				for (int j = 0, cj = getChildren().size(); j < cj; ++j)
				{
					getChildren()[j]->get(cmp, i, stream);
				}
			}
		}


		virtual void set(Component, int, Blob&) const override { ASSERT(false); };
		virtual void get(Component, int, Blob&) const override { ASSERT(false); };

		virtual int getCount(Component cmp) const override { return (static_cast<S*>(cmp.scene)->*m_counter)(cmp); }
		virtual void addArrayItem(Component cmp, int index) const override { (static_cast<S*>(cmp.scene)->*m_adder)(cmp, index); }
		virtual void removeArrayItem(Component cmp, int index) const override { (static_cast<S*>(cmp.scene)->*m_remover)(cmp, index); }

	private:
		Counter m_counter;
		Adder m_adder;
		Remover m_remover;
};


template <class S>
class IntPropertyDescriptor : public IIntPropertyDescriptor
{
	public:
		typedef int (S::*IntegerGetter)(Component);
		typedef void (S::*IntegerSetter)(Component, int);

	public:
		IntPropertyDescriptor(const char* name, IntegerGetter _getter, IntegerSetter _setter) { setName(name); m_integer_getter = _getter; m_integer_setter = _setter; m_type = INTEGER; }


		virtual void set(Component cmp, Blob& stream) const override
		{
			int32_t i;
			stream.read(&i, sizeof(i));
			(static_cast<S*>(cmp.scene)->*m_integer_setter)(cmp, i);
		}


		virtual void get(Component cmp, Blob& stream) const override
		{
			int32_t i = (static_cast<S*>(cmp.scene)->*m_integer_getter)(cmp);
			len = sizeof(i);
			stream.write(&i, len);
		}


		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream); };

	private:
		IntegerGetter m_integer_getter;
		IntegerSetter m_integer_setter;
};


template <class S>
class StringPropertyDescriptor : public IPropertyDescriptor
{
	public:
		typedef void (S::*Getter)(Component, string&);
		typedef void (S::*Setter)(Component, const string&);

	public:
		StringPropertyDescriptor(const char* name, Getter getter, Setter setter)
		{
			setName(name);
			m_getter = getter;
			m_setter = setter;
			m_type = IPropertyDescriptor::STRING;
		}


		virtual void set(Component cmp, Blob& stream) const override
		{
			char tmp[300];
			char* c = tmp;
			do
			{
				stream.read(c, 1);
				++c;
			}
			while (*(c - 1) && (c - 1) - tmp < 300);
			string s((char*)tmp);
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, s);
		}


		virtual void get(Component cmp, Blob& stream) const override
		{
			string value;
			(static_cast<S*>(cmp.scene)->*m_getter)(cmp, value);
			int len = value.length() + 1;
			stream.write(value.c_str(), len);
		}


		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream); };

	private:
		Getter m_getter;
		Setter m_setter;
};


template <class S>
class BoolPropertyDescriptor : public IPropertyDescriptor
{
	public:
		typedef bool (S::*Getter)(Component);
		typedef void (S::*Setter)(Component, bool);

	public:
		BoolPropertyDescriptor(const char* name, Getter getter, Setter setter)
		{
			setName(name);
			m_getter = getter;
			m_setter = setter;
			m_type = IPropertyDescriptor::BOOL;
		}


		virtual void set(Component cmp, Blob& stream) const override
		{
			bool b;
			stream.read(&b, sizeof(b));
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, b);
		}


		virtual void get(Component cmp, Blob& stream) const override
		{
			bool b = (static_cast<S*>(cmp.scene)->*m_getter)(cmp);
			int len = sizeof(b);
			stream.write(&b, len);
		}


		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream); };

	private:
		Getter m_getter;
		Setter m_setter;
};


template <class S>
class Vec3PropertyDescriptor : public IPropertyDescriptor
{
	public:
		typedef Vec3 (S::*Getter)(Component);
		typedef void (S::*Setter)(Component, const Vec3&);

	public:
		Vec3PropertyDescriptor(const char* name, Getter getter, Setter setter)
		{
			setName(name);
			m_getter = getter;
			m_setter = setter;
			m_type = IPropertyDescriptor::VEC3;
		}


		virtual void set(Component cmp, Blob& stream) const override
		{
			Vec3 v;
			stream.read(&v, sizeof(v));
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, v);
		}


		virtual void get(Component cmp, Blob& stream) const override
		{
			Vec3 v = (static_cast<S*>(cmp.scene)->*m_getter)(cmp);
			int len = sizeof(v);
			stream.write(&v, len);
		}


		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream); };

	private:
		Getter m_getter;
		Setter m_setter;
};


class IFilePropertyDescriptor
{
	public:
		virtual const char* getFileType() = 0;
};


template <class T>
class FilePropertyDescriptor : public StringPropertyDescriptor<T>, public IFilePropertyDescriptor
{
	public:
		FilePropertyDescriptor(const char* name, Getter getter, Setter setter, const char* file_type)
			: StringPropertyDescriptor(name, getter, setter)
			, m_file_type(file_type)
		{
			m_type = IPropertyDescriptor::FILE;
		}

		virtual const char* getFileType() override
		{
			return m_file_type.c_str();
		}

	private:
		string m_file_type;
};


template <class T>
class ResourcePropertyDescriptor : public FilePropertyDescriptor<T>
{
	public:
		ResourcePropertyDescriptor(const char* name, Getter getter, Setter setter, const char* file_type)
			: FilePropertyDescriptor(name, getter, setter, file_type)
		{
			m_type = IPropertyDescriptor::RESOURCE;
		}
};



template <class S>
class DecimalPropertyDescriptor : public IPropertyDescriptor
{
	public:
		typedef float (S::*Getter)(Component);
		typedef void (S::*Setter)(Component, float);

	public:
		DecimalPropertyDescriptor(const char* name, Getter _getter, Setter _setter) { setName(name); m_getter = _getter; m_setter = _setter; m_type = DECIMAL; }
		
		
		virtual void set(Component cmp, Blob& stream) const override
		{
			float f;
			stream.read(&f, sizeof(f));
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, f);
		}


		virtual void get(Component cmp, Blob& stream) const override
		{
			float f = (static_cast<S*>(cmp.scene)->*m_getter)(cmp);
			int len = sizeof(f);
			stream.write(&f, len);
		}


		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream);};

	private:
		Getter m_getter;
		Setter m_setter;
};


template <class S>
class ColorPropertyDescriptor : public IPropertyDescriptor
{
	public:
		typedef Vec4 (S::*Getter)(Component);
		typedef void (S::*Setter)(Component, const Vec4&);

	public:
		ColorPropertyDescriptor(const char* name, Getter _getter, Setter _setter) { setName(name); m_getter = _getter; m_setter = _setter; m_type = COLOR; }
		
		
		virtual void set(Component cmp, Blob& stream) const override
		{
			Vec4 f;
			stream.read(&f, sizeof(f));
			(static_cast<S*>(cmp.scene)->*m_setter)(cmp, f);
		}


		virtual void get(Component cmp, Blob& stream) const override
		{
			Vec4 f = (static_cast<S*>(cmp.scene)->*m_getter)(cmp);
			int len = sizeof(f);
			stream.write(&f, len);
		}


		virtual void set(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); set(cmp, stream); };
		virtual void get(Component cmp, int index, Blob& stream) const override { ASSERT(index == -1); get(cmp, stream);};

	private:
		Getter m_getter;
		Setter m_setter;
};


} // !namespace Lumix
