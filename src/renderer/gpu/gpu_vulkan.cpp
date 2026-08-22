// Minimal Vulkan backend: presentation and a clear, intentionally no shader path.
#if defined(__linux__)
	#define VK_USE_PLATFORM_XLIB_KHR
#elif defined(_WIN32)
	#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "core/allocator.h"
#include "core/os.h"
#include "renderer/gpu/gpu.h"
#include <vulkan/vulkan.h>
#if defined(_WIN32)
	#include <windows.h>
#endif
namespace Lumix::gpu {
struct Buffer {};
struct Program {};
struct Texture {};
struct Query {};
static VkInstance I;
static VkPhysicalDevice P;
static VkDevice D;
static VkQueue Q;
static u32 QF;
static VkSurfaceKHR S;
static VkSwapchainKHR W;
static VkCommandPool CP;
static VkCommandBuffer CB;
static VkSemaphore acquire, present_done;
static VkFence F;
static bool vsync = true;
static IAllocator* A;
void preinit(IAllocator& a, bool) {
	A = &a;
}
IAllocator& getAllocator() {
	return *A;
}
static void dropSwap() {
	if (D) vkDeviceWaitIdle(D);
	if (W) vkDestroySwapchainKHR(D, W, nullptr);
	W = VK_NULL_HANDLE;
}
bool init(void* window, InitFlags) {
	VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "Lumix", 1, "Lumix", 1, VK_API_VERSION_1_0};
	const char* ex[] = {VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(__linux__)
		VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#else
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
	};
	VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	ic.pApplicationInfo = &app;
	ic.enabledExtensionCount = 2;
	ic.ppEnabledExtensionNames = ex;
	if (vkCreateInstance(&ic, nullptr, &I) != VK_SUCCESS) return false;
#if defined(__linux__)
	VkXlibSurfaceCreateInfoKHR xc{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
	xc.dpy = (Display*)os::getNativeDisplay();
	xc.window = (Window)(uintptr_t)window;
	if (vkCreateXlibSurfaceKHR(I, &xc, nullptr, &S) != VK_SUCCESS) return false;
#else
	VkWin32SurfaceCreateInfoKHR xc{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
	xc.hinstance = GetModuleHandle(nullptr);
	xc.hwnd = (HWND)window;
	if (vkCreateWin32SurfaceKHR(I, &xc, nullptr, &S) != VK_SUCCESS) return false;
#endif
	u32 n = 0;
	vkEnumeratePhysicalDevices(I, &n, nullptr);
	if (!n) return false;
	VkPhysicalDevice ds[8];
	n = n < 8 ? n : 8;
	vkEnumeratePhysicalDevices(I, &n, ds);
	for (u32 i = 0; i < n && !P; i++) {
		u32 m = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(ds[i], &m, nullptr);
		VkQueueFamilyProperties ps[32];
		m = m < 32 ? m : 32;
		vkGetPhysicalDeviceQueueFamilyProperties(ds[i], &m, ps);
		for (u32 q = 0; q < m; q++) {
			VkBool32 present = 0;
			vkGetPhysicalDeviceSurfaceSupportKHR(ds[i], q, S, &present);
			if (present && (ps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
				P = ds[i];
				QF = q;
				break;
			}
		}
	}
	if (!P) return false;
	float pr = 1;
	VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	qi.queueFamilyIndex = QF;
	qi.queueCount = 1;
	qi.pQueuePriorities = &pr;
	const char* sw = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	di.queueCreateInfoCount = 1;
	di.pQueueCreateInfos = &qi;
	di.enabledExtensionCount = 1;
	di.ppEnabledExtensionNames = &sw;
	if (vkCreateDevice(P, &di, nullptr, &D) != VK_SUCCESS) return false;
	vkGetDeviceQueue(D, QF, 0, &Q);
	VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	pi.queueFamilyIndex = QF;
	pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	vkCreateCommandPool(D, &pi, nullptr, &CP);
	VkCommandBufferAllocateInfo ba{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	ba.commandPool = CP;
	ba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ba.commandBufferCount = 1;
	vkAllocateCommandBuffers(D, &ba, &CB);
	VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	vkCreateSemaphore(D, &si, nullptr, &acquire);
	vkCreateSemaphore(D, &si, nullptr, &present_done);
	VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	vkCreateFence(D, &fi, nullptr, &F);
	setCurrentWindow(window);
	return true;
}
void setCurrentWindow(void*) {
	if (!D) return;
	dropSwap();
	VkSurfaceCapabilitiesKHR c;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(P, S, &c);
	u32 n = 0;
	VkSurfaceFormatKHR fs[16];
	vkGetPhysicalDeviceSurfaceFormatsKHR(P, S, &n, fs);
	VkSurfaceFormatKHR f = n ? fs[0] : VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	VkSwapchainCreateInfoKHR x{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	x.surface = S;
	x.minImageCount = 2;
	x.imageFormat = f.format;
	x.imageColorSpace = f.colorSpace;
	x.imageExtent = c.currentExtent;
	x.imageArrayLayers = 1;
	x.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	x.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	x.preTransform = c.currentTransform;
	x.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	x.presentMode = vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
	x.clipped = VK_TRUE;
	vkCreateSwapchainKHR(D, &x, nullptr, &W);
}
u32 present() {
	if (!W) return 0;
	u32 idx = 0;
	vkAcquireNextImageKHR(D, W, UINT64_MAX, acquire, VK_NULL_HANDLE, &idx);
	u32 n = 8;
	VkImage imgs[8];
	vkGetSwapchainImagesKHR(D, W, &n, imgs);
	vkWaitForFences(D, 1, &F, VK_TRUE, UINT64_MAX);
	vkResetFences(D, 1, &F);
	vkResetCommandBuffer(CB, 0);
	VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	vkBeginCommandBuffer(CB, &bi);
	VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.image = imgs[idx];
	b.subresourceRange = r;
	vkCmdPipelineBarrier(CB, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
	VkClearColorValue col{{.1f, .1f, .1f, 1.f}};
	vkCmdClearColorImage(CB, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &r);
	b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b.dstAccessMask = 0;
	vkCmdPipelineBarrier(CB, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
	vkEndCommandBuffer(CB);
	VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	sub.waitSemaphoreCount = 1;
	sub.pWaitSemaphores = &acquire;
	sub.pWaitDstStageMask = &stage;
	sub.commandBufferCount = 1;
	sub.pCommandBuffers = &CB;
	sub.signalSemaphoreCount = 1;
	sub.pSignalSemaphores = &present_done;
	vkQueueSubmit(Q, 1, &sub, F);
	VkPresentInfoKHR out{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	out.waitSemaphoreCount = 1;
	out.pWaitSemaphores = &present_done;
	out.swapchainCount = 1;
	out.pSwapchains = &W;
	out.pImageIndices = &idx;
	vkQueuePresentKHR(Q, &out);
	return 1;
}
void waitFrame(u32) {
	if (D) vkWaitForFences(D, 1, &F, VK_TRUE, UINT64_MAX);
}
bool frameFinished(u32) {
	return true;
}
void enableVSync(bool x) {
	vsync = x;
}
bool isVSyncEnabled() {
	return vsync;
}
bool isOriginBottomLeft() {
	return false;
}
void captureFrame() {}
void checkThread() {}
void pushGPUCounters() {}
bool getMemoryStats(MemoryStats&) {
	return false;
}
void shutdown() {
	if (!D) return;
	vkDeviceWaitIdle(D);
	dropSwap();
	vkDestroyFence(D, F, nullptr);
	vkDestroySemaphore(D, acquire, nullptr);
	vkDestroySemaphore(D, present_done, nullptr);
	vkDestroyCommandPool(D, CP, nullptr);
	vkDestroyDevice(D, nullptr);
	vkDestroySurfaceKHR(I, S, nullptr);
	vkDestroyInstance(I, nullptr);
	D = VK_NULL_HANDLE;
	I = VK_NULL_HANDLE;
}
#define EMPTY(...) \
	void __VA_ARGS__ {}
TextureHandle allocTextureHandle() {
	return new Texture;
}
BufferHandle allocBufferHandle() {
	return new Buffer;
}
ProgramHandle allocProgramHandle() {
	return new Program;
}
QueryHandle createQuery(QueryType) {
	return new Query;
}
void createProgram(ProgramHandle, StateFlags, const VertexDecl&, const char*, ShaderType, const char*) {}
void createBuffer(BufferHandle, BufferFlags, size_t, const void*, const char*) {}
void createTexture(TextureHandle, u32, u32, u32, TextureFormat, TextureFlags, const char*) {}
void createTextureView(TextureHandle, TextureHandle, u32, u32) {}
void memoryBarrier(BufferHandle) {}
void memoryBarrier(TextureHandle) {}
void barrier(TextureHandle, BarrierType) {}
void barrier(BufferHandle, BarrierType) {}
void destroy(TextureHandle p) {
	delete p;
}
void destroy(BufferHandle p) {
	delete p;
}
void destroy(ProgramHandle p) {
	delete p;
}
void destroy(QueryHandle p) {
	delete p;
}
void setFramebuffer(const TextureHandle*, u32, TextureHandle, FramebufferFlags) {}
void setFramebufferCube(TextureHandle, u32, u32) {}
void viewport(u32, u32, u32, u32) {}
void scissor(u32, u32, u32, u32) {}
void clear(ClearFlags, const float*, float) {}
void useProgram(ProgramHandle) {}
void requestDisassembly(ProgramHandle) {}
bool getDisassembly(ProgramHandle, String&) {
	return false;
}
BindlessHandle getBindlessHandle(BufferHandle) {
	return INVALID_BINDLESS_HANDLE;
}
BindlessHandle getBindlessHandle(TextureHandle) {
	return INVALID_BINDLESS_HANDLE;
}
RWBindlessHandle getRWBindlessHandle(BufferHandle) {
	return INVALID_RW_BINDLESS_HANDLE;
}
RWBindlessHandle getRWBindlessHandle(TextureHandle) {
	return INVALID_RW_BINDLESS_HANDLE;
}
void bindIndexBuffer(BufferHandle) {}
void bindVertexBuffer(u32, BufferHandle, u32, u32) {}
void bindUniformBuffer(u32, BufferHandle, size_t, size_t) {}
void bindIndirectBuffer(BufferHandle) {}
void bindShaderBuffers(Span<BufferHandle>) {}
void drawArrays(u32, u32) {}
void drawIndirect(DataType, u32) {}
void drawIndexed(u32, u32, DataType) {}
void drawArraysInstanced(u32, u32) {}
void drawIndexedInstanced(u32, u32, DataType) {}
void draw(const Drawcall&) {}
void dispatch(u32, u32, u32) {}
void copy(TextureHandle, TextureHandle, u32, u32) {}
void copy(BufferHandle, BufferHandle, u32, u32, u32) {}
void copy(BufferHandle, TextureHandle) {}
void readTexture(TextureHandle, TextureReadCallback) {}
void setDebugName(TextureHandle, const char*) {}
void update(TextureHandle, u32, u32, u32, u32, u32, u32, TextureFormat, const void*, u32) {}
void update(BufferHandle, const void*, size_t) {}
void* map(BufferHandle, size_t) {
	return nullptr;
}
void unmap(BufferHandle) {}
void queryTimestamp(QueryHandle) {}
void beginQuery(QueryHandle) {}
void endQuery(QueryHandle) {}
u64 getQueryResult(QueryHandle) {
	return 0;
}
u64 getQueryFrequency() {
	return 1;
}
bool isQueryReady(QueryHandle) {
	return true;
}
void pushDebugGroup(const char*) {}
void popDebugGroup() {}
} // namespace Lumix::gpu
