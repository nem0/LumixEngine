# Meta

Meta scans the source code for lines containing `//@` and generates artifacts such as reflection data and the Lua C API. It's used to mark modules, components, properties, functions, etc. To keep Meta simple and fast, it works using basic string operations. Meta does not preprocess C++ nor does it parse C++. This means valid C++ code can break Meta, if it's not exactly as Meta expects it.

# Modules

To mark a class to be processed as a module by Meta, `//@ module` is used. It has 3 parameters:

1. Name of the module's struct.
2. Module's identifier.
3. Label used in UI. This must be enclosed in "" since it can contain spaces.

```cpp
//@ module AnimationModule animation "Animation"
struct AnimationModule : IModule {
```

It's not necessary for struct to follow immediately after `//@ module`
```cpp
//@ module GUIModule gui "GUI"

//@ component_struct id gui_canvas
struct Canvas {
	EntityRef entity;
	bool is_3d = false;				//@ property
```

`//@ end` or end of file mark end of module.

Between `//@ module` and its end, there can be multiple elements:

* `//@ enum`
* `//@ functions`
* `//@ include`
* `//@ events`
* `//@ component`
* `//@ component_struct`

## `//@ enum`

Enum can have one attribute named `full`, which makes Meta emit fully qualified enumerator names (keeping the enum's scope) in the generated data instead of shortening them. This is necessary for enums
nested in other structs or namespaces.

Values inside enums are automatically parsed by Meta.

Examples:
```cpp
//@ enum
enum class TextVAlign : i32 {
	TOP,
	MIDDLE,
	BOTTOM
};
```

```cpp
//@ module PhysicsModule physics "Physics"
struct PhysicsModule : IModule {
	//@ enum full PhysicsModule::D6Motion
	enum class D6Motion : i32 {
		LOCKED,
		LIMITED,
		FREE
	};
```

## `//@ functions`

Marks all functions between `//@ functions` and `//@ end` for inclusion in the generated metadata. Only lines starting with `virtual` are included. Functions can have attributes, which are explained later.

Examples:
```cpp
//@ functions
virtual EntityPtr raycast(const Vec3& origin, const Vec3& dir, float distance, EntityPtr ignore_entity) = 0;
virtual void setGravity(const Vec3& gravity) = 0;
//@ end
```

## `//@ include`

When parsing a module, only the header file containing the module is included in the generated metadata. However, metadata may depend on types declared in other headers; for example, a function parameter type may come from another file. Use the `//@ include "path/to/header.h"` directive to add additional headers to the metadata.

Examples:
```cpp
//@ module RenderModule renderer "Render"
//@ include "core/geometry.h"
//@ include "renderer/model.h"
namespace Lumix
{
...
//@ functions
// `Ray` is defined in "core/geometry.h"
virtual RayCastModelHit castRay(const Ray& ray, EntityPtr ignore) = 0;geometry.h"
```

## `//@ events`

Marks all virtual methods between `//@ events` and `//@ end` for inclusion in the generated metadata as events.

Examples:
```cpp
//@ events
virtual DelegateList<void(EntityRef)>& buttonClicked() = 0;
virtual DelegateList<void(EntityRef)>& rectHovered() = 0;
virtual DelegateList<void(EntityRef)>& rectHoveredOut() = 0;
virtual DelegateList<void(EntityRef, float, float)>& rectMouseDown() = 0;
virtual DelegateList<void(bool, i32, i32)>& mousedButtonUnhandled() = 0;
//@ end
```

## `//@ component`

Defines new component type and includes all virtual methods between `//@ component` and `//@ end` in the genrated metadata.

Examples:
```cpp
//@ component
virtual bool isPropertyAnimatorEnabled(EntityRef entity) = 0;
virtual void enablePropertyAnimator(EntityRef entity, bool enabled) = 0;
...
//@ end
```

`//@ component` has 1 parameter - Name - used to autodetect properties from method names. 
ID and label are generated from name and can be overriden with attributes `//@ component Listener id audio_listener`, `//@ component Script id lua_script label "File"`.

### Properties

Meta tries to autodetect properties from method names:

* A method named `get` + `ComponentName` + `PropertyName` is a getter for `PropertyName`.
	```cpp
	//@ component AmbientSound ambient_sound "Ambient sound"
	virtual Path getAmbientSoundClip(EntityRef entity) = 0;					//@ resource_type Clip::TYPE
	```
	This is a getter for property `Clip` of component `AmbientSound`.

* Setters follow the same rule but start with `set`.
* Methods named `is` + `ComponentName` + `Enabled` are treated as getters.
* Methods named `enable` + `ComponentName` are treated as setters for property `Enabled`.
* This behaviour can be changed based on attributes, explained later.

#### `//@ array`

Marks array propreties. Must end with `//@ end`. `//@ array` has two parameters:

1. Name - used to detect sub-properties.
2. Identifier.

Each array must define the methods `add` + `Name`, `remove` + `Name`, and `get` + `Name` + `Count`. All other methods inside the array block are treated as sub-properties. Sub-property detection follows the same rules as ordinary property detection, but uses the array name instead of the component name.

```cpp
//@ array Box boxes
virtual void addBox(EntityRef entity, int index) = 0;
virtual void removeBox(EntityRef entity, int index) = 0;
virtual int getBoxCount(EntityRef entity) = 0;
virtual Vec3 getBoxHalfExtents(EntityRef entity, int index) = 0;
virtual void setBoxHalfExtents(EntityRef entity, int index, const Vec3& size) = 0;
virtual Vec3 getBoxOffsetPosition(EntityRef entity, int index) = 0;			//@ label "Position offset"
virtual void setBoxOffsetPosition(EntityRef entity, int index, const Vec3& pos) = 0;
virtual Vec3 getBoxOffsetRotation(EntityRef entity, int index) = 0;			//@ radians label "Rotation offset"
virtual void setBoxOffsetRotation(EntityRef entity, int index, const Vec3& euler_angles) = 0;
//@ end
```

### Functions in components

Any method inside a component block (`//@ component` ... `//@ end`) that is not detected as a property getter or setter is emitted in the generated metadata as a component function.

## `//@ component_struct`

`//@ component_struct` declares a POD-style component whose data is stored directly in a C++ struct rather than accessed through virtual interface methods.

Rules and behavior:
* `//@ component` and `//@ component_struct` with the same id are merged together. So some properties can use direct field access and other properties can use virtual interface methods.
* ID and label are generated from struct's name and can be overriden with attributes `//@ component_struct id gui_canvas`, `//@ component_struct label "Zone"`.
* Must appear inside a module block.
* The struct must immediately follow on the next line (without blank lines / comments).
* Fields become properties only if marked with //@ property (unlike function-based components where properties are inferred from method names).
* After the struct you terminate the component with `//@ end`
* Only plain, trivially reflectable field declarations are supported; avoid macros, bitfields, templated members, or conditional compilation that would confuse the simple string scanner.
* Default initializers are allowed but not interpreted by Meta (they remain C++-only).
* Comments with attributes must be on the same line as the field.
* Keep one field per line; combining declarations (float a, b;) is not supported.

Examples:
```cpp
//@ component_struct
struct Fur {
	u32 layers = 16;		//@ property
	float scale = 0.01f;	//@ property
	float gravity = 1.f;	//@ property
	bool enabled = true;	//@ property
};
//@ end
```

```cpp
//@ component_struct
struct EchoZone {
	EntityRef entity;
	float radius;		//@ property min 0
	float delay;		//@ property min 0 label "Delay (ms)"
};
//@ end
```

## Attributes

Properties and functions in meta can have attributes. These are defined as `//@` at the end of line. When both a getter and a setter belong to the same property, their attributes are merged (this overwrites attribute value if both getter and setter defines the attribute).

```cpp
void setCameraFov(EntityRef entity, float fov); //@ min 0
float getCameraFov(EntityRef entity); //@ radians
// fov is is in radians and minimal value is 0

void setCameraNearPlane(EntityRef entity, float fov); //@ min 0
float getCameraNearPlane(EntityRef entity); //@ min 0.01
// near plane minimal value is 0.01, 0 on setter is overwritten by 0.01 on getter
```

Examples:

```cpp
// attribute for getter
virtual Path getAmbientSoundClip(EntityRef entity) = 0;	//@ resource_type Clip::TYPE

// for setter
virtual void setAnimatorSource(EntityRef entity, const Path& path) = 0;	//@ resource_type anim::Controller::TYPE

// for function
virtual void pauseAmbientSound(EntityRef entity) = 0;					//@ alias pause

struct ChorusZone {
	// for field
	float radius;			//@ property min 0
};

```

### `function` attribute

Forces Meta to treat the method strictly as a function rather than inferring it as a property getter or setter; this disables automatic property name detection for that method.

```cpp
// this is not auto-detected as a getter for property `InputIndex`
virtual int getAnimatorInputIndex(EntityRef entity, const char* name) const = 0;	//@ function alias getInputIndex
```

### `getter`, `setter` attributes

Allows explicit classification of a method as a property accessor and (optionally) supplies / overrides the property name. Use when the method name does not match automatic patterns or when you want to rename / merge differently named methods into one property.

Example:
```cpp
	//@ component Text gui_text "Text" icon ICON_FA_FONT
	// getter for property `Text` on component `Text`, with automatic 
	// detection the method name would have to be getTextText
	virtual const char* getText(EntityRef entity) = 0;	//@ getter Text 
	virtual void setText(EntityRef entity, const char* text) = 0; //@ setter Text multiline
```

### `radians` attribute

Marks that the numeric value is stored in radians; the editor/UI may display and edit it in degrees (converting automatically).

```cpp
virtual Vec2 getD6JointSwingLimit(EntityRef entity) = 0; //@ radians
struct Camera {
	float fov; //@ property radians
```

### `resource_type` attribute

Restricts a Path property so the editor accepts only resources of the specified type. Provide the engine resource type constant (e.g. Clip::TYPE, anim::Controller::TYPE) after resource_type.

```cpp
virtual Path getAmbientSoundClip(EntityRef entity) = 0; //@ resource_type Clip::TYPE
```

### `color` attribute

Marks that the value represents a color and should use a color picker in tools/UI.

Examples:
```cpp
virtual Vec4 getImageColorRGBA(EntityRef entity) = 0;	//@ label "Color" color
```

### `no_ui` attribute

Hides the property from editor/UI exposure while still generating metadata (available to scripting/serialization but not shown in tools).

```cpp
virtual void setCurveDecalBezierP0(EntityRef entity, const Vec2& value) = 0; //@ no_ui
```

### `multiline` attribute

Marks a string so the editor uses a multi-line text box.

```cpp
virtual void setText(EntityRef entity, const char* text) = 0;			//@ setter Text multiline
```

### `min` attribute

Specifies the minimum allowed value for a numeric property or return value.

```cpp
virtual float getHingeJointDamping(EntityRef entity) = 0;	//@ min 0
```
### `alias` attribute

Specifies an alternate function name; the alias does not need to be globally unique within the module.

### `clamp` attribute

Specifies the minimum and the maximum allowed value for a numeric property or return value.

```cpp
float fov;						//@ property radians clamp 0 360
```

### `label` attribute

Sets a custom human‑readable UI name (can include spaces and different capitalization) for the targeted property or function instead of the automatically derived one.

```cpp
virtual bool getRectClip(EntityRef entity) = 0;					//@ label "Clip content"
```

### `dynenum` attribute

Marks the property as dynamic enum - its values are not compile-time constants. User must proved `Name`+`Enum` class which inherits from `reflection::EnumAttribute`;

```cpp
virtual u32 getInstancedCubeLayer(EntityRef entity) = 0;				//@ dynenum Layer
```

```cpp
	struct LayerEnum : reflection::EnumAttribute {
		u32 count(ComponentUID cmp) const override { 
			PhysicsModule* module = (PhysicsModule*)cmp.module;
			PhysicsSystem& system = (PhysicsSystem&)module->getSystem();
			return system.getCollisionsLayersCount();
		}
		const char* name(ComponentUID cmp, u32 idx) const override { 
			PhysicsModule* module = (PhysicsModule*)cmp.module;
			PhysicsSystem& system = (PhysicsSystem&)module->getSystem();
			return system.getCollisionLayerName(idx);
		}
	};
```

### `enum` attribute

```cpp
virtual GrassRotationMode getGrassRotationMode(EntityRef entity, int index) = 0;	//@ enum
```

# Objects

`//@ object`

# Structs

`//@ struct`
