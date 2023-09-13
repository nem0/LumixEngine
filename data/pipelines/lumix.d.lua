export type RenderBufferDesc = {
    format : string,
    debug_name : string?,
    size : {number}?,
    rel_size : {number}?,
    compute_write : boolean?,
    point_filter : boolean?
}

declare class RenderBufferDescHandle
end

declare class RenderState
end

declare class CameraParams
end

declare class Shader
end

declare class Bucket
end

declare class RenderBuffer
end

declare function beginBlock(name : string) : ()
declare function bindImageTexture(texture : RenderBuffer, offset : number) : ()
declare function bindTextures(textures : {RenderBuffer}, offset : number) : ()
declare function clear(flags : number, r : number, g : number, b : number, a : number, depth : number) : ()
declare function createRenderbuffer(desc : RenderBufferDescHandle) : RenderBuffer
declare function createRenderbufferDesc(desc : RenderBufferDesc) : RenderBufferDescHandle
declare function createRenderState(state : any) : RenderState
declare function cull(cp : CameraParams, ... : any) : ()
declare function dispatch(shader : Shader, x : number, y : number, z : number) : ()
declare function drawArray(indices_offset : number, indices_count : number, shader : Shader, textures : {RenderBuffer}, rs : any, define : string?) : ()
declare function drawcallUniforms(...: number) : ()
declare function endBlock() : ()
declare function environmentCastShadows() : boolean
declare function getCameraParams() : CameraParams
declare function getShadowCameraParams(slice : number) : CameraParams
declare function keepRenderbufferAlive(rb : RenderBuffer) : ()
declare function pass(cp : CameraParams) : ()
declare function preloadShader(path: string) : Shader
declare function render2D() : ()
declare function renderBucket(bucket : Bucket) : ()
declare function renderDebugShapes() : ()
declare function renderGizmos() : ()
declare function renderGrass(cp : CameraParams, rs : RenderState) : ()
declare function renderIcons() : ()
declare function renderIngameGUI() : ()
declare function renderOpaque() : ()
declare function renderSelection() : ()
declare function renderTerrains(cp: CameraParams, rs: RenderState, define : string?) : ()
declare function renderTransparent() : ()
declare function renderUI() : ()
declare function setOutput(rb : RenderBuffer) : ()
declare function setRenderTargets(rt: {RenderBuffer}?) : ()
declare function setRenderTargetsDS(...: RenderBuffer) : ()
declare function setRenderTargetsReadonlyDS(...: RenderBuffer) : ()
declare function viewport(x : number, y : number, w : number, h : number) : ()

    
declare PROBE : string
declare PREVIEW : string
declare GAME_VIEW : string
declare SCENE_VIEW : string
declare APP : string
declare STENCIL_EQUAL : string
declare STENCIL_NOT_EQUAL : string
declare STENCIL_ALWAYS : string
declare STENCIL_KEEP : string
declare STENCIL_REPLACE : string
declare DEPTH_FN_EQUAL : string
declare SHADOW_ATLAS : RenderBuffer
declare REFLECTION_PROBES : RenderBuffer
declare CLEAR_ALL : number
declare CLEAR_DEPTH : number
declare CLEAR_COLOR : number
declare viewport_w : number
declare viewport_h : number
declare Renderer : any