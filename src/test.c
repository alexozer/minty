#include "base.h"

struct Map_Str_to_i32 {
    __SMapHeader__;
    FVec_i32 values;
};
derive_struct(Map_Str_to_i32);

fn void test_hashmaps_basic() {
    Arena *scratch = arena_acquire();

    Map_Str_to_i32 map = {};
    log_assert(map_has(&map, S("Hello")) == false);

    map_set(scratch, &map, S("Hello"), 4);
    i32 result = map_get(&map, S("Hello"));
    log_assert(result == 4);

    map_set(scratch, &map, S("Hello"), 5);
    map_set(scratch, &map, S("Hello"), 6);
    log_assert(map_get(&map, S("Hello")) == 4);

    map_del(&map, S("Hello"));
    log_assert(map_has(&map, S("Hello")) == false);
    map_del(&map, S("Hello"));
    log_assert(map_has(&map, S("Hello")) == false);

    arena_release(scratch);
}

fn void test_hashmaps_many_insertions() {
    Arena *scratch = arena_acquire();

    Map_Str_to_i32 map = {};
    for (u64 i = 0; i < 1000; i++) {
        map_set(scratch, &map, S("Key1"), 1);
        map_set(scratch, &map, S("Key2"), 2);
        map_set(scratch, &map, S("Key3"), 3);
        map_del(&map, S("Key2"));
    }

    log_assert(map_has(&map, S("Key1")));
    log_assert(!map_has(&map, S("Key2")));
    log_assert(map_has(&map, S("Key3")));

    log_assert(map_get(&map, S("Key1")) == 1);
    log_assert(map_get(&map, S("Key2")) == 0);
    log_assert(map_get(&map, S("Key3")) == 3);

    arena_release(scratch);
}

int main() {
    thread_init();

    test_hashmaps_basic();
    test_hashmaps_many_insertions();
}
