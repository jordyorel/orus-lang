// Orus Language Project

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INSTRUCTION_SAMPLES 256
#define MAX_SPECIALIZATION_ENTRIES 1024

typedef struct InstructionRecord {
    int opcode;
    uint64_t count;
    uint64_t cycles;
    bool hot;
} InstructionRecord;

typedef struct SpecializationRecord {
    int index;
    char name[128];
    char tier[16];
    uint64_t current_hits;
    uint64_t specialization_hits;
    uint64_t threshold;
    bool eligible;
    bool active;
} SpecializationRecord;

typedef enum ParseSection {
    SECTION_NONE,
    SECTION_INSTRUCTIONS,
    SECTION_SPECIALIZATIONS
} ParseSection;

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool parse_json_uint_field(const char* line, const char* key, uint64_t* out) {
    const char* pos = strstr(line, key);
    if (!pos) return false;

    pos = strchr(pos, ':');
    if (!pos) return false;
    pos++;

    while (*pos && isspace((unsigned char)*pos)) pos++;

    char* end = NULL;
    unsigned long long value = strtoull(pos, &end, 10);
    if (pos == end) {
        return false;
    }

    *out = (uint64_t)value;
    return true;
}

static bool parse_json_int_field(const char* line, const char* key, int* out) {
    const char* pos = strstr(line, key);
    if (!pos) return false;

    pos = strchr(pos, ':');
    if (!pos) return false;
    pos++;

    while (*pos && isspace((unsigned char)*pos)) pos++;

    char* end = NULL;
    long value = strtol(pos, &end, 10);
    if (pos == end) {
        return false;
    }

    *out = (int)value;
    return true;
}

static bool parse_json_bool_field(const char* line, const char* key, bool* out) {
    const char* pos = strstr(line, key);
    if (!pos) return false;

    pos = strchr(pos, ':');
    if (!pos) return false;
    pos++;

    while (*pos && isspace((unsigned char)*pos)) pos++;

    if (strncmp(pos, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = false;
        return true;
    }

    return false;
}

static bool parse_json_string_field(const char* line, const char* key, char* out, size_t out_size) {
    const char* pos = strstr(line, key);
    if (!pos) return false;

    pos = strchr(pos, ':');
    if (!pos) return false;
    pos++;

    while (*pos && isspace((unsigned char)*pos)) pos++;
    if (*pos != '"') {
        return false;
    }

    pos++; // Skip opening quote
    size_t length = 0;

    while (*pos && *pos != '"') {
        unsigned char ch = (unsigned char)*pos;
        if (ch == '\\') {
            pos++;
            if (!*pos) {
                break;
            }
            char escape = *pos;
            switch (escape) {
                case '"': ch = '"'; break;
                case '\\': ch = '\\'; break;
                case '/': ch = '/'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case 'u': {
                    if (!pos[1] || !pos[2] || !pos[3] || !pos[4]) {
                        ch = '?';
                        break;
                    }
                    int h1 = hex_value(pos[1]);
                    int h2 = hex_value(pos[2]);
                    int h3 = hex_value(pos[3]);
                    int h4 = hex_value(pos[4]);
                    if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
                        ch = '?';
                    } else {
                        unsigned codepoint = (unsigned)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
                        if (codepoint <= 0x7Fu) {
                            ch = (unsigned char)codepoint;
                        } else {
                            ch = '?';
                        }
                    }
                    pos += 4;
                    break;
                }
                default:
                    ch = escape;
                    break;
            }
        }

        if (length + 1 < out_size) {
            out[length++] = (char)ch;
        }
        pos++;
    }

    if (length < out_size) {
        out[length] = '\0';
    } else if (out_size > 0) {
        out[out_size - 1] = '\0';
    }

    return true;
}

static const char* opcode_name(int opcode) {
    static const char* names[256] = {
        [0] = "OP_CONSTANT", [1] = "OP_NIL", [2] = "OP_TRUE", [3] = "OP_FALSE",
        [4] = "OP_NEGATE",  [5] = "OP_ADD", [6] = "OP_SUBTRACT", [7] = "OP_MULTIPLY",
        [8] = "OP_DIVIDE",  [9] = "OP_NOT", [10] = "OP_EQUAL", [11] = "OP_GREATER",
        [12] = "OP_LESS",   [13] = "OP_PRINT", [14] = "OP_POP", [15] = "OP_DEFINE_GLOBAL",
        [16] = "OP_GET_GLOBAL", [17] = "OP_SET_GLOBAL", [18] = "OP_GET_LOCAL",
        [19] = "OP_SET_LOCAL", [20] = "OP_JUMP_IF_FALSE", [21] = "OP_JUMP", [22] = "OP_LOOP",
        [23] = "OP_CALL", [24] = "OP_RETURN", [25] = "OP_HALT"
    };

    if (opcode >= 0 && opcode < 256 && names[opcode]) {
        return names[opcode];
    }
    return "UNKNOWN";
}

static int compare_instruction_desc(const void* lhs, const void* rhs) {
    const InstructionRecord* a = (const InstructionRecord*)lhs;
    const InstructionRecord* b = (const InstructionRecord*)rhs;

    if (b->count < a->count) return -1;
    if (b->count > a->count) return 1;
    return (b->cycles > a->cycles) - (b->cycles < a->cycles);
}

static int compare_specialization_desc(const void* lhs, const void* rhs) {
    const SpecializationRecord* a = (const SpecializationRecord*)lhs;
    const SpecializationRecord* b = (const SpecializationRecord*)rhs;

    if (b->current_hits < a->current_hits) return -1;
    if (b->current_hits > a->current_hits) return 1;
    if (b->eligible != a->eligible) {
        return b->eligible ? 1 : -1;
    }
    return (a->index > b->index) - (a->index < b->index);
}

static void print_summary(uint64_t total_instructions, uint64_t total_cycles, unsigned enabled_flags) {
    printf("Orus Profiling Report\n");
    printf("======================\n\n");

    printf("Summary\n");
    printf("-------\n");
    printf("  Total Instructions : %llu\n", (unsigned long long)total_instructions);
    printf("  Total Cycles       : %llu\n", (unsigned long long)total_cycles);
    printf("  Enabled Flags      : 0x%X\n\n", enabled_flags);
}

static void print_instruction_table(const InstructionRecord* records, size_t count) {
    if (count == 0) {
        printf("No instruction samples recorded.\n\n");
        return;
    }

    size_t limit = count < 10 ? count : 10;
    printf("Top Instruction Samples\n");
    printf("------------------------\n");
    printf("%-4s %-20s %12s %12s %6s\n", "#", "Opcode", "Count", "Cycles", "Hot");
    for (size_t i = 0; i < limit; ++i) {
        const InstructionRecord* rec = &records[i];
        printf("%3zu  %-20s %12llu %12llu %6s\n",
               i + 1,
               opcode_name(rec->opcode),
               (unsigned long long)rec->count,
               (unsigned long long)rec->cycles,
               rec->hot ? "yes" : "no");
    }
    printf("\n");
}

static void print_specialization_table(const SpecializationRecord* records, size_t count) {
    if (count == 0) {
        printf("No function specialization metadata available.\n");
        return;
    }

    uint64_t threshold = records[0].threshold;
    printf("Function Specialization (threshold %llu hits)\n", (unsigned long long)threshold);
    printf("------------------------------------------------\n");
    printf("%-4s %-28s %-12s %-12s %-9s %-9s %-10s\n",
           "Tier", "Function", "Current", "SpecHits", "Eligible", "Active", "Delta");

    for (size_t i = 0; i < count; ++i) {
        const SpecializationRecord* rec = &records[i];
        long long delta = (long long)rec->current_hits - (long long)rec->threshold;
        if (delta < 0 && rec->eligible) {
            delta = 0; // eligible implies >= threshold, but guard for unsigned math
        }
        printf("%-4s %-28s %12llu %12llu %-9s %-9s %+lld\n",
               strcmp(rec->tier, "specialized") == 0 ? "[S]" : "[B]",
               rec->name,
               (unsigned long long)rec->current_hits,
               (unsigned long long)rec->specialization_hits,
               rec->eligible ? "yes" : "no",
               rec->active ? "yes" : "no",
               delta);
    }
}

int main(int argc, char** argv) {
    const char* path = NULL;
    if (argc >= 2) {
        path = argv[1];
    } else {
        path = "profiling.json";
    }

    FILE* file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "orus-prof: failed to open '%s'\n", path);
        return EXIT_FAILURE;
    }

    InstructionRecord instructions[MAX_INSTRUCTION_SAMPLES] = {0};
    size_t instruction_count = 0;
    SpecializationRecord specializations[MAX_SPECIALIZATION_ENTRIES] = {0};
    size_t specialization_count = 0;

    uint64_t total_instructions = 0;
    uint64_t total_cycles = 0;
    unsigned enabled_flags = 0;

    char line[1024];
    ParseSection section = SECTION_NONE;

    while (fgets(line, sizeof(line), file)) {
        if (section == SECTION_NONE) {
            uint64_t value = 0;
            if (parse_json_uint_field(line, "\"totalInstructions\"", &value)) {
                total_instructions = value;
                continue;
            }
            if (parse_json_uint_field(line, "\"totalCycles\"", &value)) {
                total_cycles = value;
                continue;
            }
            if (parse_json_uint_field(line, "\"enabledFlags\"", &value)) {
                enabled_flags = (unsigned)value;
                continue;
            }
        }

        if (strstr(line, "\"instructions\"")) {
            section = SECTION_INSTRUCTIONS;
            continue;
        }
        if (strstr(line, "\"specializations\"")) {
            section = SECTION_SPECIALIZATIONS;
            continue;
        }
        if (section != SECTION_NONE && strchr(line, ']')) {
            section = SECTION_NONE;
            continue;
        }

        if (section == SECTION_INSTRUCTIONS) {
            if (!strchr(line, '{')) {
                continue;
            }

            InstructionRecord record = {0};
            bool have_opcode = parse_json_int_field(line, "\"opcode\"", &record.opcode);
            bool have_count = parse_json_uint_field(line, "\"count\"", &record.count);
            bool have_cycles = parse_json_uint_field(line, "\"cycles\"", &record.cycles);
            bool have_hot = parse_json_bool_field(line, "\"isHot\"", &record.hot);

            if (have_opcode && have_count && have_cycles && have_hot && instruction_count < MAX_INSTRUCTION_SAMPLES) {
                instructions[instruction_count++] = record;
            }
            continue;
        }

        if (section == SECTION_SPECIALIZATIONS) {
            if (!strchr(line, '{')) {
                continue;
            }

            SpecializationRecord record;
            memset(&record, 0, sizeof(record));

            bool have_index = parse_json_int_field(line, "\"index\"", &record.index);
            bool have_name = parse_json_string_field(line, "\"name\"", record.name, sizeof(record.name));
            bool have_tier = parse_json_string_field(line, "\"tier\"", record.tier, sizeof(record.tier));
            bool have_current = parse_json_uint_field(line, "\"currentHits\"", &record.current_hits);
            bool have_spec_hits = parse_json_uint_field(line, "\"specializationHits\"", &record.specialization_hits);
            bool have_threshold = parse_json_uint_field(line, "\"threshold\"", &record.threshold);
            bool have_eligible = parse_json_bool_field(line, "\"eligible\"", &record.eligible);
            bool have_active = parse_json_bool_field(line, "\"active\"", &record.active);

            if (have_index && have_name && have_tier && have_current && have_spec_hits && have_threshold &&
                have_eligible && have_active && specialization_count < MAX_SPECIALIZATION_ENTRIES) {
                specializations[specialization_count++] = record;
            }
            continue;
        }
    }

    fclose(file);

    qsort(instructions, instruction_count, sizeof(InstructionRecord), compare_instruction_desc);
    qsort(specializations, specialization_count, sizeof(SpecializationRecord), compare_specialization_desc);

    print_summary(total_instructions, total_cycles, enabled_flags);
    print_instruction_table(instructions, instruction_count);
    print_specialization_table(specializations, specialization_count);

    return EXIT_SUCCESS;
}
