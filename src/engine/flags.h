#pragma once


namespace Lumix
{


template <typename Enum, typename Base>
struct Flags
{
	void set(Enum value)
	{
		base |= (Base)value;
	}

	void unset(Enum value)
	{
		base &= ~(Base)value;
	}

	bool isSet(Enum value) const
	{
		return base & (Base)value;
	}

	Base base = 0;
};


} // namespace Lumix