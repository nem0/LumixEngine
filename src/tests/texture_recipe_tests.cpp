#include "core/crt.h"
#include "core/log.h"
#include "core/stream.h"
#include "engine/file_system.h"
#include "renderer/editor/composite_texture.h"
#include "tests/common.h"

using namespace Lumix;
namespace {
struct Fixture {
	Fixture() : fs(FileSystem::create(".", getGlobalAllocator())), result(getGlobalAllocator()), error(getGlobalAllocator()) {
		fs->mount(".", "");
	}
	bool run(const char* source, bool expect_error = false) {
		const bool success = compileTextureRecipe(*fs, source, Path("test.ltct"), result, error, getGlobalAllocator());
		if (!success && !expect_error) logError(error);
		return success;
	}
	UniquePtr<FileSystem> fs;
	CompositeTexture::Result result;
	String error;
};

bool testEagerRecipeMap() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
fn main() : Texture2D {
    if false { var unused = load("textures/does_not_exist.png"); }
    var image = resize(color(2,2,Vec4 {0.25,0.5,0.75,1}),3,1);
    map(image,fn(x:u32,y:u32,value:Vec4):Vec4 {
        return Vec4 {x as f32 / 2,value.y,value.z,value.w};
    });
    // map has already modified the original image.
    setPixel(image,0,0,Vec4 {1,0,0,0});
    return texture2D(image);
}
)"));
	ASSERT_EQ(1, f.result.layers.size());
	const auto& image = f.result.layers[0];
	ASSERT_EQ(3, image.w); ASSERT_EQ(1, image.h); ASSERT_EQ(4, image.channels);
	ASSERT_EQ(1.f, image.pixels[0]); ASSERT_EQ(0.5f, image.pixels[4]); ASSERT_EQ(1.f, image.pixels[8]);
	ASSERT_EQ(0.f, image.pixels[1]); ASSERT_EQ(0.f, image.pixels[2]); ASSERT_EQ(0.f, image.pixels[3]);
	return true;
}

bool testEagerRecipeStateAndAliasing() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
struct State { scale:f32; bias:f32; }
fn main() : Texture2D {
    var image = create(2,1,1);
    var alias = image;
    setPixel(alias,1,0,Vec4 {0.25,0,0,1});
    mapWith(image,State {2,0.125},fn(x:u32,y:u32,v:Vec4,state:State):Vec4 {
        return Vec4 {v.x*state.scale+state.bias,v.y,v.z,v.w};
    });
    return texture2D(alias);
}
)"));
	const auto& image = f.result.layers[0];
	ASSERT_EQ(1, image.channels); ASSERT_EQ(2, image.pixels.size());
	ASSERT_EQ(0.125f, image.pixels[0]); ASSERT_EQ(0.625f, image.pixels[1]);
	return true;
}

bool testEagerRecipeChannelPreservation() {
	Fixture f;
	for (u32 channels = 1; channels <= 4; ++channels) {
		const StaticString<1024> source(R"(
import "core:texture_recipe"
import "core:vec4"
fn main() : Texture2D {
    var image = create(1,1,)", channels, R"();
    map(image,fn(x:u32,y:u32,v:Vec4):Vec4 { return Vec4 {0.25,0.5,0.75,1}; });
    mapWith(image,2 as f32,
        fn(x:u32,y:u32,v:Vec4,scale:f32):Vec4 { return Vec4 {v.x*scale,v.y*scale,v.z*scale,v.w*scale}; });
    return texture2D(image);
}
)");
		ASSERT_TRUE(f.run(source));
		const auto& image = f.result.layers[0];
		ASSERT_EQ(channels, image.channels);
		ASSERT_EQ(channels, image.pixels.size());
		for (u32 i = 0; i < channels; ++i) ASSERT_EQ(0.5f * (i + 1), image.pixels[i]);
	}
	return true;
}

bool testEagerRecipeLoad() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
fn main() : Texture2D { return texture2D(resize(load("engine/textures/white.tga"),2,1)); }
)"));
	ASSERT_EQ(2, f.result.layers[0].w); ASSERT_EQ(1, f.result.layers[0].h);
	for (float value : f.result.layers[0].pixels) ASSERT_FLOAT_EQ(1.f, value);
	return true;
}

bool testEagerRecipeExample() {
	Fixture f;
	OutputMemoryStream source(getGlobalAllocator());
	ASSERT_TRUE(f.fs->getContentSync(Path("scripts/tests/texture_recipe.ltct"), source));
	ASSERT_TRUE(compileTextureRecipe(*f.fs, StringView((const char*)source.data(), source.size()), Path("example.ltct"), f.result, f.error, getGlobalAllocator()));
	const auto& image = f.result.layers[0];
	ASSERT_EQ(4, image.w); ASSERT_EQ(4, image.h); ASSERT_EQ(4, image.channels);
	ASSERT_EQ(0.25f, image.pixels[0]); ASSERT_EQ(0.5f, image.pixels[1]);
	ASSERT_EQ(0.75f, image.pixels[2]); ASSERT_EQ(1.f, image.pixels[3]);
	return true;
}

bool testEagerRecipeArray() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
fn main() : TextureArray {
    var red = create(2,1,2);
    setPixel(red,0,0,Vec4 {1,0,0,1});
    var green = create(2,1,2);
    setPixel(green,0,0,Vec4 {0.5,0,0,1});
    var layers : [3]Image = [red,green,red];
    var output = textureArray(layers);
    layers[0] = green;
    return output;
}
)"));
	ASSERT_EQ(3, f.result.layers.size());
	ASSERT_TRUE(!f.result.is_cubemap);
	for (const auto& layer : f.result.layers) {
		ASSERT_EQ(2, layer.w); ASSERT_EQ(1, layer.h); ASSERT_EQ(2, layer.channels);
	}
	ASSERT_EQ(0.5f, f.result.layers[0].pixels[0]);
	ASSERT_EQ(0.f, f.result.layers[0].pixels[1]);
	ASSERT_EQ(0.5f, f.result.layers[1].pixels[0]);
	ASSERT_EQ(1.f, f.result.layers[2].pixels[0]);
	f.result.layers[0].pixels[0] = 0;
	ASSERT_EQ(1.f, f.result.layers[2].pixels[0]);
	return true;
}

bool testEagerRecipeArrayExample() {
	Fixture f;
	OutputMemoryStream source(getGlobalAllocator());
	ASSERT_TRUE(f.fs->getContentSync(Path("scripts/tests/texture_recipe_array.ltct"), source));
	ASSERT_TRUE(compileTextureRecipe(*f.fs, StringView((const char*)source.data(), source.size()), Path("array_example.ltct"), f.result, f.error, getGlobalAllocator()));
	ASSERT_EQ(3, f.result.layers.size());
	for (u32 i = 0; i < 3; ++i) {
		const auto& layer = f.result.layers[i];
		ASSERT_EQ(4, layer.w); ASSERT_EQ(4, layer.h); ASSERT_EQ(4, layer.channels);
		for (u32 channel = 0; channel < 4; ++channel) ASSERT_EQ(channel == i || channel == 3 ? 1.f : 0.f, layer.pixels[channel]);
	}
	return true;
}

bool testRecipeArraySlice() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
fn main() : TextureArray {
    var layers : [3]Image = [color(1,1,Vec4 {1,0,0,1}),color(1,1,Vec4 {0,1,0,1}),color(1,1,Vec4 {0,0,1,1})];
    var output = textureArray(layers[1:3]);
    layers[1] = layers[0];
    return output;
}
)"));
	ASSERT_EQ(2, f.result.layers.size());
	ASSERT_EQ(1.f, f.result.layers[0].pixels[0]);
	ASSERT_EQ(1.f, f.result.layers[1].pixels[2]);
	ASSERT_TRUE(!f.result.is_cubemap);
	return true;
}

bool testEagerRecipeArrayErrors() {
	Fixture f;
	const char* invalid[] = {
		R"(import "core:texture_recipe" fn main():TextureArray { var layers:[1]Image=[create(1,1,4)]; return textureArray(layers[0:0]); })",
		R"(import "core:texture_recipe" fn main():TextureArray { var layers:[2]Image=[create(1,1,4),create(2,1,4)]; return textureArray(layers); })",
		R"(import "core:texture_recipe" fn main():TextureArray { var layers:[1]Image=[create(1,1,4)]; layers[0].handle=0; return textureArray(layers); })",
		R"(import "core:texture_recipe" fn main():TextureArray { var layers:[2]Image=[create(1,1,1),create(1,1,4)]; return textureArray(layers); })",
		R"(import "core:texture_recipe" fn main():TextureArray { var image=create(1,1,4); var layers:[257]Image=undefined; for i in 0..257 { layers[i]=image; } return textureArray(layers); })"
	};
	for (const char* source : invalid) {
		ASSERT_TRUE(!f.run(source, true));
		ASSERT_TRUE(f.error.length() > 0);
		ASSERT_TRUE(f.result.layers.empty());
	}
	ASSERT_TRUE(f.run(R"(import "core:texture_recipe" fn main():TextureArray { var layers:[1]Image=[create(1,1,4)]; return textureArray(layers); })"));
	ASSERT_EQ(1, f.result.layers.size());
	return true;
}

bool testRecipeProceduralExamples() {
	Fixture f;
	const char* paths[] = {"scripts/tests/ion_marble.ltct", "scripts/tests/orbital_alloy.ltct", "scripts/tests/tree_bark.ltct"};
	for (const char* name : paths) {
		OutputMemoryStream source(getGlobalAllocator());
		const Path path(name);
		ASSERT_TRUE(f.fs->getContentSync(path, source));
		ASSERT_TRUE(compileTextureRecipe(*f.fs, StringView((const char*)source.data(), source.size()), path, f.result, f.error, getGlobalAllocator()));
		ASSERT_EQ(1, f.result.layers.size());
		const auto& image = f.result.layers[0];
		ASSERT_TRUE(image.w >= 256 && image.h == image.w);
		ASSERT_EQ(4, image.channels);
		bool varied = false;
		for (u32 i = 0; i < image.w * image.h; ++i) {
			ASSERT_FLOAT_EQ(1.f, image.pixels[i * 4 + 3]);
			if (image.pixels[i * 4] != image.pixels[0]) varied = true;
		}
		ASSERT_TRUE(varied);
	}
	return true;
}

bool testEagerRecipeErrors() {
	Fixture f;
	const char* invalid[] = {
		"import \"core:texture_recipe\" fn main():Texture2D { return texture2D(create(0,1,4)); }",
		"import \"core:texture_recipe\" fn main():Texture2D { return texture2D(create(20000,1,4)); }",
		"import \"core:texture_recipe\" fn main():Texture2D { var i=create(1,1,4); var p=getPixel(i,1,0); return texture2D(i); }",
		"import \"core:texture_recipe\" fn main():Texture2D { var i=create(1,1,4); i.width=2; return texture2D(i); }",
		"import \"core:texture_recipe\" fn main():Texture2D { var i=create(1,1,4); i.channels=0; return texture2D(i); }",
		"import \"core:texture_recipe\" fn main():Texture2D { return texture2D(load(\"../outside.tga\")); }",
		"import \"core:texture_recipe\" fn main():Texture2D { return texture2D(load(\"textures/missing.png\")); }",
		"fn main():i32 { return 0; }",
		R"(import "core:texture_recipe" fn main():Image { return create(1,1,4); })",
		R"(import "core:texture_recipe" struct Output { image:Image; } fn main():Output { return Output {create(1,1,4)}; })",
		R"(import "core:texture_recipe" import "core:vec4"
fn main():Texture2D { var image=create(1,1,4); map(image,fn(x:u32,y:u32,v:Vec4):Vec4 { panic("callback failure"); }); return texture2D(image); })"
	};
	for (const char* source : invalid) {
		ASSERT_TRUE(!f.run(source, true));
		ASSERT_TRUE(f.error.length() > 0);
		ASSERT_TRUE(f.result.layers.empty());
	}
	// A failed build must not poison the next isolated execution.
	ASSERT_TRUE(f.run("import \"core:texture_recipe\" fn main():Texture2D { return texture2D(create(1,1,4)); }"));
	return true;
}
bool testRecipeInPlace() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
fn main():Texture2D {
    var image=color(2,1,Vec4 {0.25,0.5,0.75,1});
    const alias=image;
    const original=copy(image);
    for i in 0..300 { invert(image); }
    brightness(image,0.25);
    if getPixel(alias,0,0).x != 0.5 or getPixel(original,0,0).x != 0.25 {
        panic("In-place alias or independent copy broken");
    }
    const resized=resize(image,3,2);
    const cropped=crop(image,0,0,1,1);
    const scalar=split(image,0);
    if resized.handle == image.handle or cropped.handle == image.handle or scalar.handle == image.handle {
        panic("Layout-changing operations must allocate");
    }
    if image.width != 2 or image.height != 1 or image.channels != 4 {
        panic("Input layout changed");
    }
    return texture2D(alias);
})"));
	ASSERT_FLOAT_EQ(0.5f, f.result.layers[0].pixels[0]);
	return true;
}

bool testRecipeSamplingAndTransforms() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
fn check(v:f32,e:f32):void { if v-e > 0.0001 or e-v > 0.0001 { panic("Unexpected transform pixel"); } }
fn main():Texture2D {
	var image=resize(gradient(3),3,3);
	check(sample(image,-0.5,0,true).x,0.5);
	check(sample(image,-3,0,true).x,0);
	check(sample(image,-0.5,0,false).x,0);
	check(sample(image,0.5,0,false).x,0.25);
	var flipped=copy(image); flip(flipped,true,true);
	check(getPixel(flipped,0,0).x,1);
	var wrapped=copy(image); translate(wrapped,-1,0,true);
	check(getPixel(wrapped,0,0).x,1);
	var clamped=copy(image); translate(clamped,-1,0,false);
	check(getPixel(clamped,0,0).x,0);
	check(getPixel(crop(image,1,1,2,2),0,0).x,0.5);
	var warped=copy(image); warp(warped,warped,1);
	check(getPixel(warped,0,1).x,0.25);
	var twirled=copy(image); twirl(twirled,0);
	check(getPixel(twirled,1,1).x,0.5);
	var copied=copy(image); setPixel(image,1,1,Vec4 {9,0,0,1});
	check(getPixel(copied,1,1).x,0.5);
	return texture2D(copied);
})"));
	ASSERT_EQ(1, f.result.layers[0].channels);
	return true;
}

bool testRecipeChannelsAndMath() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
import "core:vec2"
fn check(v:f32,e:f32):void { if v-e > 0.0001 or e-v > 0.0001 { panic("Unexpected math pixel"); } }
fn main():Texture2D {
	const image=color(1,1,Vec4 {0.25,0.5,0.75,0.5});
	const alpha=split(image,3); check(getPixel(alpha,0,0).x,0.5);
	const merged=merge([split(image,0),split(image,1),split(image,2),alpha]);
	check(getPixel(merged,0,0).z,0.75);
	const rg=merge([split(image,0),split(image,1)]);
	if rg.channels != 2 { panic("Merge channel count"); }
	check(getPixel(rg,0,0).y,0.5);
	check(getPixel(setAlpha(image,split(image,0)),0,0).w,0.25);
	check(getPixel(splat(alpha),0,0).z,0.5);
	var product=copy(image); multiply(product,product);
	check(getPixel(product,0,0).y,0.25);
	var quotient=copy(image); divide(quotient,quotient);
	check(getPixel(quotient,0,0).w,1);
	var inverted=copy(image); invert(inverted);
	var low=copy(image); minimum(low,inverted);
	check(getPixel(low,0,0).z,0.25);
	var high=copy(image); maximum(high,inverted);
	check(getPixel(high,0,0).x,0.75);
	var blended=copy(image); mix(blended,inverted,0.5);
	check(getPixel(blended,0,0).x,0.5);
	var threshold=copy(image); step(threshold,0.5);
	check(getPixel(threshold,0,0).y,1);
	check(getPixel(threshold,0,0).x,0);
	var bright=copy(image); brightness(bright,0.25);
	check(getPixel(bright,0,0).x,0.5);
	check(getPixel(bright,0,0).w,0.5);
	var contrasted=copy(image); contrast(contrasted,0);
	check(getPixel(contrasted,0,0).z,0.75);
	var corrected=copy(image); gamma(corrected,2);
	check(getPixel(corrected,0,0).x,0.5);
	var gray=copy(image); grayscale(gray);
	check(getPixel(gray,0,0).x,0.45375);
	var points:[2]Vec2=[Vec2 {0,1},Vec2 {1,0}];
	var curved=copy(image); curve(curved,points);
	check(getPixel(curved,0,0).z,0.25);
	var above=copy(image); brightness(above,2); curve(above,points);
	check(getPixel(above,0,0).x,0);
	var below=copy(image); brightness(below,-2); curve(below,points);
	check(getPixel(below,0,0).x,1);
	var stops:[2]ColorStop=[ColorStop {0,Vec4 {1,0,0,1}},ColorStop {1,Vec4 {0,0,1,0}}];
	const mapped=gradientMap(alpha,stops);
	check(getPixel(mapped,0,0).x,0.5); check(getPixel(mapped,0,0).w,0.5);
	var dark_alpha=copy(alpha); brightness(dark_alpha,-2);
	check(getPixel(gradientMap(dark_alpha,stops),0,0).x,1);
	var bright_alpha=copy(alpha); brightness(bright_alpha,2);
	check(getPixel(gradientMap(bright_alpha,stops),0,0).z,1);
	return texture2D(mapped);
})"));
	ASSERT_EQ(4, f.result.layers[0].channels);
	return true;
}

bool testRecipeShade() {
	Fixture f;
	for (u32 channels = 1; channels <= 4; ++channels) {
		const StaticString<1024> source(R"(
import "core:texture_recipe"
import "core:vec2"
import "core:vec4"
fn main():Texture2D {
    var image=create(3,1,)", channels, R"();
    for x in 0..3 { setPixel(image,x as u32,0,Vec4 {0.25,0.5,0.75,0.4}); }
    shade(image,gradient(3),1,ReliefLight {Vec2 {1,0},0.5,0.3,0.8});
    return texture2D(image);
})");
		ASSERT_TRUE(f.run(source));
		const auto& image = f.result.layers[0];
		ASSERT_EQ(channels, image.channels);
		for (u32 x = 0; x < 3; ++x) {
			for (u32 c = 0; c < channels; ++c) {
				const float expected = c == 3 ? 0.4f : 0.25f * (c + 1) * (x == 1 ? 0.8f : 0.3f);
				ASSERT_FLOAT_EQ(expected, image.pixels[x * channels + c]);
			}
		}
	}
	const char* invalid[] = {
		R"(import "core:texture_recipe" import "core:vec2" fn main():Texture2D { var image=create(1,1,4); shade(image,create(2,1,1),1,ReliefLight {Vec2 {1,0},1,0,1}); return texture2D(image); })",
		R"(import "core:texture_recipe" import "core:vec2" fn main():Texture2D { var image=create(1,1,4); shade(image,create(1,1,4),1,ReliefLight {Vec2 {1,0},1,0,1}); return texture2D(image); })",
		R"(import "core:texture_recipe" import "core:vec2" fn main():Texture2D { var image=create(1,1,4); shade(image,create(1,1,1),1,ReliefLight {Vec2 {1,0},1,2,1}); return texture2D(image); })"
	};
	for (const char* source : invalid) {
		ASSERT_TRUE(!f.run(source, true));
		ASSERT_TRUE(f.error.length() > 0);
		ASSERT_TRUE(f.result.layers.empty());
	}
	return true;
}

bool testRecipeFilters() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
fn check(v:f32,e:f32):void { if v-e > 0.0001 or e-v > 0.0001 { panic("Unexpected filter pixel"); } }
fn main():Texture2D {
	var image=create(3,3,1); setPixel(image,1,1,Vec4 {1,0,0,1});
	const b=copy(image); blur(b,1);
	check(getPixel(b,0,0).x,1 as f32/9); check(getPixel(b,1,1).x,1 as f32/9);
	const v=copy(image); vBlur(v,1);
	check(getPixel(v,1,0).x,1 as f32/3); check(getPixel(v,0,1).x,0);
	const sharp=copy(image); sharpen(sharp);
	check(getPixel(sharp,1,1).x,17 as f32/9);
	blur(image,0);
	check(getPixel(image,1,1).x,1);
	const repeated=create(1,1,1); blur(repeated,260);
	check(getPixel(repeated,0,0).x,0);
	const n=normalmap(resize(gradient(3),3,3),1);
	check(getPixel(n,1,1).x,1); check(getPixel(n,1,1).y,0.5);
	return texture2D(n);
})"));
	ASSERT_EQ(2, f.result.layers[0].channels);
	return true;
}

bool testRecipeGeneratorsAndSplatter() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
import "core:vec2"
fn check(v:f32,e:f32):void { if v-e > 0.0001 or e-v > 0.0001 { panic("Unexpected generator pixel"); } }
fn main():Texture2D {
	check(getPixel(checkerboard(3,3,1),1,0).x,1);
	check(getPixel(circle(3,3,1),1,1).x,0);
	check(getPixel(circle(3,3,1),0,1).x,1);
	check(getPixel(square(3,3,1),0,0).x,1);
	check(getPixel(triangle(3,3,1),1,1).x,0);
	check(getPixel(gradient(1),0,0).x,0.5);
	const a=valueNoise(4,4,123); const b=valueNoise(4,4,123); const c=valueNoise(4,4,124);
	const g=gradientNoise(4,4,2); const cell=cellNoise(4,4,2,0); const wave=waveNoise(4,4,2,0);
	check(getPixel(g,0,0).x,0.5); check(getPixel(wave,0,0).x,0.5);
	var different=false;
	for y in 0..4 { for x in 0..4 {
	    const v=getPixel(a,x as u32,y as u32).x; check(v,getPixel(b,x as u32,y as u32).x);
	    if v != getPixel(c,x as u32,y as u32).x { different=true; }
	    if v < 0 or v > 1 or getPixel(cell,x as u32,y as u32).x < 0 or getPixel(wave,x as u32,y as u32).x < 0 or getPixel(wave,x as u32,y as u32).x > 1 { panic("Noise out of range"); }
	} }
	if not different { panic("Seed ignored"); }
	const background=color(4,4,Vec4 {0,0,0,0});
	const pattern=color(1,1,Vec4 {1,0,0,0.5});
	gridSplatter(background,pattern,2,2,0,0,123);
	check(getPixel(background,2,2).x,0.5); check(getPixel(background,1,1).x,0);
	circularSplatter(background,pattern,1,0,0,0,0,0,123);
	check(getPixel(background,1,1).x,0.5);
	return texture2D(background);
})"));
	return true;
}

bool testRecipeCubemap() {
	Fixture f;
	ASSERT_TRUE(f.run(R"(
import "core:texture_recipe"
import "core:vec4"
fn main():TextureCube {
	var faces:[6]Image=undefined;
	for i in 0..6 { faces[i]=color(2,2,Vec4 {i as f32,0,0,1}); }
	return textureCube(faces);
})"));
	ASSERT_TRUE(f.result.is_cubemap);
	ASSERT_EQ(6, f.result.layers.size());
	for (u32 i = 0; i < 6; ++i) ASSERT_FLOAT_EQ(float(i), f.result.layers[i].pixels[0]);
	const char* invalid[] = {
	    R"(import "core:texture_recipe" fn main():TextureCube { var i=create(2,1,1); return textureCube([i,i,i,i,i,i]); })",
	    R"(import "core:texture_recipe" fn main():TextureCube { var i=create(1,1,1); return textureCube([i,i,i,i,i,create(2,2,1)]); })",
	    R"(import "core:texture_recipe" fn main():TextureCube { var i=create(1,1,1); return textureCube([i,i,i,i,i,create(1,1,4)]); })",
	    R"(import "core:texture_recipe" fn main():TextureCube { var i=create(1,1,1); var bad=i; bad.handle=0; return textureCube([i,i,i,i,i,bad]); })"
	};
	for (const char* source : invalid) { ASSERT_TRUE(!f.run(source, true)); ASSERT_TRUE(f.result.layers.empty()); }
	return true;
}

bool testRecipeOperationErrors() {
	Fixture f;
	const char* invalid[] = {
	    R"(import "core:texture_recipe" fn main():Texture2D { return texture2D(checkerboard(1,1,0)); })",
	    R"(import "core:texture_recipe" fn main():Texture2D { return texture2D(crop(create(2,2,1),1,0,2,1)); })",
	    R"(import "core:texture_recipe" fn main():Texture2D { return texture2D(split(create(1,1,1),1)); })",
	    R"(import "core:texture_recipe" fn main():Texture2D { var image=create(1,1,1); multiply(image,create(2,1,1)); return texture2D(image); })",
	    R"(import "core:texture_recipe" fn main():Texture2D { var image=create(1,1,1); multiply(image,create(1,1,4)); return texture2D(image); })",
	    R"(import "core:texture_recipe" fn main():Texture2D { var image=create(1,1,1); divide(image,create(1,1,1)); return texture2D(image); })",
	    R"(import "core:texture_recipe" fn main():Texture2D { return texture2D(normalmap(create(1,1,4),1)); })",
	    R"(import "core:texture_recipe" fn main():Texture2D { var image=create(1,1,1); gamma(image,0); return texture2D(image); })",
	    R"(import "core:texture_recipe" import "core:vec2" fn main():Texture2D { var p:[2]Vec2=[Vec2 {0,0},Vec2 {0,1}]; var image=create(1,1,1); curve(image,p); return texture2D(image); })"
	};
	for (const char* source : invalid) {
	    ASSERT_TRUE(!f.run(source, true)); ASSERT_TRUE(f.error.length() > 0); ASSERT_TRUE(f.result.layers.empty());
	}
	return true;
}
} // namespace

void runTextureRecipeTests() {
	RUN_TEST(testRecipeProceduralExamples);
	RUN_TEST(testRecipeInPlace);
	RUN_TEST(testRecipeSamplingAndTransforms);
	RUN_TEST(testRecipeChannelsAndMath);
	RUN_TEST(testRecipeShade);
	RUN_TEST(testRecipeFilters);
	RUN_TEST(testRecipeGeneratorsAndSplatter);
	RUN_TEST(testRecipeCubemap);
	RUN_TEST(testRecipeOperationErrors);
	RUN_TEST(testEagerRecipeMap);
	RUN_TEST(testEagerRecipeStateAndAliasing);
	RUN_TEST(testEagerRecipeChannelPreservation);
	RUN_TEST(testEagerRecipeLoad);
	RUN_TEST(testEagerRecipeExample);
	RUN_TEST(testEagerRecipeArray);
	RUN_TEST(testEagerRecipeArrayExample);
	RUN_TEST(testRecipeArraySlice);
	RUN_TEST(testEagerRecipeArrayErrors);
	RUN_TEST(testEagerRecipeErrors);
}
