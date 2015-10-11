---
layout:     post
title:      Third party libraries in Lumix Engine - part 1
date:       2015-10-11 22:04:00
summary:    As it is a waste of time to reinvent the wheel I also used some 3rd party libraries to develop Lumix Engine. Here you can find out about the reasons I ended up using ImGui.
categories: editor GUI library
comments: true
---

## Editor GUI

From here on I will talk about UI for editor when talking about UI. 

I think that creating great UI library is extremely hard and maybe it is even an impossible task. In the past I required several qualities from UI frameworks:

* Fast - it must be able to work with list of milions of objects,
* Complete - it must have all of the common widgets already implemented,
* Flexible - it must empower me to do whatever I want,
* Data definition - widgets can be defined by some data file (XML, JSON, ...)
* Easy to implement - the less time it takes to code stuff the better,
* Both editor and game UI - one system for both,
* Docking and multimonitor support - must have, 
* Skinnable - I could live without this.

Of course I did not have all this requirements all at once, rather they changed with time. In the last two years I've tried at least 8 different UI frameworks. 

* Gwen
* CEGUI
* Rocket
* Browser HTML
* Custom UI
* C#/.Net
* Qt
* ImGui

### Gwen, CEGUI
In the beginning of my search I wanted something that can be used for both, for editor and for game. Later I realised that it is not a good idea, since each area have different requirements. I do not need any fancy graphics, animations, effects or skins in an editor. On the other hand I do not usually need to display a huge amount of data in a game UI. 

I've tried several famous open source GUIs, but mainly two of them - [Gwen](https://github.com/garrynewman/GWEN) and [CEGUI](http://cegui.org.uk/). They turned out to be not a good choice for an editor UI, however I plan to give them a chance when I will work on game UI. 

### Rocket, Browser HTML
I do not really remember why did I decide to try to make an editor in HTML, maybe it was because of a blogpost from [Insomniac Games](http://www.insomniacgames.com/ron-pieket-a-clientserver-tools-architecture/). I implemented a simple webserver in my engine and managed to communicate between a browser and the engine. Everything worked fine until I tried to do first real functions. Then it came back to me. I used to do webpages and web apps for years and the experience with web technologies was far from pleasant. And I had no idea how to do docking, I did not want to implement it by myself in HTML/JavaScript. Later I found [a neat solution for my docking problem](http://www.dockspawn.com/) however it was already too late and it would not change my mind anyway. What is more the perforamce of this solution is pretty terrible. Try to create a table with two milions columns and see whether your browser manage to survive. If it does, congratulation. Now try to serialize these 2 milions rows of data in C++, send them through a websocket a deserialize them in JavaScript. Still alive?

### Custom UI
As much as I wanted to avoid this, I ended up writing my own GUI. All my requirements can be met, if I use my own UI. I know doing this is a job for years, but I tried anyway. Maybe I hoped I was wrong about the length of the task. At first it seemed everything went smooth. I managed to implement basic widgets pretty easily, but then I needed a multiline text edit with text selection and it hit me. This is really a work for years.

### C#/.Net
Making GUI in C# is recommended by 90% of the programmers on the Internet. But can you trust people on the Web? C# was our choice at the company I worked for a few years ago. We've had WinAPI based editor before and decided to make something modern in C#. The rest of our code was C++, so we have to implement some connection between C# and C++. We chose SWIG. It caused a few problems, but with regards to the amount of code it generated it was not so unexpected. In Lumix Engine I chose a different approach. I had only one C++ function which the editor in C# used to communicate with C++. This has a nice side effect - it force both sides of this communication channel to have a well architectured code. On the other hand doing everything else was more complicated. What is more makeing GUI in C# is not really easy. Yes, as long as you need just some standard widgets it is simple, you can even use some WYSIWYG editor. But do you want checkboxes in you property grid? Or some other nonstandard widgets? What about skinning a scrollbar? Well it turns out it is not so easy. In the end there is one feature that C# does better than the rest - docking. Although it is provided in a form of [library](http://dockpanelsuite.com/), I have not found anything close to such library in any other solutions.

### Qt 
Compared to C# I do not need special layer to communicate with different programming language. Creating UI widgets seems to be as complicated or as easy as in C#, there is event a WYSIWYG editor. Qt support for skinning is terrific, except the dark skin I used caused crashes when a progress bar's value is set 0%. New windows can be created using data files, I was even able to easily create new UI from Lua scripts, all I had to do was to export one function to Lua. Standard GUI operations are fast, however programmatically selecting half of about 20 thousands entities in a list view takes half a minute. Also when rendering profiling graph, the rendering itself takes substantial amount of time and can be visible in the graph as a big spike. It is possible to solve this, however it means basically going around Qt. There is also a builtin support for docking, however it lacks features compared to C#. The last issue I had with Qt is that it is really huge. I have to install Qt plugin for Visual Studio, when compiling there must be a custom build step on some files, Qt is basically tring to extend C++ by using special macros. And also Qt requires several huge dynamic libraries - ~20 MB just because of UTF, that's much more than the all of my engine with several other 3rd party libraries. 


### ImGui
The last solution and the one I am currently using is ImGui. It's the only representative of immediate mode GUIs. All the other solutions are retained mode GUIs. I am not going to write about differences between these two modes as you can find a lot of info on the Internet. 

[ImGui](https://github.com/ocornut/imgui) is an open source library created by Omar Cornut. I hasitated at first to switch to ImGui from Qt because I was afraid that immediate mode GUI is very limited and I will encounter something I will not be able to do. In fact I am still afraid even though the experience so far was very pleasant. Yes, ImGui does not have many features Qt has, which in some cases is a good thing (Qt can do almost anything, even things I do not need and never will, however I have to pay the price in form of huge dll). In other cases it's a disadvantage. ImGui misses a lot of basic widgets, such as a progress bar or advanced color picker. Saying that I must add that while ImGui does not have advanced color pickers, it is extremely easy to add, which [I did](https://github.com/nem0/LumixEngine/commit/c0ea1a8a754344856c0b039cfd4b4b7e030505ae). And so did [other people](https://github.com/ApoorvaJ/Papaya). I can not even imagine how difficult this is in Qt. Things are in general much easier to do in ImGui than in other UI toolkits:

```
  if(ImGui::Button("OK")) showMessage("OK");
``` 

vs

```
  	&lt;!-- somewhere in ui file --&gt;
	  &lt;widget class="QPushButton" name="browseSourceButton"&gt;
	   &lt;property name="text"&gt;
		&lt;string&gt;Browse&lt;/string&gt;
	   &lt;/property&gt;
	  &lt;/widget&gt;
	  
	// somwhere in .h	  
	void on_browseSourceButton_clicked();
	  
	// somewhere in .cpp
	void ImportAssetDialog::on_browseSourceButton_clicked()
	{
		showMessage("OK");
	}
```

To be honest a button in Qt is much more powerful than a button in ImGui, but do I really need all that power for a simple button and pay the price for it?

My second fear regarding ImGui was that it will be slow. Well it has to be since I call all this functions every frame. Except it is not. And if you think about it and realise which instructions need to be executed in the end by immediate mode and which by retained mode, you find out that in most use cases there is no reason for ImGui to be slow. In fact there were two things which in my case were faster in ImGui than in Qt. 

Firstly, I had a list of all entities in a map. This number can be quite high. I aim for milions of entities in one map, but the performance problem was there even for 20 thousands of entities. What is more some of these entities can be selected. In Qt I used QListView but selecting several entities at once turned out to be painfully slow (~20 seconds). In ImGui this causes no noticable delay. 

Secondly, I had a profiler which display a graph. Ironically enough each time I render the graph, you could see a spike in the profile graph caused by the rendering. I know there is a way to do this fast, however it is not the straightest solution, especially compared to ImGui.

Finally, the thing that convinced me to switch to ImGui from Qt, the property grid. In Qt I had to have milions of signals/slots to keep the displayed data correct and each new feature added a new callback to the property grid. Resource loading... callback. Undo/redo... callback. Terrain editor... callback. Of course I always missed some of them and a new bug was born. On the other hand it is the nature of immediate mode that you do not need any callbacks and it is a pleasant suprise to find out everything correctly updating in the property grid.

#Summary

After spending the last two years looking for ideal UI for an editor, I think I finally found the acceptable one and I hope I will stick with it for some time.
