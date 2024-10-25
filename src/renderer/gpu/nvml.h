#pragma once

extern "C" {

typedef enum nvmlReturn_enum {
	NVML_SUCCESS = 0,
	NVML_ERROR_UNINITIALIZED = 1,
	NVML_ERROR_INVALID_ARGUMENT = 2,
	NVML_ERROR_NOT_SUPPORTED = 3,
	NVML_ERROR_NO_PERMISSION = 4,
	NVML_ERROR_ALREADY_INITIALIZED = 5,
	NVML_ERROR_NOT_FOUND = 6,
	NVML_ERROR_INSUFFICIENT_SIZE = 7,
	NVML_ERROR_INSUFFICIENT_POWER = 8,
	NVML_ERROR_DRIVER_NOT_LOADED = 9,
	NVML_ERROR_TIMEOUT = 10,
	NVML_ERROR_IRQ_ISSUE = 11,
	NVML_ERROR_LIBRARY_NOT_FOUND = 12,
	NVML_ERROR_FUNCTION_NOT_FOUND = 13,
	NVML_ERROR_CORRUPTED_INFOROM = 14,
	NVML_ERROR_GPU_IS_LOST = 15,
	NVML_ERROR_RESET_REQUIRED = 16,
	NVML_ERROR_OPERATING_SYSTEM = 17,
	NVML_ERROR_LIB_RM_VERSION_MISMATCH = 18,
	NVML_ERROR_IN_USE = 19,
	NVML_ERROR_MEMORY = 20,
	NVML_ERROR_NO_DATA = 21,
	NVML_ERROR_VGPU_ECC_NOT_SUPPORTED = 22,
	NVML_ERROR_INSUFFICIENT_RESOURCES = 23,
	NVML_ERROR_UNKNOWN = 999
} nvmlReturn_t;

typedef enum nvmlClockType_enum {
	NVML_CLOCK_GRAPHICS = 0,
	NVML_CLOCK_SM = 1,
	NVML_CLOCK_MEM = 2,
	NVML_CLOCK_VIDEO = 3,

	NVML_CLOCK_COUNT
} nvmlClockType_t;

typedef struct nvmlMemory_st {
	unsigned long long total;
	unsigned long long free;
	unsigned long long used;
} nvmlMemory_t;


typedef struct nvmlDevice_st* nvmlDevice_t;

nvmlReturn_t (*nvmlInit_v2)(void);
nvmlReturn_t (*nvmlShutdown)(void);
nvmlReturn_t (*nvmlDeviceGetHandleByIndex_v2)(unsigned int index, nvmlDevice_t* device);
nvmlReturn_t (*nvmlDeviceGetName)(nvmlDevice_t device, char* name, unsigned int length);
nvmlReturn_t (*nvmlDeviceGetMemoryInfo)(nvmlDevice_t device, nvmlMemory_t* memory);
nvmlReturn_t (*nvmlDeviceGetClockInfo)(nvmlDevice_t device, nvmlClockType_t type, unsigned int* clock);
nvmlReturn_t (*nvmlDeviceSetApplicationsClocks)(nvmlDevice_t device, unsigned int memClockMHz, unsigned int graphicsClockMHz);
nvmlReturn_t (*nvmlDeviceGetMaxClockInfo)(nvmlDevice_t device, nvmlClockType_t type, unsigned int *clock);
nvmlReturn_t (*nvmlDeviceResetApplicationsClocks)(nvmlDevice_t device);
nvmlReturn_t (*nvmlDeviceGetSupportedMemoryClocks)(nvmlDevice_t device, unsigned int *count, unsigned int *clocksMHz);
nvmlReturn_t (*nvmlDeviceGetSupportedGraphicsClocks)(nvmlDevice_t device, unsigned int memoryClockMHz, unsigned int *count, unsigned int *clocksMHz);


}