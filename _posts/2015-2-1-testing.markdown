---
layout:     post
title:      Testing
date:       2015-1-26 21:42:00
summary:    I've just finished a cool new feature in my game editor, but some old feature is not working anymore...
categories: editor
comments: true
---

## 1. Why testing

I've encounter this problem many times before, especially when working on some project alone. I am working on a new feature in my editor and everything seems to be going smooth. That's until I find out that some completely different feature is broken. Nobody but me may have noticed this defect because the editor is not yet widely used or because the feature itself is very rarely used, but one of the main components may be affected too. USers remember well when an application is crashing or not working properly. It's easy to lose users' trust but it's hard to gain one. 

This is where tests come into play. They determine whether some feature is fit for use or not. Therefore when I create a new feature, change some old one or refactor something and tests are not failing, I can sleep better knowing I most likely did not break anything.

## 2. Testing in Game Development

Just to make things clear we are talking mostly about automated testing but in the end we will talk a little about other types of testing. 

Most game companies I know of do not use any kind of automated tests. Some of them use unit tests and only a few ones are doing some advanced testing. This is getting better day by day with engines like [Unity](http://blogs.unity3d.com/2013/12/18/unity-test-tools-released/), [Unreal](https://docs.unrealengine.com/latest/INT/Programming/Automation/index.html), or [CryEngine](http://www.crytek.com/cryengine/cryengine3/presentations/aaa-automated-testing-for-aaa-games) taking software testing seriously. 

Game developers who do not use automated testing tend to argue that game development is very different from other types of software. It is not so simple to test correct rendering or property GUI functionality, but using automated testing might be worth the time it takes to implement them. It is quite common that [a bugfix is a cause of a new bug](http://opera.ucsd.edu/paper/fse11.pdf). 

## 3. Unit tests

Every programmer should know about [unit tests](http://en.wikipedia.org/wiki/Unit_testing). They are the most basic type of automated testing. They are the most commonly used tests because they are easy to implement. It should not take long for a programmer to write a test for a function, returning the length of a vector. Certainly you may think that your math library is correct, but I know AAA games shipped with trivial bugs in their math library.

## 4. Static analysis

While writing about unit tests, I realized that issues found by these tests are sometimes the same static analysis find. Bug in the following code could be found by unit testing as well as static analysis:

{% highlight cpp %}

void Vec3::set(float x, float y, float z)
{
	this->x = x;
	this->y = y;
	this->z = z;
}

void Vec4::set(float x, float y, float z, float w)
{
	this->x = x;
	this->y = y;
	this->z = z;
	this->z = w;
}

{% endhighlight %}

Even though static analysis is not automated testing it's without doubt a very good tool to improve software quality.

## 5. Rendering

How do you test rendering? Simple, you render something. This way you immediately find out whether the renderer is crashing. Naturally, crashes are not the only bugs we want to catch as the renderer can not crash and still render nothing or render objects incorrectly. So we have to make a screenshot of a correctly rendered scene and compare it with our test rendering. One may argue that the pictures are not the same, because of different platforms, some floating point rounding or something else. It can be easily solved by looking for less than 100% similarity. In my engine I test renderer this way and it works like a charm.

## 6. Editor

I know of nobody who test editors but I break some editor functionality so often I eventually had to implement some kind of automated testing for editor. A way to test an editor may not come to your mind at first, but it is actually quite simple. I implemented one approach to editor testing and plan to have another procedure and I am sure there are many means to test an editor.

I plan to record all the GUI inputs and replay them. My editor uses QT and it should be quite easy to record all the user actions ([QInputEvent](http://qt-project.org/doc/qt-4.8/qinputevent.html)) and emit them again. There are even [some libraries for testing QT applications](http://doc.qt.digia.com/qq/qq16-testing.html)

What is more I've already implemented another way to test my editor. Because of the redo/undo functionality and network editing, all the editor commands are already encapsulated in objects. It was plain simple to add a serialize and deserialize methods:

{% highlight cpp %}
class CreateTemplateCommand : public IEditorCommand
{
	public:
		virtual void serialize(JsonSerializer& serializer) override
		{
			serializer.serialize("template_name", m_name.c_str());
			serializer.serialize("entity", m_entity.index);
		}


		virtual void deserialize(JsonSerializer& serializer) override
		{
			char name[50];
			serializer.deserialize("template_name", name, sizeof(name), "");
			m_name = name;
			serializer.deserialize("entity", m_entity.index, -1);
			m_entity.universe = m_editor.getEngine().getUniverse();
		}


		virtual void execute() override
		{
			uint32_t name_hash = crc32(m_name.c_str());
			if (m_entity_system.m_instances.find(name_hash) < 0)
			{
				m_entity_system.m_template_names.push(m_name);
				m_entity_system.m_instances.insert(name_hash, Array<Entity>(m_editor.getAllocator()));
				m_entity_system.m_instances.get(name_hash).push(m_entity);
				m_entity_system.m_updated.invoke();
			}
			else
			{
				ASSERT(false);
			}
		}
		
		...
{% endhighlight %}

and create a scene by replaying saved commands

{% highlight cpp %}
virtual bool executeUndoStack(const Path& path) override
{
	destroyUndoStack();
	m_undo_index = -1;
	FS::IFile* file = m_engine->getFileSystem().open("disk", path.c_str(), FS::Mode::OPEN | FS::Mode::READ);
	if (file)
	{
		JsonSerializer serializer(*file, JsonSerializer::READ, path.c_str(), m_allocator);
		serializer.deserializeObjectBegin();
		serializer.deserializeArrayBegin("commands");
		while (!serializer.isArrayEnd())
		{
			serializer.nextArrayItem();
			serializer.deserializeObjectBegin();
			uint32_t type;
			serializer.deserialize("undo_command_type", type, 0);
			IEditorCommand* command = createEditorCommand(type);
			if (!command)
			{
				g_log_error.log("editor") << "Unknown command " << type << " in " << path.c_str();
				destroyUndoStack();
				m_undo_index = -1;
				return false;
			}
			command->deserialize(serializer);
			executeCommand(command);
			serializer.deserializeObjectEnd();
		}
		serializer.deserializeArrayEnd();
		serializer.deserializeObjectEnd();
		m_engine->getFileSystem().close(file);
	}
	return file != NULL;
} 
{% endhighlight %}

Naturally, just by replaying saved command you do not test for anything but crashes. Therefore I save the scene created this way and compare it with a correct scene. It is not possible to test all features in the editor as a lot of them do not change the results scene, but any testing is better than no testing.

For features which are not testable by this procedure, you can still hardcode the test yourself:
{% highlight cpp %}
void runTest()
{
	m_editor->newScene();
	TEST_EXPECT_TRUE(m_editor->isClearScene());
	m_editor->createEntity();
	TEST_EXPECT_EQUAL(m_editor->getUniverse()->getEntities().size(), 1);
	...
}
{% endhighlight %}

## 7. Other tests 

There is one area we did not take into account so far. All the tests we talked about are tests to find bugs, but performance is probably equally important. Nobody will play a game if it crashes all the time but nobody will play a game even if it runs at 5 FPS. You can simply test performance by runing the game, letting AI or scripts control the player or feeding it with random inputs. 

Assets are tightly packed with performance. Artists can not create a mesh with too many polygons, huge textures or complicated shaders and expect the game to run smoothly. All these conditions are however easily tested. 

Finally, tests which are rarely used by are as important as the others are usability test. Even though I have no experience with them I know there are [game companies taking usability tests seriously](http://www.insomniacgames.com/gdc12-insomniacs-share/).

To sum up, automated testing is often overlooked but necessary procedure to create high quality games and game technologies
