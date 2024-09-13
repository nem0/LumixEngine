include "pipelines/common.glsl"

function surface_shader_ex(args)
	args.vertex_preface = args.vertex_preface or ""
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

	common [[
		#if !defined GRASS && !defined PARTICLES
			#define HAS_LOD
		#endif

		struct VSOutput {
			float4 wpos : TEXCOORD0;
			float3 normal : TEXCOORD1;
			#ifdef UV0_ATTR
				float2 uv : TEXCOORD2;
			#endif
			#ifdef TANGENT_ATTR
				float3 tangent : TEXCOORD3;
			#endif
			#if defined DYNAMIC || defined SKINNED
				float4 prev_ndcpos_no_jitter : TEXCOORD4;
			#endif
			#ifdef HAS_LOD
				float lod : TEXCOORD5;
			#endif
			#ifdef GRASS
				#ifdef COLOR0_ATTR
					//float4 color : TEXCOORD6;
				#endif
				float pos_y : TEXCOORD7;
			#endif
			float4 position : SV_POSITION;
		};
	]]

	vertex_shader(args.vertex_preface .. [[
		#line 65 "surface_base.inc"
		#define ATTR(X) TEXCOORD##X
		struct Input {
			float3 position : ATTR(0);
			float3 normal : ATTR(NORMAL_ATTR);
			#ifdef INDICES_ATTR
				int4 indices : ATTR(INDICES_ATTR);
				float4 weights : ATTR(WEIGHTS_ATTR);
			#endif
			#ifdef AUTOINSTANCED
				float4 i_rot : ATTR(INSTANCE0_ATTR);
				float4 i_pos_lod : ATTR(INSTANCE1_ATTR);
				float4 i_scale : ATTR(INSTANCE2_ATTR);
			#elif defined INSTANCED
				float4 i_rot_lod : ATTR(INSTANCE0_ATTR);
				float4 i_pos_scale : ATTR(INSTANCE1_ATTR);
			#elif defined DYNAMIC
				float4 i_rot : ATTR(INSTANCE0_ATTR);
				float4 i_pos_lod : ATTR(INSTANCE1_ATTR);
				float4 i_scale : ATTR(INSTANCE2_ATTR);
				float4 i_prev_rot : ATTR(INSTANCE3_ATTR);
				float4 i_prev_pos_lod : ATTR(INSTANCE4_ATTR);
				float4 i_prev_scale : ATTR(INSTANCE5_ATTR);
			#elif defined SKINNED
			#elif defined GRASS
				float4 i_pos_scale : ATTR(INSTANCE0_ATTR);
				float4 i_rot : ATTR(INSTANCE1_ATTR);
				#ifdef COLOR0_ATTR
					//float4 color : ATTR(COLOR0_ATTR)
				#endif
			#else
			#endif

			#ifdef UV0_ATTR
				float2 uv : ATTR(UV0_ATTR);
			#endif
			#ifdef TANGENT_ATTR
				float3 tangent : ATTR(TANGENT_ATTR);
			#endif
		};

		#ifdef SKINNED
			cbuffer ModelState : register(b4) {
				float fur_scale;
				float fur_gravity;
				float layers;
				float padding;
				row_major float4x4 mtx;
				row_major float4x4 prev_matrix;
				row_major float2x4 bones[255];
			}
		#elif defined INSTANCED
		#elif defined AUTOINSTANCED
		#elif defined GRASS
			cbuffer ModelState : register(b4) {
				float3 u_grass_origin;
			};
		#elif defined DYNAMIC
		#else
			cbuffer ModelState : register(b4) {
				row_major float4x4 model_mtx;
			};
		#endif

		VSOutput main(Input input) {
			VSOutput output;
			#ifdef HAS_LOD
				output.lod = 0;
			#endif
			#ifdef TANGENT_ATTR
				output.tangent = input.tangent;
			#endif
			#ifdef UV0_ATTR
				output.uv = input.uv;
			#endif
			#ifdef AUTOINSTANCED
				float3 p = input.position.xyz * input.i_scale.xyz;
				output.wpos = float4(input.i_pos_lod.xyz + rotateByQuat(input.i_rot, p), 1);
				output.position = mul(output.wpos, Pass_view_projection);
				output.normal = rotateByQuat(input.i_rot, input.normal);
				#ifdef HAS_LOD
					output.lod = input.i_pos_lod.w;
				#endif
			#elif defined INSTANCED
				float4 rot_quat = float4(input.i_rot_lod.xyz, 0);
				rot_quat.w = sqrt(saturate(1 - dot(rot_quat.xyz, rot_quat.xyz)));
				output.normal = rotateByQuat(rot_quat, input.normal);
				#ifdef TANGENT_ATTR
					output.tangent = rotateByQuat(rot_quat, input.tangent);
				#endif
				float3 p = input.position * input.i_pos_scale.w;
				output.wpos = float4(input.i_pos_scale.xyz + rotateByQuat(rot_quat, p), 1);
				output.position = mul(output.wpos, Pass_view_projection);
			#elif defined GRASS
				output.normal = rotateByQuat(input.i_rot, input.normal);
				#ifdef TANGENT_ATTR
					output.tangent = rotateByQuat(input.i_rot, input.tangent);
				#endif
				float3 p = input.position;
				output.pos_y = p.y;
				output.wpos = float4(input.i_pos_scale.xyz + rotateByQuat(input.i_rot, input.position * input.i_pos_scale.w), 1);
				output.wpos.xyz += u_grass_origin;
				#ifdef COLOR0_ATTR
					//output.color = input.color;
				#endif
				output.position = mul(output.wpos, Pass_view_projection);
			#elif defined DYNAMIC
				output.normal = rotateByQuat(input.i_rot, input.normal);
				#ifdef TANGENT_ATTR
					output.tangent = rotateByQuat(input.i_rot, input.tangent);
				#endif
				output.wpos = float4(input.i_pos_lod.xyz + rotateByQuat(input.i_rot, input.position * input.i_scale.xyz), 1);
				output.position = mul(output.wpos, Pass_view_projection);
				output.prev_ndcpos_no_jitter = float4(input.i_prev_pos_lod.xyz + rotateByQuat(input.i_prev_rot, input.position * input.i_prev_scale.xyz), 1);
				output.prev_ndcpos_no_jitter = mul(output.prev_ndcpos_no_jitter, mul(Global_view_projection_no_jitter, Global_reprojection));
			#elif defined SKINNED
				float2x4 dq = mul(bones[input.indices.x], input.weights.x);
				float w = dot(bones[input.indices.y][0], bones[input.indices.x][0]) < 0 ? -input.weights.y : input.weights.y;
				dq += mul(bones[input.indices.y], w);
				w = dot(bones[input.indices.z][0], bones[input.indices.x][0]) < 0 ? -input.weights.z : input.weights.z;
				dq += mul(bones[input.indices.z], w);
				w = dot(bones[input.indices.w][0], bones[input.indices.x][0]) < 0 ? -input.weights.w : input.weights.w;
				dq += mul(bones[input.indices.w], w);
			
				dq *= 1 / length(dq[0]);

				float3x3 m = (float3x3)mtx;
				output.normal = mul(rotateByQuat(dq[0], input.normal), m);
				#ifdef TANGENT_ATTR
					output.tangent = mul(rotateByQuat(dq[0], input.tangent), m);
				#endif
				float3 mpos;
				#ifdef FUR
					v_fur_layer = gl_InstanceID / layers;
					mpos = input.position + (input.normal + float3(0, -fur_gravity * input.fur_layer, 0)) * input.fur_layer * fur_scale;
				#else
					mpos = input.position;
				#endif
				output.wpos = mul(float4(transformByDualQuat(dq, mpos), 1), mtx);
				output.position = mul(output.wpos, Pass_view_projection);
				// TODO previous frame bone positions
				output.prev_ndcpos_no_jitter = mul(float4(transformByDualQuat(dq, mpos), 1), prev_matrix);
				output.prev_ndcpos_no_jitter = mul(output.prev_ndcpos_no_jitter, mul(Global_view_projection_no_jitter, Global_reprojection));
			#else
				float3x3 rot_mtx = (float3x3)model_mtx;
				output.normal =  mul(input.normal, rot_mtx);
				#ifdef TANGENT_ATTR
					output.tangent = mul(input.tangent, rot_mtx);
				#endif
				output.wpos = mul(float4(input.position, 1), model_mtx);
				output.position = mul(output.wpos, Pass_view_projection);
			#endif
			return output;
		}
	]])

	fragment_shader([[
		typedef VSOutput Input;
		
		Surface getSurface(Input input) {
			Surface data;
			data.motion = 0.0f.xx;
			float4 p = mul(input.wpos, Global_view_projection_no_jitter);
			#if defined DYNAMIC || defined SKINNED
				float2 prev_pos_projected = input.prev_ndcpos_no_jitter.xy / input.prev_ndcpos_no_jitter.w;
				data.motion = prev_pos_projected.xy - p.xy / p.w;
			#else
				data.motion = computeStaticObjectMotionVector(input.wpos.xyz);
			#endif
	]] .. args.fragment .. [[
			#line 234 "surface_base.inc"
			data.V = normalize(-data.wpos);
			return data;
		}

		#ifdef DEPTH
			void main(Input input) {
				#ifdef HAS_LOD
					if (ditherLOD(input.lod, input.position.xy)) discard;
				#endif
			}
		#elif defined DEFERRED || defined GRASS
			GBufferOutput main(Input input) {
				#ifdef HAS_LOD
					if (ditherLOD(input.lod, input.position.xy)) discard;
				#endif

				return packSurface(getSurface(input));
			}
		#else
			cbuffer Drawcall2 : register(b5) {
				uint u_shadowmap;
				uint u_shadow_atlas;
				uint u_reflection_probes;
			};

			float4 main(Input input, float4 frag_coord : SV_POSITION) : SV_TARGET{
				#ifdef HAS_LOD
					if (ditherLOD(input.lod, input.position.xy)) discard;
				#endif
				
				Surface data = getSurface(input);
			
				float linear_depth = dot(data.wpos.xyz, Pass_view_dir.xyz);
				Cluster cluster = getClusterLinearDepth(linear_depth, frag_coord.xy);
				float4 result;
				result.rgb = computeLighting(cluster, data, Global_light_dir.xyz, Global_light_color.rgb * Global_light_intensity, u_shadowmap, u_shadow_atlas, u_reflection_probes, frag_coord);

				#if defined ALPHA_CUTOUT
					if(data.alpha < 0.5) discard;
				#endif
				result.a = data.alpha;
				return result;
			}
		#endif
	]])
end

function surface_shader(code) 
	local args = { fragment = code }
	surface_shader_ex(args)
end

