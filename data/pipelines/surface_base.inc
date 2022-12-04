function surface_shader_ex(args)
	args.vertex_preface = args.vertex_preface or ""
	args.fragment_preface = args.fragment_preface or ""
	args.vertex = args.vertex or ""
	
	if args.need_local_position then
		args.vertex_preface = args.vertex_preface .. "\nlayout(location = 8) out vec3 v_local_position;\n"
		args.vertex = args.vertex .. "\nv_local_position = a_position;\n"
		args.fragment_preface = args.fragment_preface .. "\nlayout(location = 8) in vec3 v_local_position;\n"
	end

	if args.texture_slots == nil then
		args.texture_slots = {
			{
				name = "Albedo",
				default_texture = "textures/common/white.tga"
			},
			{
				name = "Normal",
				default_texture = "textures/common/default_normal.tga"
			},
			{
				name = "Roughness",
				default_texture = "textures/common/white.tga"
			},
			{
				name = "Metallic",
				define = "HAS_METALLICMAP"
			},
			{
				name = "Ambient occlusion",
				define = "HAS_AMBIENT_OCCLUSION_TEX"
			}
		}
	end
	for _, slot in ipairs(args.texture_slots) do
		texture_slot(slot)
	end

	include "pipelines/common.glsl"

	for _, v in ipairs(args.includes or {}) do
		include(v)
	end

	define "ALPHA_CUTOUT"
	
	------------------

	function toVarName(prefix, name) 
		local res = prefix .. "_"
		
		for idx = 1, #name do
			local c = name:byte(idx)
			if (c >= string.byte("A") and c <= string.byte("Z")) or (c >= string.byte("a") and c <= string.byte("z")) or (c >= string.byte("0") and c <= string.byte("9")) then
				res = res .. string.char(c)
			else 
				res = res .. "_"
			end
		end

		return string.lower(res)
	end

	common([[
		#ifdef SKINNED
			layout(std140, binding = 4) uniform ModelState {
				float fur_scale;
				float fur_gravity;
				float layers;
				float padding;
				mat4 matrix;
				mat2x4 bones[255];
			} Model;
		#endif

		#if !defined GRASS
			#define HAS_LOD
		#endif
	]])

	for idx, slot in ipairs(args.texture_slots) do
		if slot.define ~= nil then
			common("#ifdef " .. slot.define .. "\n")
		end
		common("layout (binding=" .. tostring(idx - 1) .. ") uniform sampler2D " .. toVarName("t", slot.name) .. ";\n")
		if slot.define ~= nil then
			common("#endif\n")
		end
	end


	vertex_shader([[
		layout(location = 0) in vec3 a_position;
		#ifdef _HAS_ATTR1
			layout(location = 1) in vec2 a_uv;
		#else
			const vec2 a_uv = vec2(0);
		#endif
		layout(location = 2) in vec3 a_normal;
		#ifdef _HAS_ATTR3
			layout(location = 3) in vec3 a_tangent;
		#else 
			const vec3 a_tangent = vec3(0, 1, 0);
		#endif
		#if defined SKINNED
			layout(location = 4) in ivec4 a_indices;
			layout(location = 5) in vec4 a_weights;
		#elif defined INSTANCED
			layout(location = 4) in vec4 i_rot_lod;
			layout(location = 5) in vec4 i_pos_scale;
			#ifdef _HAS_ATTR6
				layout(location = 6) in vec4 a_color;
				layout(location = 5) out vec4 v_color;
			#endif
		#elif defined GRASS
			layout(location = 4) in vec4 i_pos_scale;
			layout(location = 5) in vec4 i_rot;
			#ifdef _HAS_ATTR6
				layout(location = 6) in vec4 a_color;
				layout(location = 5) out vec4 v_color;
			#endif
			layout(location = 7) out float v_pos_y;
		#else
			layout(std140, binding = 4) uniform ModelState {
				mat4 matrix;
			} Model;
		#endif
		#if defined GRASS
			layout(std140, binding = 4) uniform ModelState {
				vec3 u_grass_origin;
			} Grass;
		#endif
		#ifdef HAS_LOD
			layout(location = 4) out float v_lod;
		#endif
		#ifdef _HAS_ATTR7
			layout(location = 7) in float a_ao;
			layout(location = 6) out float v_ao;
		#endif
	
		layout(location = 0) out vec2 v_uv;
		layout(location = 1) out vec3 v_normal;
		layout(location = 2) out vec3 v_tangent;
		layout(location = 3) out vec4 v_wpos;
		#ifdef FUR
			layout(location = 7) out float v_fur_layer;
		#endif
	]] .. args.vertex_preface .. [[	
		void main() {
			v_uv = a_uv;
			#ifdef _HAS_ATTR7
				v_ao = a_ao;
			#endif

			#if defined INSTANCED
				vec4 rot_quat = vec4(i_rot_lod.xyz, 0);
				rot_quat.w = sqrt(saturate(1 - dot(rot_quat.xyz, rot_quat.xyz)));
				v_normal = rotateByQuat(rot_quat, a_normal);
				v_tangent = rotateByQuat(rot_quat, a_tangent);
				vec3 p = a_position * i_pos_scale.w;
				#ifdef HAS_LOD
					v_lod = i_rot_lod.w;
				#endif
				v_wpos = vec4(i_pos_scale.xyz + rotateByQuat(rot_quat, p), 1);
				#ifdef _HAS_ATTR6
					v_color = a_color;
				#endif
			#elif defined GRASS
				v_normal = rotateByQuat(i_rot, a_normal);
				v_tangent = rotateByQuat(i_rot, a_tangent);
				vec3 p = a_position;
				v_pos_y = p.y;
				v_wpos = vec4(i_pos_scale.xyz + rotateByQuat(i_rot, a_position * i_pos_scale.w), 1);
				#ifdef GRASS
					v_wpos.xyz += Grass.u_grass_origin;
				#endif
				#ifdef _HAS_ATTR6
					v_color = a_color;
				#endif
			#elif defined SKINNED
				mat2x4 dq = a_weights.x * Model.bones[a_indices.x];
				float w = dot(Model.bones[a_indices.y][0], Model.bones[a_indices.x][0]) < 0 ? -a_weights.y : a_weights.y;
				dq += w * Model.bones[a_indices.y];
				w = dot(Model.bones[a_indices.z][0], Model.bones[a_indices.x][0]) < 0 ? -a_weights.z : a_weights.z;
				dq += w * Model.bones[a_indices.z];
				w = dot(Model.bones[a_indices.w][0], Model.bones[a_indices.x][0]) < 0 ? -a_weights.w : a_weights.w;
				dq += w * Model.bones[a_indices.w];
			
				dq *= 1 / length(dq[0]);

				mat3 m = mat3(Model.matrix);
				v_normal = m * rotateByQuat(dq[0], a_normal);
				v_tangent = m * rotateByQuat(dq[0], a_tangent);
				vec3 mpos;
				#ifdef FUR
					v_fur_layer = gl_InstanceID / Model.layers;
					mpos = a_position + (a_normal + vec3(0, -Model.fur_gravity * v_fur_layer, 0)) * v_fur_layer * Model.fur_scale;
				#else
					mpos = a_position;
				#endif
				v_wpos = Model.matrix * vec4(transformByDualQuat(dq, mpos), 1);
			#else 
				mat4 model_mtx = Model.matrix;
				v_normal = mat3(model_mtx) * a_normal;
				v_tangent = mat3(model_mtx) * a_tangent;

				vec3 p = a_position;

				v_wpos = model_mtx * vec4(p,  1);
			#endif
	]] .. args.vertex .. [[
			gl_Position = Pass.view_projection * v_wpos;		
		}
	]])

	---------------------

	fragment_shader([[
		layout (binding=5) uniform sampler2D u_shadowmap;
		#if !defined DEPTH && !defined DEFERRED && !defined GRASS
			layout (binding=6) uniform sampler2D u_shadow_atlas;
			layout (binding=7) uniform samplerCubeArray u_reflection_probes;
			layout (binding=8) uniform sampler2D u_depthbuffer;
		#endif
	
		layout(location = 0) in vec2 v_uv;
		layout(location = 1) in vec3 v_normal;
		layout(location = 2) in vec3 v_tangent;
		layout(location = 3) in vec4 v_wpos;
		#ifdef HAS_LOD
			layout(location = 4) in float v_lod;
		#endif
		#ifdef _HAS_ATTR6
			layout(location = 5) in vec4 v_color;
		#endif
		#ifdef _HAS_ATTR7
			layout(location = 6) in float v_ao;
		#endif
		#ifdef FUR
			layout(location = 7) in float v_fur_layer;
		#endif
		#ifdef GRASS
			layout(location = 7) in float v_pos_y;
		#endif

		#ifndef DEPTH
			#if defined DEFERRED || defined GRASS
				layout(location = 0) out vec4 o_gbuffer0;
				layout(location = 1) out vec4 o_gbuffer1;
				layout(location = 2) out vec4 o_gbuffer2;
			#else
				layout(location = 0) out vec4 o_color;
			#endif
		#endif

	]] .. args.fragment_preface .. [[	

		Surface getSurface()
		{
			#ifdef HAS_LOD
				if (ditherLOD(v_lod)) discard;
			#endif
			Surface data;
			data.wpos = v_wpos.xyz;
			data.V = normalize(-data.wpos);
]] .. args.fragment .. [[
			return data;
		}
	
		#ifdef DEPTH
			void main()
			{
				#ifdef HAS_LOD
					if (ditherLOD(v_lod)) discard;
				#endif
				#ifdef ALPHA_CUTOUT
					Surface data = getSurface();
					if(data.alpha < 0.5) discard;
				#endif
			}
		#elif defined DEFERRED || defined GRASS
			void main()
			{
				Surface data = getSurface();
				#if defined ALPHA_CUTOUT
					if(data.alpha < 0.5) discard;
				#endif
				packSurface(data, o_gbuffer0, o_gbuffer1, o_gbuffer2);
			}
		#else 
			void main()
			{
				Surface data = getSurface();
			
				float linear_depth = dot(data.wpos.xyz, Pass.view_dir.xyz);
				Cluster cluster = getClusterLinearDepth(linear_depth);
				o_color.rgb = computeLighting(cluster, data, Global.light_dir.xyz, Global.light_color.rgb * Global.light_intensity, u_shadowmap, u_shadow_atlas, u_reflection_probes);

				#if defined ALPHA_CUTOUT
					if(data.alpha < 0.5) discard;
				#endif
				o_color.a = data.alpha;
			}
		#endif
	]])
end

function surface_shader(code)
	surface_shader_ex({fragment = code})
end
