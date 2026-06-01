/**
 * EDN.C - WebAssembly Bindings
 *
 * Exposes EDN.C functions to JavaScript through WebAssembly with SIMD128 support.
 * Provides automatic conversion from EDN values to JavaScript objects.
 * Supports custom JavaScript reader functions for tagged literals.
 */

#include <emscripten/emscripten.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/edn.h"

/* External Emval functions */
extern void _emval_decref(int handle);

// Forward declaration for recursive conversion
static int edn_to_js_internal(const edn_value_t* value);

/*
 * ============================================================================
 * JavaScript Reader Support
 * ============================================================================
 *
 * This section implements a bridge between C reader functions and JavaScript
 * callbacks. The challenge is that edn_reader_fn doesn't receive the tag name,
 * so we need a way to dispatch to the correct JavaScript callback.
 *
 * Solution: We create separate C trampoline functions for each reader slot.
 * Each trampoline knows its slot index and can look up the JavaScript callback.
 *
 * Usage from JavaScript:
 *   Module.registerReader('inst', (value) => new Date(value));
 *   Module.registerReader('uuid', (value) => ({ uuid: value }));
 *   const result = Module.parseEdnWithReaders('#inst "2024-01-01"');
 */

/* Type ID for JavaScript-created external values */
#define JS_VALUE_TYPE_ID 0x4A530000 /* 'JS\0\0' */

/* Maximum number of JavaScript readers (must match trampoline count) */
#define MAX_JS_READERS 32

/* Reader entry: tag name -> JS callback handle */
typedef struct {
    char* tag;       /* Tag name (owned, null-terminated) */
    int js_callback; /* Emval handle to JavaScript function */
    int slot;        /* Slot index for trampoline dispatch */
} js_reader_entry_t;

/* Global storage for JavaScript readers */
static js_reader_entry_t g_js_readers[MAX_JS_READERS];
static size_t g_js_reader_count = 0;

/* Global reader registry used during parsing */
static edn_reader_registry_t* g_reader_registry = NULL;

/**
 * Generic trampoline that dispatches based on slot index.
 */
static edn_value_t* js_reader_dispatch(int slot, edn_value_t* value, edn_arena_t* arena,
                                       const char** error_message) {
    if (slot < 0 || (size_t) slot >= g_js_reader_count) {
        *error_message = "Invalid reader slot";
        return NULL;
    }

    int js_callback = g_js_readers[slot].js_callback;
    if (js_callback == 0) {
        *error_message = "No JavaScript reader for this slot";
        return NULL;
    }

    /* Convert EDN value to JavaScript */
    int js_input = edn_to_js_internal(value);

    /* Call the JavaScript reader function */
    // clang-format off
    int js_result = EM_ASM_INT({
        try {
            const callback = Emval.toValue($0);
            const input = Emval.toValue($1);
            const result = callback(input);
            return Emval.toHandle(result);
        } catch (e) {
            console.error('EDN reader error:', e);
            return Emval.toHandle(null);
        }
    }, js_callback, js_input);
    // clang-format on

    _emval_decref(js_input);

    /* Check for error (null/undefined result) */
    // clang-format off
    int is_error = EM_ASM_INT({
        const result = Emval.toValue($0);
        return (result === null || result === undefined) ? 1 : 0;
    }, js_result);
    // clang-format on

    if (is_error) {
        _emval_decref(js_result);
        *error_message = "JavaScript reader returned null or threw an error";
        return NULL;
    }

    /* Store the JavaScript value handle in an external value */
    int* js_handle_ptr = edn_arena_alloc(arena, sizeof(int));
    if (!js_handle_ptr) {
        _emval_decref(js_result);
        *error_message = "Out of memory";
        return NULL;
    }
    *js_handle_ptr = js_result;

    return edn_external_create(arena, js_handle_ptr, JS_VALUE_TYPE_ID);
}

/*
 * Macro to generate trampoline functions for each slot.
 * Each trampoline captures its slot index at compile time.
 */
#define DEFINE_TRAMPOLINE(n)                                                             \
    static edn_value_t* js_reader_trampoline_##n(edn_value_t* value, edn_arena_t* arena, \
                                                 const char** error_message) {           \
        return js_reader_dispatch(n, value, arena, error_message);                       \
    }

/* Generate 32 trampoline functions */
DEFINE_TRAMPOLINE(0)
DEFINE_TRAMPOLINE(1)
DEFINE_TRAMPOLINE(2)
DEFINE_TRAMPOLINE(3)
DEFINE_TRAMPOLINE(4)
DEFINE_TRAMPOLINE(5)
DEFINE_TRAMPOLINE(6)
DEFINE_TRAMPOLINE(7)
DEFINE_TRAMPOLINE(8)
DEFINE_TRAMPOLINE(9)
DEFINE_TRAMPOLINE(10)
DEFINE_TRAMPOLINE(11)
DEFINE_TRAMPOLINE(12)
DEFINE_TRAMPOLINE(13)
DEFINE_TRAMPOLINE(14)
DEFINE_TRAMPOLINE(15)
DEFINE_TRAMPOLINE(16)
DEFINE_TRAMPOLINE(17)
DEFINE_TRAMPOLINE(18)
DEFINE_TRAMPOLINE(19)
DEFINE_TRAMPOLINE(20)
DEFINE_TRAMPOLINE(21)
DEFINE_TRAMPOLINE(22)
DEFINE_TRAMPOLINE(23)
DEFINE_TRAMPOLINE(24)
DEFINE_TRAMPOLINE(25)
DEFINE_TRAMPOLINE(26)
DEFINE_TRAMPOLINE(27)
DEFINE_TRAMPOLINE(28)
DEFINE_TRAMPOLINE(29)
DEFINE_TRAMPOLINE(30)
DEFINE_TRAMPOLINE(31)

/* Array of trampoline function pointers */
static edn_reader_fn g_trampolines[MAX_JS_READERS] = {
    js_reader_trampoline_0,  js_reader_trampoline_1,  js_reader_trampoline_2,
    js_reader_trampoline_3,  js_reader_trampoline_4,  js_reader_trampoline_5,
    js_reader_trampoline_6,  js_reader_trampoline_7,  js_reader_trampoline_8,
    js_reader_trampoline_9,  js_reader_trampoline_10, js_reader_trampoline_11,
    js_reader_trampoline_12, js_reader_trampoline_13, js_reader_trampoline_14,
    js_reader_trampoline_15, js_reader_trampoline_16, js_reader_trampoline_17,
    js_reader_trampoline_18, js_reader_trampoline_19, js_reader_trampoline_20,
    js_reader_trampoline_21, js_reader_trampoline_22, js_reader_trampoline_23,
    js_reader_trampoline_24, js_reader_trampoline_25, js_reader_trampoline_26,
    js_reader_trampoline_27, js_reader_trampoline_28, js_reader_trampoline_29,
    js_reader_trampoline_30, js_reader_trampoline_31,
};

/**
 * Initialize the reader registry.
 */
static void ensure_registry(void) {
    if (g_reader_registry == NULL) {
        g_reader_registry = edn_reader_registry_create();
    }
}

/**
 * Find a reader entry by tag name.
 * Returns the entry index or -1 if not found.
 */
static int find_reader_by_tag(const char* tag) {
    for (size_t i = 0; i < g_js_reader_count; i++) {
        if (strcmp(g_js_readers[i].tag, tag) == 0) {
            return (int) i;
        }
    }
    return -1;
}

/**
 * Register a JavaScript reader function for a tag.
 *
 * @param tag Tag name (e.g., "inst", "uuid", "myapp/custom")
 * @param js_callback Emval handle to JavaScript function
 * @return 1 on success, 0 on failure
 */
EMSCRIPTEN_KEEPALIVE
int wasm_edn_register_reader(const char* tag, int js_callback) {
    if (!tag || js_callback == 0) {
        return 0;
    }

    ensure_registry();

    /* Check if already registered (update callback) */
    int existing = find_reader_by_tag(tag);
    if (existing >= 0) {
        _emval_decref(g_js_readers[existing].js_callback);
        g_js_readers[existing].js_callback = js_callback;
        return 1;
    }

    /* Check capacity */
    if (g_js_reader_count >= MAX_JS_READERS) {
        return 0;
    }

    /* Add new entry */
    size_t tag_len = strlen(tag);
    char* tag_copy = malloc(tag_len + 1);
    if (!tag_copy) {
        return 0;
    }
    memcpy(tag_copy, tag, tag_len + 1);

    int slot = (int) g_js_reader_count;
    g_js_readers[slot].tag = tag_copy;
    g_js_readers[slot].js_callback = js_callback;
    g_js_readers[slot].slot = slot;
    g_js_reader_count++;

    /* Register the slot-specific trampoline with the C reader registry */
    edn_reader_register(g_reader_registry, tag, g_trampolines[slot]);

    return 1;
}

/**
 * Unregister a JavaScript reader function.
 *
 * @param tag Tag name
 */
EMSCRIPTEN_KEEPALIVE
void wasm_edn_unregister_reader(const char* tag) {
    if (!tag) {
        return;
    }

    int idx = find_reader_by_tag(tag);
    if (idx < 0) {
        return;
    }

    /* Free resources */
    _emval_decref(g_js_readers[idx].js_callback);
    free(g_js_readers[idx].tag);

    /* Remove from C registry */
    if (g_reader_registry) {
        edn_reader_unregister(g_reader_registry, tag);
    }

    /* Shift remaining entries and update their slots */
    for (size_t j = (size_t) idx; j < g_js_reader_count - 1; j++) {
        g_js_readers[j] = g_js_readers[j + 1];
        g_js_readers[j].slot = (int) j;
        /* Re-register with new slot trampoline */
        if (g_reader_registry) {
            edn_reader_register(g_reader_registry, g_js_readers[j].tag, g_trampolines[j]);
        }
    }
    g_js_reader_count--;
}

/**
 * Clear all registered JavaScript readers.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_edn_clear_readers(void) {
    for (size_t i = 0; i < g_js_reader_count; i++) {
        _emval_decref(g_js_readers[i].js_callback);
        free(g_js_readers[i].tag);
        if (g_reader_registry) {
            edn_reader_unregister(g_reader_registry, g_js_readers[i].tag);
        }
    }
    g_js_reader_count = 0;
}

/**
 * Get the number of registered readers.
 */
EMSCRIPTEN_KEEPALIVE
size_t wasm_edn_reader_count(void) {
    return g_js_reader_count;
}

/*
 * ============================================================================
 * Core Parsing Functions
 * ============================================================================
 */

/**
 * Parse EDN string and return result pointer.
 *
 * @param input EDN string (null-terminated)
 * @return Pointer to edn_value_t or NULL on error
 */
EMSCRIPTEN_KEEPALIVE
edn_value_t* wasm_edn_parse(const char* input) {
    if (!input) {
        return NULL;
    }

    edn_result_t result = edn_read(input, 0);

    if (result.error != EDN_OK) {
        return NULL;
    }

    return result.value;
}

/**
 * Parse EDN string with registered JavaScript readers.
 *
 * @param input EDN string (null-terminated)
 * @param default_mode Default reader mode (0=PASSTHROUGH, 1=UNWRAP, 2=ERROR)
 * @return Pointer to edn_value_t or NULL on error
 */
EMSCRIPTEN_KEEPALIVE
edn_value_t* wasm_edn_parse_with_readers(const char* input, int default_mode) {
    if (!input) {
        return NULL;
    }

    if (g_reader_registry == NULL || g_js_reader_count == 0) {
        return wasm_edn_parse(input);
    }

    edn_parse_options_t options = {.reader_registry = g_reader_registry,
                                   .eof_value = NULL,
                                   .default_reader_mode = (edn_default_reader_mode_t) default_mode};

    edn_result_t result = edn_read_with_options(input, 0, &options);

    if (result.error != EDN_OK) {
        return NULL;
    }

    return result.value;
}

EMSCRIPTEN_KEEPALIVE
void wasm_edn_free(edn_value_t* value) {
    if (value) {
        edn_free(value);
    }
}

EMSCRIPTEN_KEEPALIVE
int wasm_edn_type(edn_value_t* value) {
    if (!value) {
        return -1;
    }
    return (int) edn_type(value);
}

/*
 * ============================================================================
 * Value Accessors
 * ============================================================================
 */

EMSCRIPTEN_KEEPALIVE
int64_t wasm_edn_get_int(edn_value_t* value) {
    if (!value || edn_type(value) != EDN_TYPE_INT) {
        return 0;
    }

    int64_t result;
    edn_int64_get(value, &result);
    return result;
}

EMSCRIPTEN_KEEPALIVE
double wasm_edn_get_float(edn_value_t* value) {
    if (!value || edn_type(value) != EDN_TYPE_FLOAT) {
        return 0.0;
    }

    double result;
    edn_double_get(value, &result);
    return result;
}

EMSCRIPTEN_KEEPALIVE
int wasm_edn_get_bool(edn_value_t* value) {
    if (!value || edn_type(value) != EDN_TYPE_BOOL) {
        return 0;
    }

    bool result;
    edn_bool_get(value, &result);
    return result ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
const char* wasm_edn_get_string(edn_value_t* value) {
    if (!value || edn_type(value) != EDN_TYPE_STRING) {
        return NULL;
    }

    size_t len;
    return edn_string_get(value, &len);
}

EMSCRIPTEN_KEEPALIVE
size_t wasm_edn_get_string_length(edn_value_t* value) {
    if (!value || edn_type(value) != EDN_TYPE_STRING) {
        return 0;
    }

    size_t len;
    edn_string_get(value, &len);
    return len;
}

EMSCRIPTEN_KEEPALIVE
size_t wasm_edn_count(edn_value_t* value) {
    if (!value) {
        return 0;
    }

    edn_type_t type = edn_type(value);

    switch (type) {
        case EDN_TYPE_VECTOR:
            return edn_vector_count(value);
        case EDN_TYPE_LIST:
            return edn_list_count(value);
        case EDN_TYPE_MAP:
            return edn_map_count(value);
        case EDN_TYPE_SET:
            return edn_set_count(value);
        default:
            return 0;
    }
}

EMSCRIPTEN_KEEPALIVE
edn_value_t* wasm_edn_vector_get(edn_value_t* value, size_t index) {
    if (!value || edn_type(value) != EDN_TYPE_VECTOR) {
        return NULL;
    }

    if (index >= edn_vector_count(value)) {
        return NULL;
    }

    return edn_vector_get(value, index);
}

EMSCRIPTEN_KEEPALIVE
edn_value_t* wasm_edn_list_get(edn_value_t* value, size_t index) {
    if (!value || edn_type(value) != EDN_TYPE_LIST) {
        return NULL;
    }

    if (index >= edn_list_count(value)) {
        return NULL;
    }

    return edn_list_get(value, index);
}

EMSCRIPTEN_KEEPALIVE
edn_value_t* wasm_edn_set_get(edn_value_t* value, size_t index) {
    if (!value || edn_type(value) != EDN_TYPE_SET) {
        return NULL;
    }

    if (index >= edn_set_count(value)) {
        return NULL;
    }

    return edn_set_get(value, index);
}

EMSCRIPTEN_KEEPALIVE
edn_value_t* wasm_edn_map_get_key(edn_value_t* value, size_t index) {
    if (!value || edn_type(value) != EDN_TYPE_MAP) {
        return NULL;
    }

    if (index >= edn_map_count(value)) {
        return NULL;
    }

    return edn_map_get_key(value, index);
}

EMSCRIPTEN_KEEPALIVE
edn_value_t* wasm_edn_map_get_value(edn_value_t* value, size_t index) {
    if (!value || edn_type(value) != EDN_TYPE_MAP) {
        return NULL;
    }

    if (index >= edn_map_count(value)) {
        return NULL;
    }

    return edn_map_get_value(value, index);
}

EMSCRIPTEN_KEEPALIVE
int wasm_edn_is_external(edn_value_t* value) {
    if (!value) {
        return 0;
    }
    return edn_type(value) == EDN_TYPE_EXTERNAL ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
uint32_t wasm_edn_external_type_id(edn_value_t* value) {
    if (!value || edn_type(value) != EDN_TYPE_EXTERNAL) {
        return 0;
    }

    uint32_t type_id = 0;
    edn_external_get(value, NULL, &type_id);
    return type_id;
}

/*
 * ============================================================================
 * Validation and Error Handling
 * ============================================================================
 */

EMSCRIPTEN_KEEPALIVE
int wasm_edn_validate(const char* input) {
    if (!input) {
        return 0;
    }

    edn_result_t result = edn_read(input, 0);

    if (result.error == EDN_OK) {
        edn_free(result.value);
        return 1;
    }

    return 0;
}

EMSCRIPTEN_KEEPALIVE
const char* wasm_edn_get_error(const char* input) {
    if (!input) {
        return "Input is NULL";
    }

    edn_result_t result = edn_read(input, 0);

    if (result.error != EDN_OK) {
        // Note: This string lifetime is static, safe to return
        return result.error_message;
    }

    edn_free(result.value);
    return NULL;
}

/*
 * ============================================================================
 * JavaScript Conversion
 * ============================================================================
 *
 * Type mappings:
 * - nil -> null
 * - boolean -> boolean
 * - integer -> number (or BigInt if out of safe integer range)
 * - bigint -> BigInt
 * - float -> number
 * - bigdec -> string (preserves precision)
 * - ratio -> number (computed as numerator/denominator)
 * - bigratio -> string "numerator/denominator"
 * - character -> string (single character)
 * - string -> string
 * - symbol -> Symbol.for(name) or Symbol.for(ns/name)
 * - keyword -> Symbol.for(:name) or Symbol.for(:ns/name)
 * - list -> Array
 * - vector -> Array
 * - map -> Map
 * - set -> Set
 * - tagged -> {tag: string, value: any}
 * - external(JS_VALUE_TYPE_ID) -> stored JavaScript value
 * - external(other) -> {_external: true, typeId: number, pointer: number}
 */

// clang-format off
static int edn_to_js_internal(const edn_value_t* value) {
    if (!value) {
        return EM_ASM_INT({ return Emval.toHandle(null); });
    }

    edn_type_t type = edn_type(value);

    switch (type) {
        case EDN_TYPE_NIL: {
            return EM_ASM_INT({ return Emval.toHandle(null); });
        }

        case EDN_TYPE_BOOL: {
            bool b;
            edn_bool_get(value, &b);
            return EM_ASM_INT({
                return Emval.toHandle($0 !== 0);
            }, b);
        }

        case EDN_TYPE_INT: {
            int64_t num;
            edn_int64_get(value, &num);

            if (num >= -9007199254740991LL && num <= 9007199254740991LL) {
                return EM_ASM_INT({ return Emval.toHandle(Number($0)); }, (double)num);
            } else {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)num);
                return EM_ASM_INT({ return Emval.toHandle(BigInt(UTF8ToString($0))); }, buf);
            }
        }

        case EDN_TYPE_BIGINT: {
            size_t len;
            bool negative;
            uint8_t radix;
            const char* digits = edn_bigint_get(value, &len, &negative, &radix);

            return EM_ASM_INT({
                const digits = UTF8ToString($0, $1);
                const negative = $2 !== 0;
                const radix = $3;

                let bigint;
                if (radix === 10) {
                    bigint = BigInt(digits);
                } else if (radix === 16) {
                    bigint = BigInt('0x' + digits);
                } else if (radix === 8) {
                    bigint = BigInt('0o' + digits);
                } else if (radix === 2) {
                    bigint = BigInt('0b' + digits);
                } else {
                    bigint = BigInt(parseInt(digits, radix));
                }

                return Emval.toHandle(negative ? -bigint : bigint);
            }, digits, len, negative, radix);
        }

        case EDN_TYPE_FLOAT: {
            double d;
            edn_double_get(value, &d);
            return EM_ASM_INT({ return Emval.toHandle($0); }, d);
        }

        case EDN_TYPE_BIGDEC: {
            size_t len;
            bool negative;
            const char* dec_str = edn_bigdec_get(value, &len, &negative);

            return EM_ASM_INT({
                const str = UTF8ToString($0);
                const negative = $1 !== 0;
                return Emval.toHandle(negative ? '-' + str : str);
            }, dec_str, negative);
        }

#ifdef EDN_ENABLE_CLOJURE_EXTENSION
        case EDN_TYPE_RATIO: {
            int64_t numerator, denominator;
            edn_ratio_get(value, &numerator, &denominator);

            return EM_ASM_INT(
                { return Emval.toHandle($0 / $1); }, (double)numerator, (double)denominator);
        }

        case EDN_TYPE_BIGRATIO: {
            const char* numerator_digits;
            const char* denominator_digits;
            size_t numer_length;
            size_t denom_length;
            bool negative;
            edn_bigratio_get(value, &numerator_digits, &numer_length, &negative,
                             &denominator_digits, &denom_length);

            return EM_ASM_INT({
                const numerator_str = UTF8ToString($0, $1);
                const denominator_str = UTF8ToString($2, $3);
                const negative = $4 !== 0;

                return Emval.toHandle(
                    (negative ? '-' + numerator_str : numerator_str) + '/' + denominator_str);
            }, numerator_digits, numer_length, denominator_digits, denom_length, negative);
        }
#endif

        case EDN_TYPE_CHARACTER: {
            uint32_t codepoint;
            edn_character_get(value, &codepoint);

            return EM_ASM_INT({ return Emval.toHandle(String.fromCodePoint($0)); }, codepoint);
        }

        case EDN_TYPE_STRING: {
            size_t len;
            const char* str = edn_string_get(value, &len);

            return EM_ASM_INT({ return Emval.toHandle(UTF8ToString($0, $1)); }, str, len);
        }

        case EDN_TYPE_SYMBOL: {
            const char* ns = NULL;
            const char* name = NULL;
            size_t ns_len = 0;
            size_t name_len = 0;

            edn_symbol_get(value, &ns, &ns_len, &name, &name_len);

            if (ns && ns_len > 0) {
                return EM_ASM_INT({
                    const ns = UTF8ToString($0, $1);
                    const name = UTF8ToString($2, $3);
                    return Emval.toHandle(Symbol.for(ns + '/' + name));
                }, ns, ns_len, name, name_len);
            } else {
                return EM_ASM_INT({
                    const name = UTF8ToString($0, $1);
                    return Emval.toHandle(Symbol.for(name));
                }, name, name_len);
            }
        }

        case EDN_TYPE_KEYWORD: {
            const char* ns = NULL;
            const char* name = NULL;
            size_t ns_len = 0;
            size_t name_len = 0;

            edn_keyword_get(value, &ns, &ns_len, &name, &name_len);

            if (ns && ns_len > 0) {
                return EM_ASM_INT({
                    const ns = UTF8ToString($0, $1);
                    const name = UTF8ToString($2, $3);
                    return Emval.toHandle(Symbol.for(':' + ns + '/' + name));
                }, ns, ns_len, name, name_len);
            } else {
                return EM_ASM_INT({
                    const name = UTF8ToString($0, $1);
                    return Emval.toHandle(Symbol.for(':' + name));
                }, name, name_len);
            }
        }

        case EDN_TYPE_LIST:
        case EDN_TYPE_VECTOR: {
            size_t count =
                (type == EDN_TYPE_LIST) ? edn_list_count(value) : edn_vector_count(value);

            int arr = EM_ASM_INT({ return Emval.toHandle([]); });

            for (size_t i = 0; i < count; i++) {
                edn_value_t* elem =
                    (type == EDN_TYPE_LIST) ? edn_list_get(value, i) : edn_vector_get(value, i);

                int js_elem = edn_to_js_internal(elem);

                EM_ASM({
                    const arr = Emval.toValue($0);
                    const elem = Emval.toValue($1);
                    arr.push(elem);
                }, arr, js_elem);

                _emval_decref(js_elem);
            }

            return arr;
        }

        case EDN_TYPE_SET: {
            size_t count = edn_set_count(value);

            int set = EM_ASM_INT({ return Emval.toHandle(new Set()); });

            for (size_t i = 0; i < count; i++) {
                edn_value_t* elem = edn_set_get(value, i);
                int js_elem = edn_to_js_internal(elem);

                EM_ASM({
                    const set = Emval.toValue($0);
                    const elem = Emval.toValue($1);
                    set.add(elem);
                }, set, js_elem);

                _emval_decref(js_elem);
            }

            return set;
        }

        case EDN_TYPE_MAP: {
            size_t count = edn_map_count(value);

            int map = EM_ASM_INT({ return Emval.toHandle(new Map()); });

            for (size_t i = 0; i < count; i++) {
                edn_value_t* key = edn_map_get_key(value, i);
                edn_value_t* val = edn_map_get_value(value, i);

                int js_key = edn_to_js_internal(key);
                int js_val = edn_to_js_internal(val);

                EM_ASM({
                    const map = Emval.toValue($0);
                    const key = Emval.toValue($1);
                    const val = Emval.toValue($2);
                    map.set(key, val);
                }, map, js_key, js_val);

                _emval_decref(js_key);
                _emval_decref(js_val);
            }

            return map;
        }

        case EDN_TYPE_TAGGED: {
            const char* tag = NULL;
            size_t tag_len = 0;
            edn_value_t* tagged_value = NULL;

            edn_tagged_get(value, &tag, &tag_len, &tagged_value);

            int js_value = edn_to_js_internal(tagged_value);

            int obj = EM_ASM_INT({
                const tag = UTF8ToString($0, $1);
                const value = Emval.toValue($2);
                return Emval.toHandle({ tag: tag, value: value });
            }, tag, tag_len, js_value);

            _emval_decref(js_value);

            return obj;
        }

        case EDN_TYPE_EXTERNAL: {
            void* data = NULL;
            uint32_t type_id = 0;
            edn_external_get(value, &data, &type_id);

            /* Check if this is a JavaScript value from a JS reader */
            if (type_id == JS_VALUE_TYPE_ID && data != NULL) {
                int* js_handle_ptr = (int*)data;
                /* Return the stored JavaScript value directly */
                return EM_ASM_INT({
                    const value = Emval.toValue($0);
                    return Emval.toHandle(value);
                }, *js_handle_ptr);
            }

            /* Generic external value */
            return EM_ASM_INT({
                return Emval.toHandle({
                    _external: true,
                    typeId: $0,
                    pointer: $1
                });
            }, type_id, (intptr_t)data);
        }

        default:
            return EM_ASM_INT({ return Emval.toHandle(null); });
    }
}
// clang-format on

/**
 * Convert EDN value to JavaScript object.
 *
 * @param value EDN value pointer
 * @return JavaScript value handle
 */
EMSCRIPTEN_KEEPALIVE
int wasm_edn_to_js(edn_value_t* value) {
    return edn_to_js_internal(value);
}

/**
 * Parse EDN string and return as JavaScript object.
 *
 * @param input EDN string (null-terminated)
 * @return JavaScript object handle or null on error
 */
EMSCRIPTEN_KEEPALIVE
int wasm_edn_parse_to_js(const char* input) {
    if (!input) {
        return EM_ASM_INT({ return Emval.toHandle(null); });
    }

    edn_result_t result = edn_read(input, 0);

    if (result.error != EDN_OK) {
        return EM_ASM_INT({ return Emval.toHandle(null); });
    }

    int js_value = edn_to_js_internal(result.value);
    edn_free(result.value);

    return js_value;
}

/**
 * Parse EDN string with registered readers and return as JavaScript object.
 *
 * @param input EDN string (null-terminated)
 * @param default_mode Default reader mode (0=PASSTHROUGH, 1=UNWRAP, 2=ERROR)
 * @return JavaScript object handle or null on error
 */
EMSCRIPTEN_KEEPALIVE
int wasm_edn_parse_to_js_with_readers(const char* input, int default_mode) {
    if (!input) {
        return EM_ASM_INT({ return Emval.toHandle(null); });
    }

    edn_value_t* value = wasm_edn_parse_with_readers(input, default_mode);
    if (!value) {
        return EM_ASM_INT({ return Emval.toHandle(null); });
    }

    int js_value = edn_to_js_internal(value);
    edn_free(value);

    return js_value;
}

/*
 * ============================================================================
 * EDN Serialization (Writing)
 * ============================================================================
 *
 * This section provides the inverse of the JavaScript conversion above:
 *   - wasm_edn_write       : serialize a value-tree pointer to an EDN string
 *   - wasm_edn_free_string : free a string returned by wasm_edn_write
 *   - wasm_edn_write_js    : serialize a JavaScript value directly to EDN using
 *                            the streaming emitter (no intermediate tree)
 *
 * JS -> EDN type mapping (inverse of edn_to_js_internal):
 *   null / undefined          -> nil
 *   boolean                   -> boolean
 *   number (safe integer)     -> integer
 *   number (other)            -> float
 *   bigint                    -> bigint (or integer when no Clojure extension)
 *   string                    -> string
 *   symbol (":..." desc)      -> keyword (with optional ns/name split)
 *   symbol (other desc)       -> symbol  (with optional ns/name split)
 *   Array                     -> vector
 *   Map                       -> map
 *   Set                       -> set
 *   {tag: string, value: any} -> tagged literal
 *   plain object              -> map (keys become keywords when valid, else strings)
 */

/* Growable heap buffer feeding the streaming emitter callback. */
typedef struct {
    char* data;
    size_t len;
    size_t cap;
    int failed;
} wasm_write_buffer_t;

static int wasm_write_buffer_cb(const char* buf, size_t len, void* ctx) {
    wasm_write_buffer_t* b = (wasm_write_buffer_t*) ctx;
    if (b->failed) {
        return -1;
    }
    if (b->len + len + 1 > b->cap) {
        size_t new_cap = b->cap ? b->cap : 256;
        while (new_cap < b->len + len + 1) {
            new_cap *= 2;
        }
        char* new_data = realloc(b->data, new_cap);
        if (!new_data) {
            b->failed = 1;
            return -1;
        }
        b->data = new_data;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, buf, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 0;
}

/* Copy a JS string handle into a malloc'd UTF-8 buffer; caller frees with free(). */
static char* js_string_to_utf8(int handle, size_t* out_len) {
    size_t len = 0;
    // clang-format off
    char* ptr = (char*) EM_ASM_PTR({
        const s = Emval.toValue($0);
        const n = lengthBytesUTF8(s);
        const p = _malloc(n + 1);
        stringToUTF8(s, p, n + 1);
        setValue($1, n, 'i32');
        return p;
    }, handle, &len);
    // clang-format on
    if (out_len) {
        *out_len = len;
    }
    return ptr;
}

static int js_array_length(int arr_handle) {
    return EM_ASM_INT({ return Emval.toValue($0).length; }, arr_handle);
}

/* Returns a fresh Emval handle for arr[i]; caller decrefs. */
static int js_array_get(int arr_handle, int i) {
    return EM_ASM_INT({ return Emval.toHandle(Emval.toValue($0)[$1]); }, arr_handle, i);
}

/* Emit a (possibly namespaced) keyword or symbol from a single name string. */
static int emit_qualified(edn_emitter_t* em, const char* name, bool keyword) {
    const char* slash = strchr(name, '/');
    bool single = slash && strchr(slash + 1, '/') == NULL && slash != name && slash[1] != '\0';
    if (single) {
        size_t ns_len = (size_t) (slash - name);
        char* ns = malloc(ns_len + 1);
        if (!ns) {
            return -EDN_ERROR_OUT_OF_MEMORY;
        }
        memcpy(ns, name, ns_len);
        ns[ns_len] = '\0';
        int rc = keyword ? edn_emit_keyword_ns(em, ns, slash + 1)
                         : edn_emit_symbol_ns(em, ns, slash + 1);
        free(ns);
        return rc;
    }
    return keyword ? edn_emit_keyword(em, name) : edn_emit_symbol(em, name);
}

/* Recursively emit a JS value through the streaming emitter. Returns 0 / -EDN_ERROR_*. */
static int js_to_edn_emit(int handle, edn_emitter_t* em) {
    // clang-format off
    int type_code = EM_ASM_INT({
        const v = Emval.toValue($0);
        if (v === null || v === undefined) return 0;
        const t = typeof v;
        if (t === 'boolean') return 1;
        if (t === 'number') return 2;
        if (t === 'bigint') return 3;
        if (t === 'string') return 4;
        if (t === 'symbol') return 5;
        if (Array.isArray(v)) return 6;
        if (v instanceof Map) return 7;
        if (v instanceof Set) return 8;
        if (t === 'object') {
            if (typeof v.tag === 'string' && ('value' in v) && Object.keys(v).length === 2) {
                return 9;
            }
            return 10;
        }
        return -1;
    }, handle);

    switch (type_code) {
        case 0:
            return edn_emit_nil(em);

        case 1: {
            int b = EM_ASM_INT({ return !!Emval.toValue($0); }, handle);
            return edn_emit_bool(em, b != 0);
        }

        case 2: {
            double d = EM_ASM_DOUBLE({ return Emval.toValue($0); }, handle);
            int is_int = EM_ASM_INT(
                {
                    const v = Emval.toValue($0);
                    return (Number.isInteger(v) && Math.abs(v) <= 9007199254740991) ? 1 : 0;
                },
                handle);
            return is_int ? edn_emit_int(em, (int64_t) d) : edn_emit_double(em, d);
        }

        case 3: {
            int str_handle =
                EM_ASM_INT({ return Emval.toHandle(Emval.toValue($0).toString()); }, handle);
            size_t dlen = 0;
            char* digits = js_string_to_utf8(str_handle, &dlen);
            _emval_decref(str_handle);
            if (!digits) {
                return -EDN_ERROR_OUT_OF_MEMORY;
            }
            int rc;
#ifdef EDN_ENABLE_CLOJURE_EXTENSION
            rc = edn_emit_bigint(em, digits, 10);
#else
            errno = 0;
            char* endp = NULL;
            long long v = strtoll(digits, &endp, 10);
            if (errno == 0 && endp && *endp == '\0' && endp != digits) {
                rc = edn_emit_int(em, (int64_t) v);
            } else {
                rc = -EDN_ERROR_UNSUPPORTED_TYPE;
            }
#endif
            free(digits);
            return rc;
        }

        case 4: {
            size_t slen = 0;
            char* s = js_string_to_utf8(handle, &slen);
            if (!s) {
                return -EDN_ERROR_OUT_OF_MEMORY;
            }
            int rc = edn_emit_string(em, s, slen);
            free(s);
            return rc;
        }

        case 5: {
            int desc_handle = EM_ASM_INT(
                {
                    const d = Emval.toValue($0).description;
                    return Emval.toHandle(typeof d === 'string' ? d : '');
                },
                handle);
            size_t dlen = 0;
            char* desc = js_string_to_utf8(desc_handle, &dlen);
            _emval_decref(desc_handle);
            if (!desc) {
                return -EDN_ERROR_OUT_OF_MEMORY;
            }
            bool keyword = desc[0] == ':';
            int rc = emit_qualified(em, keyword ? desc + 1 : desc, keyword);
            free(desc);
            return rc;
        }

        case 6: {
            int n = js_array_length(handle);
            int rc = edn_emit_begin_vector(em);
            for (int i = 0; rc == 0 && i < n; i++) {
                int elem = js_array_get(handle, i);
                rc = js_to_edn_emit(elem, em);
                _emval_decref(elem);
            }
            return rc == 0 ? edn_emit_end_vector(em) : rc;
        }

        case 7: {
            int entries = EM_ASM_INT(
                { return Emval.toHandle(Array.from(Emval.toValue($0).entries())); }, handle);
            int n = js_array_length(entries);
            int rc = edn_emit_begin_map(em);
            for (int i = 0; rc == 0 && i < n; i++) {
                int pair = js_array_get(entries, i);
                int key = js_array_get(pair, 0);
                int val = js_array_get(pair, 1);
                rc = js_to_edn_emit(key, em);
                if (rc == 0) {
                    rc = js_to_edn_emit(val, em);
                }
                _emval_decref(key);
                _emval_decref(val);
                _emval_decref(pair);
            }
            _emval_decref(entries);
            return rc == 0 ? edn_emit_end_map(em) : rc;
        }

        case 8: {
            int arr = EM_ASM_INT({ return Emval.toHandle(Array.from(Emval.toValue($0))); }, handle);
            int n = js_array_length(arr);
            int rc = edn_emit_begin_set(em);
            for (int i = 0; rc == 0 && i < n; i++) {
                int elem = js_array_get(arr, i);
                rc = js_to_edn_emit(elem, em);
                _emval_decref(elem);
            }
            _emval_decref(arr);
            return rc == 0 ? edn_emit_end_set(em) : rc;
        }

        case 9: {
            int tag_handle = EM_ASM_INT({ return Emval.toHandle(Emval.toValue($0).tag); }, handle);
            size_t tlen = 0;
            char* tag = js_string_to_utf8(tag_handle, &tlen);
            _emval_decref(tag_handle);
            if (!tag) {
                return -EDN_ERROR_OUT_OF_MEMORY;
            }
            int rc = edn_emit_tag(em, tag);
            free(tag);
            if (rc != 0) {
                return rc;
            }
            int val = EM_ASM_INT({ return Emval.toHandle(Emval.toValue($0).value); }, handle);
            rc = js_to_edn_emit(val, em);
            _emval_decref(val);
            return rc;
        }

        case 10: {
            int keys =
                EM_ASM_INT({ return Emval.toHandle(Object.keys(Emval.toValue($0))); }, handle);
            int n = js_array_length(keys);
            int rc = edn_emit_begin_map(em);
            for (int i = 0; rc == 0 && i < n; i++) {
                int key_handle = js_array_get(keys, i);
                size_t klen = 0;
                char* key = js_string_to_utf8(key_handle, &klen);
                if (!key) {
                    _emval_decref(key_handle);
                    rc = -EDN_ERROR_OUT_OF_MEMORY;
                    break;
                }
                /* Conservatively decide if the key is a valid EDN keyword name.
                 * NOTE: avoid backslash escapes in this regex literal; EM_ASM
                 * stringization strips backslashes, so use explicit char ranges
                 * (\w -> A-Za-z0-9_) and place '-' at the end of each class. */
                int is_kw = EM_ASM_INT(
                    {
                        const k = UTF8ToString($0);
                        const re = /^[A-Za-z*!_?$%&=<>+.-][A-Za-z0-9_*!?$%&=<>+.:#'-]*$/;
                        const parts = k.split('/');
                        if (parts.length === 1) return re.test(parts[0]) ? 1 : 0;
                        if (parts.length === 2) {
                            return (re.test(parts[0]) && re.test(parts[1])) ? 1 : 0;
                        }
                        return 0;
                    },
                    key);
                if (is_kw) {
                    rc = emit_qualified(em, key, true);
                } else {
                    rc = edn_emit_string(em, key, klen);
                }
                free(key);
                if (rc == 0) {
                    int val_handle = EM_ASM_INT(
                        {
                            const obj = Emval.toValue($0);
                            const k = Emval.toValue($1);
                            return Emval.toHandle(obj[k]);
                        },
                        handle, key_handle);
                    rc = js_to_edn_emit(val_handle, em);
                    _emval_decref(val_handle);
                }
                _emval_decref(key_handle);
            }
            _emval_decref(keys);
            return rc == 0 ? edn_emit_end_map(em) : rc;
        }

        default:
            return -EDN_ERROR_UNSUPPORTED_TYPE;
    }
    // clang-format on
}

/**
 * Serialize an EDN value-tree to a malloc'd EDN string.
 *
 * @param value         EDN value pointer
 * @param indent        0 = compact, non-zero = pretty-print
 * @param sort_unordered non-zero to sort map/set entries deterministically
 * @param escape_unicode non-zero to escape non-ASCII as \uXXXX
 * @param newline_at_end non-zero to append a trailing newline
 * @return malloc'd null-terminated string (free via wasm_edn_free_string), NULL on error
 */
EMSCRIPTEN_KEEPALIVE
char* wasm_edn_write(edn_value_t* value, int indent, int sort_unordered, int escape_unicode,
                     int newline_at_end) {
    if (!value) {
        return NULL;
    }

    edn_write_options_t opts = {0};
    opts.struct_size = sizeof(edn_write_options_t);
    opts.indent = (size_t) indent;
    opts.sort_unordered = sort_unordered != 0;
    opts.escape_unicode = escape_unicode != 0;
    opts.newline_at_end = newline_at_end != 0;

    return edn_write_string(value, &opts, NULL);
}

/**
 * Free a string previously returned by wasm_edn_write. NULL-safe.
 */
EMSCRIPTEN_KEEPALIVE
void wasm_edn_free_string(char* s) {
    free(s);
}

/**
 * Serialize a JavaScript value directly to an EDN string using the streaming
 * emitter (no intermediate value-tree is built).
 *
 * @param js_handle      Emval handle to the JavaScript value
 * @param indent         0 = compact, non-zero = pretty-print
 * @param escape_unicode non-zero to escape non-ASCII as \uXXXX
 * @param newline_at_end non-zero to append a trailing newline
 * @return Emval handle to a JS string with the EDN serialization, or to null on error
 */
EMSCRIPTEN_KEEPALIVE
int wasm_edn_write_js(int js_handle, int indent, int escape_unicode, int newline_at_end) {
    int null_handle = EM_ASM_INT({ return Emval.toHandle(null); });

    edn_write_options_t opts = {0};
    opts.struct_size = sizeof(edn_write_options_t);
    opts.indent = (size_t) indent;
    opts.escape_unicode = escape_unicode != 0;
    opts.newline_at_end = newline_at_end != 0;

    wasm_write_buffer_t buf = {0};
    edn_emitter_t* em = edn_emitter_create(wasm_write_buffer_cb, &buf, &opts);
    if (!em) {
        free(buf.data);
        return null_handle;
    }

    int rc = js_to_edn_emit(js_handle, em);
    if (rc == 0) {
        rc = edn_emitter_finish(em);
    }
    edn_emitter_destroy(em);

    int result;
    if (rc == 0 && !buf.failed && buf.data) {
        result =
            EM_ASM_INT({ return Emval.toHandle(UTF8ToString($0, $1)); }, buf.data, (int) buf.len);
        _emval_decref(null_handle);
    } else {
        result = null_handle;
    }

    free(buf.data);
    return result;
}
