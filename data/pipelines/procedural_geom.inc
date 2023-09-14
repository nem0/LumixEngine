function procedural_geom_shader(args)
	args.user_channels = args.user_channels or 3
	include "pipelines/common.glsl"
	
	vertex_shader([[
		layout(location = 0) in vec3 a_position;
		layout(location = 1) in vec2 a_uv;
		layout(location = 2) in vec3 a_normal;
		layout(location = 3) in vec3 a_tangent;
		#ifdef _HAS_ATTR4
			layout(location = 4) in vec]] .. tostring(args.user_channels) .. [[ a_user;
		#endif
	
		layout(location = 0) out vec2 v_uv;
		layout(location = 1) out vec3 v_normal;
		layout(location = 2) out vec3 v_tangent;
		layout(location = 3) out vec4 v_wpos;
		#ifdef _HAS_ATTR4
			layout(location = 4) out vec]] .. tostring(args.user_channels) .. [[ v_user;
		#endif
	
		layout(std140, binding = 4) uniform ModelState {
			mat4 matrix;
		} Model;
	
		void main() {
			#ifdef _HAS_ATTR4
				v_user = a_user;
			#endif
			
			v_uv = a_uv;
			mat4 model_mtx = Model.matrix;
			v_normal = mat3(model_mtx) * a_normal;
			v_tangent = mat3(model_mtx) * a_tangent;
			v_wpos = model_mtx * vec4(a_position,  1);
			gl_Position = Pass.view_projection * v_wpos;		
		}
	]])
	
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
		#ifdef _HAS_ATTR4
			layout(location = 4) in vec]] .. tostring(args.user_channels) .. [[ v_user;
		#endif
	
		#ifndef DEPTH
			#if defined DEFERRED
				layout(location = 0) out vec4 o_gbuffer0;
				layout(location = 1) out vec4 o_gbuffer1;
				layout(location = 2) out vec4 o_gbuffer2;
				layout(location = 3) out vec4 o_gbuffer3;
			#else
				layout(location = 0) out vec4 o_color;
			#endif
		#endif
	
		Surface getSurface() {
			Surface data;
			data.wpos = v_wpos.xyz;
			data.V = normalize(-data.wpos);
]] .. args.code .. [[
			return data;
		}
	
		#ifdef DEPTH
			void main() {}
		#elif defined DEFERRED || defined GRASS
			void main() {
				Surface data = getSurface();
				packSurface(data, o_gbuffer0, o_gbuffer1, o_gbuffer2, o_gbuffer2);
			}
		#else 
			void main() {
				Surface data = getSurface();
			
				float linear_depth = dot(data.wpos.xyz, Pass.view_dir.xyz);
				Cluster cluster = getClusterLinearDepth(linear_depth);
				o_color.rgb = computeLighting(cluster, data, Global.light_dir.xyz, Global.light_color.rgb * Global.light_intensity, u_shadowmap, u_shadow_atlas, u_reflection_probes);
				o_color.a = data.alpha;
			}
		#endif
	]])
end