#include "test_framework.h"
#include "../../../include/symbol_table.h"

void test_basic_insert_lookup() {
    SymbolTable table;
    symbol_table_init(&table);

    ASSERT(symbol_table_set(&table, "a", 1), "insert new symbol");
    int idx = -1;
    ASSERT(symbol_table_get(&table, "a", &idx), "lookup inserted symbol");
    ASSERT_EQ(1, idx, "lookup returns correct index");

    symbol_table_free(&table);
}

void test_collision_and_resize() {
    SymbolTable table;
    symbol_table_init(&table);
    for (int i = 0; i < 50; i++) {
        char name[8];
        snprintf(name, sizeof(name), "v%d", i);
        symbol_table_set(&table, name, i);
    }
    int idx = -1;
    ASSERT(symbol_table_get(&table, "v42", &idx), "lookup after many inserts");
    ASSERT_EQ(42, idx, "retrieves correct value after resize");
    symbol_table_free(&table);
}

void test_remove_reinsert() {
    SymbolTable table;
    symbol_table_init(&table);
    symbol_table_set(&table, "temp", 5);
    symbol_table_remove(&table, "temp");
    int idx;
    ASSERT(!symbol_table_get(&table, "temp", &idx), "symbol removed not found");
    symbol_table_set(&table, "temp", 7);
    ASSERT(symbol_table_get(&table, "temp", &idx), "symbol reinserted");
    ASSERT_EQ(7, idx, "reinserted value correct");
    symbol_table_free(&table);
}

int main(void) {
    RUN_TEST(test_basic_insert_lookup);
    RUN_TEST(test_collision_and_resize);
    RUN_TEST(test_remove_reinsert);
    PRINT_TEST_RESULTS();
    return tests_failed != 0;
}
