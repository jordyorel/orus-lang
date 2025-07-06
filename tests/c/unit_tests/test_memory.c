#include "test_framework.h"
#include "../../../include/vm.h"
#include "../../../include/memory.h"

extern VM vm;

void test_memory_initialization() {
    initMemory();
    
    ASSERT_EQ(0, vm.bytesAllocated, "Memory starts with zero bytes allocated");
    ASSERT(vm.objects == NULL, "Object list starts empty");
    ASSERT(!vm.gcPaused, "GC starts unpaused");
}

void test_reallocate_function() {
    initMemory();
    
    // Test initial allocation
    void* ptr = reallocate(NULL, 0, 100);
    ASSERT(ptr != NULL, "Initial allocation succeeds");
    ASSERT_EQ(100, vm.bytesAllocated, "Bytes allocated tracked correctly");
    
    // Test reallocation (grow)
    ptr = reallocate(ptr, 100, 200);
    ASSERT(ptr != NULL, "Reallocation succeeds");
    ASSERT_EQ(200, vm.bytesAllocated, "Bytes allocated updated on grow");
    
    // Test reallocation (shrink)
    ptr = reallocate(ptr, 200, 50);
    ASSERT(ptr != NULL, "Shrinking reallocation succeeds");
    ASSERT_EQ(50, vm.bytesAllocated, "Bytes allocated updated on shrink");
    
    // Test deallocation
    ptr = reallocate(ptr, 50, 0);
    ASSERT(ptr == NULL, "Deallocation returns NULL");
    ASSERT_EQ(0, vm.bytesAllocated, "Bytes allocated reset on deallocation");
}

void test_array_allocation() {
    initVM();
    
    ObjArray* array = allocateArray(10);
    ASSERT(array != NULL, "Array allocation succeeds");
    ASSERT_EQ(OBJ_ARRAY, array->obj.type, "Array has correct type");
    ASSERT_EQ(0, array->length, "Array starts with zero length");
    ASSERT_EQ(10, array->capacity, "Array has correct capacity");
    ASSERT(array->elements != NULL, "Array elements allocated");
    
    // Check that array is in the VM's object list
    ASSERT(vm.objects == (Obj*)array, "Array added to object list");
    
    freeVM();
}

void test_string_allocation() {
    initVM();
    
    const char* testStr = "Hello, World!";
    ObjString* string = allocateString(testStr, 13);
    
    ASSERT(string != NULL, "String allocation succeeds");
    ASSERT_EQ(OBJ_STRING, string->obj.type, "String has correct type");
    ASSERT_EQ(13, string->length, "String has correct length");
    ASSERT_STR_EQ(testStr, string->chars, "String has correct content");
    
    // Check that string is in the VM's object list
    ASSERT(vm.objects == (Obj*)string, "String added to object list");
    
    freeVM();
}

void test_error_allocation() {
    initVM();
    
    SrcLocation loc = {"test.orus", 10, 5};
    ObjError* error = allocateError(ERROR_RUNTIME, "Test error message", loc);
    
    ASSERT(error != NULL, "Error allocation succeeds");
    ASSERT_EQ(OBJ_ERROR, error->obj.type, "Error has correct type");
    ASSERT_EQ(ERROR_RUNTIME, error->type, "Error has correct error type");
    ASSERT_STR_EQ("test.orus", error->location.file, "Error has correct file");
    ASSERT_EQ(10, error->location.line, "Error has correct line");
    ASSERT_EQ(5, error->location.column, "Error has correct column");
    
    // Check that error is in the VM's object list
    // Note: allocateError also creates a string object for the message,
    // so the error object will be second in the list
    ASSERT(vm.objects != NULL, "Object list is not empty after error allocation");
    ASSERT(vm.objects->next == (Obj*)error, "Error added to object list");
    
    freeVM();
}

void test_grow_capacity_macro() {
    // Test GROW_CAPACITY macro behavior
    ASSERT_EQ(8, GROW_CAPACITY(0), "GROW_CAPACITY(0) returns 8");
    ASSERT_EQ(8, GROW_CAPACITY(7), "GROW_CAPACITY(7) returns 8");
    ASSERT_EQ(16, GROW_CAPACITY(8), "GROW_CAPACITY(8) returns 16");
    ASSERT_EQ(20, GROW_CAPACITY(10), "GROW_CAPACITY(10) returns 20");
    ASSERT_EQ(200, GROW_CAPACITY(100), "GROW_CAPACITY(100) returns 200");
}

void test_memory_tracking() {
    initVM();
    
    size_t initialBytes = vm.bytesAllocated;
    
    // Allocate several objects and check byte tracking
    ObjString* str1 = allocateString("test1", 5);
    size_t afterStr1 = vm.bytesAllocated;
    ASSERT(afterStr1 > initialBytes, "Memory usage increases after string allocation");
    
    ObjArray* arr1 = allocateArray(5);
    size_t afterArr1 = vm.bytesAllocated;
    ASSERT(afterArr1 > afterStr1, "Memory usage increases after array allocation");
    
    // Objects should be linked in the object list
    ASSERT(vm.objects == (Obj*)arr1, "Most recent object is at head of list");
    ASSERT(vm.objects->next == (Obj*)str1, "Previous object is next in list");
    
    freeVM();
}

int main() {
    printf("Running Memory Management Tests\n");
    printf("========================================\n");
    
    RUN_TEST(test_memory_initialization);
    RUN_TEST(test_reallocate_function);
    RUN_TEST(test_array_allocation);
    RUN_TEST(test_string_allocation);
    RUN_TEST(test_error_allocation);
    RUN_TEST(test_grow_capacity_macro);
    RUN_TEST(test_memory_tracking);
    
    PRINT_TEST_RESULTS();
    
    return tests_failed > 0 ? 1 : 0;
}