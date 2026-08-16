#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct namfx_wasm namfx_wasm;

namfx_wasm* namfx_wasm_create(void);
void namfx_wasm_destroy(namfx_wasm* engine);

int namfx_wasm_prepare(namfx_wasm* engine, double sample_rate, int max_block_size);
int namfx_wasm_load_preset_json(namfx_wasm* engine, const char* text, unsigned int size);
int namfx_wasm_register_asset(namfx_wasm* engine, const char* name, const void* data, unsigned int size);
int namfx_wasm_set_param(namfx_wasm* engine, int slot, const char* param, float value);
int namfx_wasm_set_bypass(namfx_wasm* engine, int slot, int bypass);
int namfx_wasm_set_mix(namfx_wasm* engine, int slot, float mix);
int namfx_wasm_set_output(namfx_wasm* engine, int key, float value);

void namfx_wasm_process(const namfx_wasm* engine,
                        const float* in_l,
                        const float* in_r,
                        float* out_l,
                        float* out_r,
                        int frames);
void namfx_wasm_get_levels(const namfx_wasm* engine, float* input_level, float* output_level);

int namfx_wasm_state_json(const namfx_wasm* engine,
                          char* output,
                          unsigned int capacity,
                          unsigned int* size);
const char* namfx_wasm_last_error(const namfx_wasm* engine);

#ifdef __cplusplus
}
#endif
