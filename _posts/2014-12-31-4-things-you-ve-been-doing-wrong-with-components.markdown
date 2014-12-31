---
layout:     post
title:      4 things you've been doing wrong with components
date:       2014-12-31 19:53:00
summary:    Even though component-base design is a quite old concept in the game engine developm`ent, there are some things many programmers still get wrong. 
categories: components
comments: true
---

## 1. Not embracing all the options components offer.
Let's speek code:

{% highlight cpp %}
class BaseComponent
{
public:
	virtual ~BaseComponent() {}
	virtual int getType() = 0;
};

class GameObject
{
	...
	class MeshComponent* m_mesh_component;
	class ScriptComponent* m_script_component;
	Array<BaseComponent*> m_misc_components;
};
{% endhighlight %}

Yes, you are able to compose game objects from components at compile time, even at run time, but you are wasting precious memory, data locality is most likely very poor and things are not very well encapsulated.

## 2. Having *update* method in each component

{% highlight cpp %}
class BaseComponent
{
	... 
	virtual void update(float time_delta) = 0;
	...
};
...
for(auto object : objects)
{
	for(auto component : object->getComponents())
	{
		component->update(time_delta);
	}
}
{% endhighlight %}

A common game contains mostly static meshes. Those trees are moving in the wind, but it's done in a vertex shader. There is no need to call *update* on all of them. Do you want a slightly better solution? 

{% highlight cpp %}
for(auto component : active_component)
{
	component->update(time_delta);
}
{% endhighlight %}

Now we update only components that most likely need to be updated, however

- it can still be **too many virtual** function calls,
- components with the same type are probably not updated in a row, which can cause an instruction cachce miss.

Do we need to have *update* method in a component at all?

{% highlight cpp %}
class PhysicsSystem
{
...
	void update(float time_delta)
	{
		for(auto body : dynamic_bodies)
		{
			body->entity->setPosition(body->getPhysicsPosition());
		}
	}
};
...
	physics->update(time_delta);
	script->update(time_delta);
	renderer->update(time_delta);
...
{% endhighlight %}

No virtual calls, no unnecessary calls, happier users. 

## 3. Storing components in a game object

Since you do not have *update* method in a component now, is it still necessary to store components in a game object? Are connections between systems so tight, that every system needs to know about components of any other system? This is most likely not true. Dependencies between systems are usually a one way relation and system rarely depends on more than one other system.
An animaiton system probably needs to know about an rendering system to set some matrices, but the rendering system knows nothing about the animation system and none of them know anything about a script system.

{% highlight cpp %}
class AnimationScene
{
...
	void update(float time_delta)
	{
		for(auto animable : m_animables)
		{
			m_render_scene->setBoneMatrices(animable->m_mesh_instance, animable->m_matrices);
		}
	}
...
}
{% endhighlight %}

There is something that needs to know about every component in a game object - an editor. However since it's only an editor, it is not necessary to store them in the engine and waste precious memory even in a game.

## 4. Having component as a class

{% highlight cpp %}
struct Renderable : public BaseComponent
{
...
	int key;
	Mesh* mesh;
	Matrix transform;
	bool is_visible;
	BoundingVolume bounding_volume;
...
};
...
Array<Renderable> m_renderables;
...
for(auto& renderable : m_renderables)
{
	if(renderable.key == some_key) // not likely
	{
		foo(renderable);
	}
}
{% endhighlight %}

This is most common example of poor [data locality](http://gameprogrammingpatterns.com/data-locality.html). We read all data after *key*, even though quite often we need just a *key*. Straightforward solution is to split *Renderable* into two (or more) parts. This splitting should be based on access patterns.

{% highlight cpp %}
for(int i = 0; i < m_renderable_count; ++i)
{
	if(m_keys[i] == some_key)
	{
		foo(m_bounding_volumes[i]);
	}
}
{% endhighlight %}

Of course this refactor can lower maintainability and readability, but sometimes they must be sacrificed to improve performance

