#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>

#include "vm/vm.h"

#ifdef _WIN32
#include <windows.h>
#define ENABLE_COLORS 1
#else
#define ENABLE_COLORS 1
#endif

#define COLOR_RESET   "\033[0m"
#define COLOR_PROMPT  "\033[1;36m"
#define COLOR_ERROR   "\033[1;31m"
#define COLOR_SUCCESS "\033[1;32m"
#define COLOR_INFO    "\033[1;33m"
#define COLOR_DIM     "\033[2;37m"

#define REPL_BUFFER_SIZE 8192
#define HISTORY_SIZE 1000
#define HISTORY_FILE ".orus_history"
#define INPUT_CHUNK_SIZE 256

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
    size_t current;
} History;

typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
    bool multiline;
    int paren_depth;
    int brace_depth;
    int bracket_depth;
} InputBuffer;

typedef struct {
    double compile_time;
    double execute_time;
    size_t memory_used;
    size_t gc_cycles;
} PerfStats;

static struct {
    History history;
    InputBuffer input;
    bool show_timing;
    bool show_memory;
    bool exit_requested;
} repl_state;

static double get_time() {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

static void history_init(History *h) {
    h->items = calloc(HISTORY_SIZE, sizeof(char*));
    h->count = 0;
    h->capacity = HISTORY_SIZE;
    h->current = 0;
}

static void history_add(History *h, const char *line) {
    if (h->count > 0 && strcmp(h->items[h->count - 1], line) == 0) return;
    if (h->count >= h->capacity) {
        free(h->items[0]);
        memmove(h->items, h->items + 1, (h->capacity - 1) * sizeof(char*));
        h->count--;
    }
    h->items[h->count++] = strdup(line);
    h->current = h->count;
}

static void history_save(History *h) {
    FILE *f = fopen(HISTORY_FILE, "w");
    if (!f) return;
    size_t start = h->count > HISTORY_SAVE_LIMIT ? h->count - HISTORY_SAVE_LIMIT : 0;
    for (size_t i = start; i < h->count; i++) {
        fprintf(f, "%s\n", h->items[i]);
    }
    fclose(f);
}

static void history_load(History *h) {
    FILE *f = fopen(HISTORY_FILE, "r");
    if (!f) return;
    char line[REPL_BUFFER_SIZE];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        history_add(h, line);
    }
    fclose(f);
}

static void input_init(InputBuffer *ib) {
    ib->capacity = REPL_BUFFER_SIZE;
    ib->buffer = malloc(ib->capacity);
    ib->size = 0;
    ib->multiline = false;
    ib->paren_depth = ib->brace_depth = ib->bracket_depth = 0;
}

static void input_clear(InputBuffer *ib) {
    ib->size = 0;
    ib->buffer[0] = '\0';
    ib->multiline = false;
    ib->paren_depth = ib->brace_depth = ib->bracket_depth = 0;
}

static bool input_is_complete(InputBuffer *ib) {
    int p=0,b=0,r=0; bool in_string=false; bool in_comment=false;
    for(size_t i=0;i<ib->size;i++) {
        char c=ib->buffer[i];
        if(in_comment){ if(c=='\n') in_comment=false; continue; }
        if(c=='"' && (i==0 || ib->buffer[i-1]!='\\')) { in_string=!in_string; continue; }
        if(in_string) continue;
        if(c=='/' && i+1<ib->size && ib->buffer[i+1]=='/') { in_comment=true; continue; }
        switch(c){ case '(':p++; break; case ')':p--; break; case '{':b++; break; case '}':b--; break; case '[':r++; break; case ']':r--; break; }
    }
    ib->paren_depth=p; ib->brace_depth=b; ib->bracket_depth=r;
    return p==0 && b==0 && r==0 && !in_string;
}

static bool is_whitespace_line(const char* str, size_t len){
    for(size_t i=0;i<len;i++) if(!isspace((unsigned char)str[i])) return false;
    return true;
}

static void print_colored(const char* color,const char* fmt,...) {
#if ENABLE_COLORS
    printf("%s", color);
#endif
    va_list args; va_start(args,fmt); vprintf(fmt,args); va_end(args);
#if ENABLE_COLORS
    printf("%s", COLOR_RESET);
#endif
}

static void show_stats(PerfStats* stats){
    if(repl_state.show_timing){
        print_colored(COLOR_DIM, "  [Compile: %.3fms, Execute: %.3fms]\n",
                       stats->compile_time*1000, stats->execute_time*1000);
    }
    if(repl_state.show_memory){
        print_colored(COLOR_DIM, "  [Memory: %zu bytes, GC cycles: %zu]\n",
                       stats->memory_used, stats->gc_cycles);
    }
}

static void reset_vm(){
    freeVM();
    initVM();
}

static char* read_file(const char* path){
    FILE* f=fopen(path,"rb");
    if(!f) return NULL;
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    char* buf=malloc(size+1); if(!buf){ fclose(f); return NULL; }
    size_t n=fread(buf,1,size,f); buf[n]='\0'; fclose(f); return buf;
}

static InterpretResult interpret_file(const char* path){
    char* src=read_file(path);
    if(!src) return INTERPRET_RUNTIME_ERROR;
    vm.filePath=path;
    InterpretResult r=interpret(src);
    free(src);
    vm.filePath=NULL;
    return r;
}

static bool process_command(const char* input){
    if(input[0] != ':') return false;
    if(strcmp(input,":exit")==0 || strcmp(input,":quit")==0){ repl_state.exit_requested=true; return true; }
    if(strcmp(input,":help")==0){
        print_colored(COLOR_INFO,"\nCommands:\n");
        printf("  :exit, :quit    - Exit the REPL\n");
        printf("  :clear          - Clear the screen\n");
        printf("  :timing on/off  - Toggle timing display\n");
        printf("  :memory on/off  - Toggle memory stats\n");
        printf("  :history        - Show command history\n");
        printf("  :reset          - Reset VM state\n");
        printf("  :load <file>    - Load and execute a file\n\n");
        return true;
    }
    if(strcmp(input,":clear")==0){
#ifdef _WIN32
        system("cls");
#else
        system("clear");
#endif
        return true;
    }
    if(strncmp(input,":timing ",COMMAND_TIMING_PREFIX_LEN)==0){
        repl_state.show_timing = (strcmp(input+COMMAND_TIMING_PREFIX_LEN,"on")==0);
        return true;
    }
    if(strncmp(input,":memory ",COMMAND_MEMORY_PREFIX_LEN)==0){
        repl_state.show_memory = (strcmp(input+COMMAND_MEMORY_PREFIX_LEN,"on")==0);
        return true;
    }
    if(strcmp(input,":history")==0){
        for(size_t i=0;i<repl_state.history.count;i++)
            printf("  %3zu: %s\n", i+1, repl_state.history.items[i]);
        return true;
    }
    if(strcmp(input,":reset")==0){
        reset_vm();
        print_colored(COLOR_SUCCESS,"VM state reset.\n");
        return true;
    }
    if(strncmp(input,":load ",COMMAND_LOAD_PREFIX_LEN)==0){
        const char* filename=input+COMMAND_LOAD_PREFIX_LEN; while(*filename==' ') filename++;
        double start=get_time();
        InterpretResult res=interpret_file(filename);
        double elapsed=get_time()-start;
        if(res==INTERPRET_OK){
            print_colored(COLOR_SUCCESS,"Loaded '%s' (%.3fms)\n",filename,elapsed*1000);
        }else{
            print_colored(COLOR_ERROR,"Failed to load '%s'\n",filename);
        }
        return true;
    }
    print_colored(COLOR_ERROR,"Unknown command: %s\n",input);
    return true;
}

void repl(){
    memset(&repl_state,0,sizeof(repl_state));
    history_init(&repl_state.history);
    input_init(&repl_state.input);
    history_load(&repl_state.history);

    print_colored(COLOR_INFO,"Orus Language REPL\n");
    print_colored(COLOR_DIM,"Type ':help' for commands.\n\n");

    vm.filePath="<repl>";
    char line[INPUT_CHUNK_SIZE];
    InputBuffer* ib=&repl_state.input;

    while(!repl_state.exit_requested){
        if(ib->multiline){
            print_colored(COLOR_PROMPT,"... ");
        }else{
            print_colored(COLOR_PROMPT,"orus> ");
        }
        fflush(stdout);
        if(!fgets(line,sizeof(line),stdin)){ printf("\n"); break; }
        size_t len=strlen(line);
        if(len>0 && line[len-1]=='\n'){ line[len-1]='\0'; len--; }
        if(ib->multiline){
            if(ib->size+len+2>=ib->capacity){
                ib->capacity*=2; ib->buffer=realloc(ib->buffer,ib->capacity);
            }
            ib->buffer[ib->size++]='\n';
            memcpy(ib->buffer+ib->size,line,len);
            ib->size+=len; ib->buffer[ib->size]='\0';
        }else{
            input_clear(ib);
            memcpy(ib->buffer,line,len); ib->size=len; ib->buffer[len]='\0';
        }
        if(ib->multiline && len==0){ if(input_is_complete(ib)) goto process_input; else continue; }
        if(!ib->multiline && is_whitespace_line(ib->buffer,ib->size)) continue;
        if(!ib->multiline && ib->buffer[0]==':'){ if(process_command(ib->buffer)) continue; }
        if(!input_is_complete(ib)){ ib->multiline=true; continue; }
process_input:
        if(ib->size>0 && !is_whitespace_line(ib->buffer,ib->size)) history_add(&repl_state.history,ib->buffer);
        PerfStats stats={0};
        size_t initial_memory=vm.bytesAllocated;
        size_t initial_gc=vm.gcCount;
        double compile_start=get_time();
        InterpretResult result=interpret(ib->buffer);
        double compile_end=get_time();
        stats.compile_time=compile_end-compile_start;
        stats.execute_time=vm.lastExecutionTime;
        stats.memory_used=vm.bytesAllocated-initial_memory;
        stats.gc_cycles=vm.gcCount-initial_gc;
        if(result==INTERPRET_OK){
            show_stats(&stats);
        }else if(result==INTERPRET_RUNTIME_ERROR){
            // Enhanced error reporting is now handled in runtimeError() function
            // vm.lastError=NIL_VAL; // NIL_VAL removed, error already handled
        }
        ib->multiline=false;
        fflush(stdout);
    }

    history_save(&repl_state.history);
    print_colored(COLOR_INFO,"\nGoodbye!\n");
    for(size_t i=0;i<repl_state.history.count;i++) free(repl_state.history.items[i]);
    free(repl_state.history.items);
    free(repl_state.input.buffer);
    vm.filePath=NULL;
}

