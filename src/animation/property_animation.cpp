#include "animation/property_animation.h"
#include "core/allocator.h"
#include "core/log.h"
#include "core/stream.h"
#include "core/tokenizer.h"
#include "engine/reflection.h"


namespace Lumix {


const ResourceType PropertyAnimation::TYPE("property_animation");

PropertyAnimation::PropertyAnimation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, fps(30)
	, curves(allocator)
{
}


PropertyAnimation::Curve& PropertyAnimation::addCurve()
{
	return curves.emplace(m_allocator);
}


void PropertyAnimation::deserialize(InputMemoryStream& blob) {
	bool res = load(Span((const u8*)blob.getData(), (u32)blob.size()));
	ASSERT(res);
}

void PropertyAnimation::serialize(OutputMemoryStream& blob) {
	ASSERT(isReady());

	for (Curve& curve : curves) {
		blob << "{\n";
		blob << "\t component = \"" << reflection::getComponent(curve.cmp_type)->name << "\",\n";
		blob << "\t property = \"" << curve.property->name << "\",\n";
		blob << "\tkeyframes = [\n";
		for (int i = 0; i < curve.frames.size(); ++i) {
			if (i != 0) blob << ", ";
			blob << curve.frames[i];
		}
		blob << "],\n";
		blob << "\tvalues = [\n";
		for (int i = 0; i < curve.values.size(); ++i) {
			if (i != 0) blob << ", ";
			blob << curve.values[i];
		}
		blob << "]\n},\n\n";
	}
}

template <typename T>
static bool consumeNumberArray(Tokenizer& tokenizer, Array<T>& array) {
	for (;;) {
		Tokenizer::Token token = tokenizer.nextToken();
		if (!token) return false;
		if (token == "]") return true;
		T value;
		if (!fromCString(token.value, value)) {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected a number, got ", token.value);
			tokenizer.logErrorPosition(token.value.begin);
			return false;
		}
		array.push(value);
		token = tokenizer.nextToken();
		if (!token) return false;
		if (token == "]") return true;
		if (token != ",") {
			logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',' or ']', got ", token.value);
			tokenizer.logErrorPosition(token.value.begin);
			return false;
		}
	}
}

/*
	{
		component = "gui_rect",
		property = "Top Points",
		keyframes = [ 0, 20, 40],
		values = [ 0.000000, 100.000000, 0.000000 ]
	},
	{
		...
*/
bool PropertyAnimation::load(Span<const u8> mem) {
	Tokenizer tokenizer(mem, getPath().c_str());
	for (;;) {
		Tokenizer::Token token = tokenizer.tryNextToken();
		switch (token.type) {
			case Tokenizer::Token::EOF: return true;
			case Tokenizer::Token::ERROR: return false;
			case Tokenizer::Token::SYMBOL:
				if (!equalStrings(token.value, "{")) {
					logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected '{', got ", token.value);
					tokenizer.logErrorPosition(token.value.begin);

					return false;
				}
				break;
			default:
				logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected '{', got ", token.value);
				tokenizer.logErrorPosition(token.value.begin);
				return false;
		}

		// single curve
		Curve curve(m_allocator);
		for (;;) {
			Tokenizer::Token key = tokenizer.nextToken();
			if (!key) return false;
			if (key == "}") {
				curves.push(static_cast<Curve&&>(curve));
				continue;
			}
			
			if (!tokenizer.consume("=")) return false;

			if (key == "component") {
				StringView value;
				if (!tokenizer.consume(value)) return false;
				curve.cmp_type = reflection::getComponentType(value);
			}
			else if (key == "property") {
				StringView value;
				if (!tokenizer.consume(value)) return false;
				curve.property = static_cast<const reflection::Property<float>*>(reflection::getProperty(curve.cmp_type, value));
			}
			else if (key == "keyframes") {
				if (!tokenizer.consume("[")) return false;
				if (!consumeNumberArray(tokenizer, curve.frames)) return false;
			}
			else if (key == "values") {
				if (!tokenizer.consume("[")) return false;
				if (!consumeNumberArray(tokenizer, curve.values)) return false;
			}
			else {
				logError(tokenizer.filename, "(", tokenizer.getLine(), "): Unknown identifier ", key.value);
				tokenizer.logErrorPosition(key.value.begin);
				return false;
			}

			Tokenizer::Token next = tokenizer.nextToken();
			if (!next) return false;
			if (next == "}") {
				curves.push(static_cast<Curve&&>(curve));
				break;
			}
			if (next != ",") {
				logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',' or '}', got ", next.value);
				tokenizer.logErrorPosition(next.value.begin);
				return false;
			}
		}
		token = tokenizer.tryNextToken();
		switch (token.type) {
			case Tokenizer::Token::EOF: return true;
			case Tokenizer::Token::ERROR: return false;
			case Tokenizer::Token::SYMBOL:
				if (!equalStrings(token.value, ",")) {
					logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',', got ", token.value);
					tokenizer.logErrorPosition(token.value.begin);
					return false;
				}
				break;
			default:
				logError(tokenizer.filename, "(", tokenizer.getLine(), "): Expected ',', got ", token.value);
				tokenizer.logErrorPosition(token.value.begin);
				return false;
		}


	}
	return true;
}


void PropertyAnimation::unload()
{
	curves.clear();
}


} // namespace Lumix
