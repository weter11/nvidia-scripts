/*
 * NVIDIA GPU Stats Reader
 * 
 * This simple C program demonstrates how to read NVIDIA GPU:
 * - Core Voltage (using undocumented NVAPI call 0x465f9bcf)
 * - Hotspot Temperature (using undocumented NVAPI call 0x65fe3aad)
 * - Memory Temperature (using undocumented NVAPI call 0x65fe3aad)
 *
 * Based on LACT (Linux AMDGPU Controller Tool) implementation.
 * Reference: https://github.com/weter11/LACT
 *
 * Compile: gcc -o nvidia_stats nvidia_stats.c -ldl
 * Run: ./nvidia_stats
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

/* NVAPI Constants */
#define NVAPI_LIBRARY "libnvidia-api.so.1"
#define NVAPI_MAX_PHYSICAL_GPUS 64
#define NVAPI_SHORT_STRING_MAX 64

/* NVAPI Query Interface IDs */
#define QUERY_NVAPI_INITIALIZE       0x0150e828
#define QUERY_NVAPI_UNLOAD           0xd22bdd7e
#define QUERY_NVAPI_ENUM_PHYSICAL_GPUS 0xe5ac921f
#define QUERY_NVAPI_GET_BUS_ID       0x1be0b8e5
#define QUERY_NVAPI_GET_ERROR_MESSAGE 0x6c2d048c
#define QUERY_NVAPI_THERMALS         0x65fe3aad  /* Undocumented call */
#define QUERY_NVAPI_VOLTAGE          0x465f9bcf  /* Undocumented call */

/* Type definitions */
typedef int32_t NvAPI_Status;
typedef void* NvPhysicalGpuHandle;

/* Function pointer type for nvapi_QueryInterface */
typedef void* (*NvAPI_QueryInterface_t)(uint32_t id);

/* Function pointer types for NVAPI functions */
typedef NvAPI_Status (*NvAPI_Initialize_t)(void);
typedef NvAPI_Status (*NvAPI_Unload_t)(void);
typedef NvAPI_Status (*NvAPI_EnumPhysicalGPUs_t)(NvPhysicalGpuHandle handles[], uint32_t *count);
typedef NvAPI_Status (*NvAPI_GetErrorMessage_t)(NvAPI_Status status, char text[NVAPI_SHORT_STRING_MAX]);

/*
 * NvApiThermals structure
 * Used with undocumented call QUERY_NVAPI_THERMALS (0x65fe3aad)
 * 
 * The version field encodes struct size and version number:
 * version = (sizeof(struct) | (version_number << 16))
 * 
 * Temperature values are stored in the values array and need to be divided by 256.
 * - Hotspot temperature is at index 9
 * - VRAM/Memory temperature is at index 15
 */
typedef struct {
    uint32_t version;
    int32_t mask;
    int32_t values[40];
} NvApiThermals;

typedef NvAPI_Status (*NvAPI_GetThermals_t)(NvPhysicalGpuHandle handle, NvApiThermals *thermals);

/*
 * NvApiVoltage structure
 * Used with undocumented call QUERY_NVAPI_VOLTAGE (0x465f9bcf)
 * 
 * The version field encodes struct size and version number:
 * version = (sizeof(struct) | (version_number << 16))
 * 
 * value_uv contains the voltage in microvolts (µV)
 */
typedef struct {
    uint32_t version;
    uint32_t flags;
    uint32_t padding_1[8];
    uint32_t value_uv;
    uint32_t padding_2[8];
} NvApiVoltage;

typedef NvAPI_Status (*NvAPI_GetVoltage_t)(NvPhysicalGpuHandle handle, NvApiVoltage *voltage);

/* Global variables */
static void *nvapi_lib = NULL;
static NvAPI_QueryInterface_t nvapi_QueryInterface = NULL;

/*
 * Load NVAPI library and get the query interface function
 */
int load_nvapi(void) {
    nvapi_lib = dlopen(NVAPI_LIBRARY, RTLD_NOW);
    if (!nvapi_lib) {
        fprintf(stderr, "Error: Could not load %s: %s\n", NVAPI_LIBRARY, dlerror());
        fprintf(stderr, "Make sure NVIDIA drivers are installed.\n");
        return -1;
    }

    nvapi_QueryInterface = (NvAPI_QueryInterface_t)dlsym(nvapi_lib, "nvapi_QueryInterface");
    if (!nvapi_QueryInterface) {
        fprintf(stderr, "Error: Could not find nvapi_QueryInterface: %s\n", dlerror());
        dlclose(nvapi_lib);
        return -1;
    }

    return 0;
}

/*
 * Get a function pointer from NVAPI using query interface
 */
void* get_nvapi_function(uint32_t id) {
    void *func = nvapi_QueryInterface(id);
    if (!func) {
        fprintf(stderr, "Error: Could not get function for ID 0x%08x\n", id);
    }
    return func;
}

/*
 * Initialize NVAPI
 */
int init_nvapi(void) {
    NvAPI_Initialize_t initialize = (NvAPI_Initialize_t)get_nvapi_function(QUERY_NVAPI_INITIALIZE);
    if (!initialize) return -1;

    NvAPI_Status status = initialize();
    if (status != 0) {
        fprintf(stderr, "Error: NvAPI_Initialize failed with status 0x%08x\n", status);
        return -1;
    }

    printf("NVAPI initialized successfully.\n\n");
    return 0;
}

/*
 * Unload NVAPI
 */
void unload_nvapi(void) {
    if (nvapi_QueryInterface) {
        NvAPI_Unload_t unload = (NvAPI_Unload_t)get_nvapi_function(QUERY_NVAPI_UNLOAD);
        if (unload) {
            unload();
        }
    }

    if (nvapi_lib) {
        dlclose(nvapi_lib);
    }
}

/*
 * Enumerate physical GPUs
 */
int enum_physical_gpus(NvPhysicalGpuHandle *handles, uint32_t *count) {
    NvAPI_EnumPhysicalGPUs_t enum_gpus = (NvAPI_EnumPhysicalGPUs_t)get_nvapi_function(QUERY_NVAPI_ENUM_PHYSICAL_GPUS);
    if (!enum_gpus) return -1;

    NvAPI_Status status = enum_gpus(handles, count);
    if (status != 0) {
        fprintf(stderr, "Error: EnumPhysicalGPUs failed with status 0x%08x\n", status);
        return -1;
    }

    return 0;
}

/*
 * Calculate the thermals mask by probing which bits return valid data
 * This is necessary because different GPUs support different sensors
 */
int32_t calculate_thermals_mask(NvPhysicalGpuHandle handle) {
    NvAPI_GetThermals_t get_thermals = (NvAPI_GetThermals_t)get_nvapi_function(QUERY_NVAPI_THERMALS);
    if (!get_thermals) return 1;

    NvApiThermals thermals;
    memset(&thermals, 0, sizeof(thermals));
    
    /* Version: struct size | (version 2 << 16) */
    thermals.version = sizeof(NvApiThermals) | (2 << 16);
    thermals.mask = 1;

    /* Initial call to verify it works */
    NvAPI_Status status = get_thermals(handle, &thermals);
    if (status != 0) {
        fprintf(stderr, "Warning: Initial thermals query failed\n");
        return 1;
    }

    /* Probe each bit to find the maximum valid mask */
    for (int bit = 0; bit < 32; bit++) {
        thermals.mask = 1 << bit;
        status = get_thermals(handle, &thermals);
        if (status != 0) {
            return thermals.mask - 1;
        }
    }

    return 0xFFFFFFFF; /* All bits valid */
}

/*
 * Get thermals (hotspot and VRAM temperature)
 * 
 * Returns temperatures in degrees Celsius.
 * Based on LACT's implementation:
 * - Hotspot temperature is at values[9] / 256
 * - VRAM temperature is at values[15] / 256
 */
int get_thermals(NvPhysicalGpuHandle handle, int32_t mask, int32_t *hotspot, int32_t *vram) {
    NvAPI_GetThermals_t get_thermals = (NvAPI_GetThermals_t)get_nvapi_function(QUERY_NVAPI_THERMALS);
    if (!get_thermals) return -1;

    NvApiThermals thermals;
    memset(&thermals, 0, sizeof(thermals));
    
    /* Version: struct size | (version 2 << 16) */
    thermals.version = sizeof(NvApiThermals) | (2 << 16);
    thermals.mask = mask;

    NvAPI_Status status = get_thermals(handle, &thermals);
    if (status != 0) {
        fprintf(stderr, "Error: GetThermals failed with status 0x%08x\n", status);
        return -1;
    }

    /* Extract hotspot temperature from index 9 */
    int32_t hotspot_raw = thermals.values[9] / 256;
    if (hotspot_raw > 0 && hotspot_raw < 255) {
        *hotspot = hotspot_raw;
    } else {
        *hotspot = -1; /* Not available */
    }

    /* Extract VRAM temperature from index 15 */
    int32_t vram_raw = thermals.values[15] / 256;
    if (vram_raw > 0 && vram_raw < 255) {
        *vram = vram_raw;
    } else {
        *vram = -1; /* Not available */
    }

    return 0;
}

/*
 * Get voltage in microvolts
 */
int get_voltage(NvPhysicalGpuHandle handle, uint32_t *voltage_uv) {
    NvAPI_GetVoltage_t get_voltage = (NvAPI_GetVoltage_t)get_nvapi_function(QUERY_NVAPI_VOLTAGE);
    if (!get_voltage) return -1;

    NvApiVoltage voltage;
    memset(&voltage, 0, sizeof(voltage));
    
    /* Version: struct size | (version 1 << 16) */
    voltage.version = sizeof(NvApiVoltage) | (1 << 16);

    NvAPI_Status status = get_voltage(handle, &voltage);
    if (status != 0) {
        fprintf(stderr, "Error: GetVoltage failed with status 0x%08x\n", status);
        return -1;
    }

    *voltage_uv = voltage.value_uv;
    return 0;
}

/*
 * Main function - demonstrate reading NVIDIA GPU stats
 */
int main(void) {
    printf("=================================================\n");
    printf("NVIDIA GPU Stats Reader\n");
    printf("Using undocumented NVAPI calls from libnvidia-api.so.1\n");
    printf("=================================================\n\n");

    /* Load NVAPI library */
    if (load_nvapi() != 0) {
        return 1;
    }

    /* Initialize NVAPI */
    if (init_nvapi() != 0) {
        dlclose(nvapi_lib);
        return 1;
    }

    /* Enumerate GPUs */
    NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS];
    uint32_t gpu_count = 0;

    if (enum_physical_gpus(handles, &gpu_count) != 0) {
        unload_nvapi();
        return 1;
    }

    printf("Found %u NVIDIA GPU(s)\n\n", gpu_count);

    /* Get stats for each GPU */
    for (uint32_t i = 0; i < gpu_count; i++) {
        printf("-------------------------------------------------\n");
        printf("GPU %u:\n", i);
        printf("-------------------------------------------------\n");

        NvPhysicalGpuHandle handle = handles[i];

        /* Calculate thermals mask */
        int32_t mask = calculate_thermals_mask(handle);
        printf("Thermals mask: 0x%08x\n\n", mask);

        /* Get voltage */
        uint32_t voltage_uv = 0;
        if (get_voltage(handle, &voltage_uv) == 0) {
            float voltage_v = (float)voltage_uv / 1000000.0f;
            printf("Core Voltage: %.3f V (%u µV)\n", voltage_v, voltage_uv);
        } else {
            printf("Core Voltage: Not available\n");
        }

        /* Get thermals */
        int32_t hotspot = 0, vram = 0;
        if (get_thermals(handle, mask, &hotspot, &vram) == 0) {
            if (hotspot >= 0) {
                printf("Hotspot Temperature: %d °C\n", hotspot);
            } else {
                printf("Hotspot Temperature: Not available\n");
            }

            if (vram >= 0) {
                printf("Memory Temperature: %d °C\n", vram);
            } else {
                printf("Memory Temperature: Not available\n");
            }
        } else {
            printf("Hotspot Temperature: Error reading\n");
            printf("Memory Temperature: Error reading\n");
        }

        printf("\n");
    }

    /* Cleanup */
    unload_nvapi();
    printf("Done.\n");

    return 0;
}