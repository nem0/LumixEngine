#pragma once


namespace Lumix
{


template <typename Enum, typename Base>
struct FlagSet
{
	void clear() { base = 0; }
	void set(Enum value, bool on) { if (on) set(value); else unset(value); }
	void set(Enum value) { base |= (Base)value; }
	void unset(Enum value) { base &= ~(Base)value; }
	bool isSet(Enum value) const { return base & (Base)value; }

private:
	Base base = 0;
};


} // namespace Lumix