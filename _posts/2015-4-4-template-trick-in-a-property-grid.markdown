---
layout:     post
title:      Template trick in a property grid
date:       2015-4-4 12:21:00
summary:    Somehow a property grid is the most often refactored part of my code. I want it to be as useful as the one in Unity and at the same time very easy for programmers to extend. The second condition is hardly possible without templates. 
categories: components
comments: true
---

Yet again I am rewriting the property grid. I want it to have very fluent API. I mean something like this:

{% highlight cpp %}
	auto object = model->object("Material", material);
	object
		.property("Alpha cutout", &Material::isAlphaCutout, &Material::enableAlphaCutout)
		.property("Alpha to coverage", &Material::isAlphaToCoverage, &Material::enableAlphaToCoverage)
		.property("Backface culling", &Material::isBackfaceCulling, &Material::enableBackfaceCulling)
		.property("Shadow receiver", &Material::isShadowReceiver, &Material::enableShadowReceiving)
		.property("Z test", &Material::isZTest, &Material::enableZTest)
		.property("Shader", [](Material* material) -> const char* { return material->getShader()->getPath().c_str(); });
	object.array("Textures", material->getTextureCount(), &Material::getTexture, [](Lumix::Texture* texture) -> const char* { return texture->getPath().c_str(); })
		.property("Width", &Lumix::Texture::getWidth)
		.property("Height", &Lumix::Texture::getHeight)
		.property("Bytes per pixel", &Lumix::Texture::getBytesPerPixel);
{% endhighlight %}

As you can see, a property must have a getter and it can have a setter. Most of time they are just pointers to member functions. However sometimes I want to use simple functions or lambdas. These two options differ in signature:

{% highlight cpp %}
template <typename T>
class Object
{
	template <typename R>
	Object<T>& property(const char* name, R (T::*getter)());
	
	template <typename R>
	Object<T>& property(const char* name, R (*getter)(T*));
};                                               
{% endhighlight %}

This seems to be pretty straightforward, however it does not work with lambdas passed directly:

{% highlight cpp %}
object.property("Shader", [](Material* material) -> const char* { return material->getShader()->getPath().c_str(); });
// could not deduce template argument for 'R (__cdecl *)(T *)'
{% endhighlight %}

A possible solution would be:

{% highlight cpp %}
template <typename T>
class Object
{
	template <typename Getter>
	Object<T>& propertyFunction(const char* name, Getter getter);
	
	template <typename Getter>
	Object<T>& propertyMethod(const char* name, Getter getter);
};                     

object
	.propertyMethod("Z test", &Material::isZTest, &Material::enableZTest)
	.propertyFunction("Shader", [](Material* material) -> const char* { return material->getShader()->getPath().c_str(); });
{% endhighlight %}

As you can see this is not as fluent as I want it to be. This is where a ugly template trick come in. The only way I found out how to make it better is based on the fact that the function has one argument while the method has none.

{% highlight cpp %}  
	template <typename T>
	struct FunctionTraits : public FunctionTraits<decltype(&T::operator())>
	{};

	template <typename ClassType, typename ReturnType, typename... Args>
	struct FunctionTraits<ReturnType(ClassType::*)(Args...) const>
	{
		enum { arity = sizeof...(Args) };
	};

	template<typename Functor, size_t NArgs, typename Ret>
	struct CountArg : std::enable_if<FunctionTraits<Functor>::arity == NArgs, Ret>
	{};

	template <typename Getter>
	typename CountArg<Getter, 1, Object>::type property(QString name, Getter getter) { ... }
	
	template <typename Getter>
	typename CountArg<Getter, 0, Object>::type property(QString name, Getter getter) { ... }
{% endhighlight %}
