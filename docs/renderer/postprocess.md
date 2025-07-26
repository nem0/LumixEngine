# Custom postprocess

1. Create a new class, which inherits from `RenderPlugin`.
2. Implement needed methods in your class. Implement necessary shaders. Here is an example for a film grain postprocess:

	```cpp
	struct FilmGrain : public RenderPlugin {
		Renderer& m_renderer;
		Shader* m_shader = nullptr;
		float m_noise_scale = 2.f;

		FilmGrain(Renderer& renderer) : m_renderer(renderer) {}

		void shutdown() {
			m_shader->decRefCount();
		}

		void init() {
			ResourceManagerHub& rm = m_renderer.getEngine().getResourceManager();
			m_shader = rm.load<Shader>(Path("shaders/film_grain.hlsl"));
		}

		RenderBufferHandle renderAfterTonemap(const GBuffer& gbuffer, RenderBufferHandle input, Pipeline& pipeline) override {
			if (!m_shader->isReady()) return input;
			if (pipeline.getType() != PipelineType::GAME_VIEW) return input;

			RenderModule* module = pipeline.getModule();
			EntityPtr camera_entity = module->getActiveCamera();
			if (!camera_entity.isValid()) return input;

			Camera& camera = module->getCamera(*camera_entity);
			if (camera.film_grain_intensity <= 1e-5) return input;

			pipeline.beginBlock("film_grain");

			DrawStream& stream = pipeline.getRenderer().getDrawStream();
			const Viewport& vp = pipeline.getViewport();
			struct {
				float intensity;
				float lumamount;
				gpu::RWBindlessHandle source;
			} ubdata = {
				camera.film_grain_intensity,
				0.1f,
				pipeline.toRWBindless(input, stream),
			};
			pipeline.setUniform(ubdata);
			pipeline.dispatch(*m_shader, (vp.w + 15) / 16, (vp.h + 15) / 16, 1);

			pipeline.endBlock();
			return input;
		}
	};
	```
	
3. Create an instance of your class. Let's call it `my_postprocess`.
4. Add your postprocess to a renderer using `addPlugin`.

	```cpp
	Renderer& renderer = ...;
	renderer.addPlugin(my_postprocess);
	```

	Your postprocess should just work now.