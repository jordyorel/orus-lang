//  WebAssembly bridge exposing Orus runtime helpers to the generated
//  Emscripten module. These functions are auto-exported and consumed by the
//  playground loader at runtime.


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "public/version.h"
#include "config/config.h"
#include "debug/debug_config.h"
#include "errors/error_interface.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "internal/logging.h"
#include "vm/vm.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_profiling.h"
#include "vm/vm_string_ops.h"

// -----------------------------------------------------------------------------
// Static state for the WebAssembly facade
// -----------------------------------------------------------------------------

static bool g_vm_ready = false;
static OrusConfig* g_web_config = NULL;
static char g_last_error[1024] = "";

static void set_last_error(const char* message) {
    if (!message || message[0] == '\0') {
        g_last_error[0] = '\0';
        return;
    }

    size_t len = strlen(message);
    if (len >= sizeof(g_last_error)) {
        len = sizeof(g_last_error) - 1;
    }
    memcpy(g_last_error, message, len);
    g_last_error[len] = '\0';
}

static void clear_last_error(void) {
    g_last_error[0] = '\0';
}

static void populate_error_from_vm(void) {
    if (!IS_ERROR(vm.lastError)) {
        return;
    }

    ObjError* err = AS_ERROR(vm.lastError);
    if (!err) {
        return;
    }

    const char* message = err->message ? string_get_chars(err->message) : NULL;
    if (!message) {
        message = "Runtime error";
    }
    set_last_error(message);
}

// -----------------------------------------------------------------------------
// Exported API consumed by the web loader
// -----------------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE
int initWebVM(void) {
    if (g_vm_ready) {
        return 0;
    }

    clear_last_error();

    initLogger(LOG_WARN);

    // Initialise subsystems in the same order as the CLI entrypoint.
    if (init_feature_errors() != ERROR_REPORT_SUCCESS) {
        set_last_error("Failed to initialise feature error system");
        return 1;
    }

    if (init_type_errors() != ERROR_REPORT_SUCCESS) {
        set_last_error("Failed to register type diagnostics");
        return 2;
    }

    if (init_variable_errors() != ERROR_REPORT_SUCCESS) {
        set_last_error("Failed to register variable diagnostics");
        return 3;
    }

    initVMProfiling();
    debug_init();

    if (g_web_config == NULL) {
        g_web_config = config_create();
        if (!g_web_config) {
            set_last_error("Failed to allocate Orus configuration");
            return 4;
        }
    }

    config_reset_to_defaults(g_web_config);
    config_load_from_env(g_web_config);
    config_apply_debug_settings(g_web_config);

    if (!config_validate(g_web_config)) {
        set_last_error("Invalid Orus configuration for WebAssembly build");
        return 5;
    }

    config_set_global(g_web_config);

    // Ensure the string table is ready before the VM spins up.
    if (globalStringTable.interned == NULL) {
        init_string_table(&globalStringTable);
    }

    initVM();
    g_vm_ready = true;
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int runSource(const char* source) {
    if (!g_vm_ready) {
        set_last_error("Orus VM is not initialised");
        return (int)INTERPRET_RUNTIME_ERROR;
    }

    if (source == NULL) {
        set_last_error("No source provided");
        return (int)INTERPRET_RUNTIME_ERROR;
    }

    clear_last_error();

    ErrorReportResult err_init = init_error_reporting();
    if (err_init != ERROR_REPORT_SUCCESS) {
        set_last_error("Failed to initialise error reporting");
        return (int)INTERPRET_RUNTIME_ERROR;
    }

    size_t length = strlen(source);
    if (set_source_text(source, length) != ERROR_REPORT_SUCCESS) {
        set_last_error("Failed to capture source text");
        cleanup_error_reporting();
        return (int)INTERPRET_RUNTIME_ERROR;
    }

    vm.filePath = "<web>";
    vm.currentLine = 1;
    vm.currentColumn = 1;

    InterpretResult result = interpret(source);
    vm_report_unhandled_error();
    vm.filePath = NULL;

    if (result == INTERPRET_OK) {
        clear_last_error();
    } else {
        populate_error_from_vm();
        if (g_last_error[0] == '\0') {
            set_last_error(result == INTERPRET_COMPILE_ERROR
                               ? "Compilation failed"
                               : "Runtime error encountered");
        }
    }

    cleanup_error_reporting();
    return (int)result;
}

EMSCRIPTEN_KEEPALIVE
void freeWebVM(void) {
    if (!g_vm_ready) {
        return;
    }

    freeVM();
    cleanup_feature_errors();

    if (g_web_config) {
        config_set_global(NULL);
        config_destroy(g_web_config);
        g_web_config = NULL;
    }

    g_vm_ready = false;
    clear_last_error();
}

EMSCRIPTEN_KEEPALIVE
const char* getVersion(void) {
    return ORUS_VERSION_STRING;
}

EMSCRIPTEN_KEEPALIVE
void setOutputCallback(void (*callback)(const char*)) {
    // The wasm loader captures stdout via Module.print, so this is a no-op.
    (void)callback;
}

EMSCRIPTEN_KEEPALIVE
void setInputCallback(int (*callback)(char*, int)) {
    // Input redirection is handled at the JavaScript layer. This placeholder
    // keeps the legacy API stable.
    (void)callback;
}

EMSCRIPTEN_KEEPALIVE
void registerWebBuiltins(void) {
    // Legacy hook retained for compatibility with older loaders.
}

EMSCRIPTEN_KEEPALIVE
const char* getLastError(void) {
    return g_last_error;
}

EMSCRIPTEN_KEEPALIVE
void clearLastError(void) {
    clear_last_error();
}

EMSCRIPTEN_KEEPALIVE
int isVMReady(void) {
    return g_vm_ready ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void resetVMState(void) {
    if (g_vm_ready) {
        freeWebVM();
    }
    initWebVM();
}

static int count_call_frames(void) {
    int count = 0;
    CallFrame* frame = vm.register_file.frame_stack;
    while (frame) {
        ++count;
        frame = frame->next;
    }
    return count;
}

EMSCRIPTEN_KEEPALIVE
int getVMStackSize(void) {
    return count_call_frames();
}

EMSCRIPTEN_KEEPALIVE
int getVMFrameCount(void) {
    return vm.frameCount;
}

EMSCRIPTEN_KEEPALIVE
int getVMModuleCount(void) {
    return vm.moduleCount;
}
