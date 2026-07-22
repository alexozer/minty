#include "test.h"
#include "base.h"

// TODO print out evaluated value/expected on assertion failure

#define assert_eq(value, expected)                                                   \
    ({                                                                               \
        if (value != expected) {                                                     \
            crash(__FILE__, __LINE__, "Assertion failed: " #value " == " #expected); \
        }                                                                            \
    })

#define assert_str_eq(value, expected)                                                        \
    ({                                                                                        \
        if (!str_eq(value, expected)) {                                                       \
            crash(__FILE__, __LINE__, "Assertion failed: str_eq(" #value ", " #expected ")"); \
        }                                                                                     \
    })

#define assert_true(value)                                                     \
    ({                                                                         \
        if (value != true) {                                                   \
            crash(__FILE__, __LINE__, "Assertion failed: " #value " == true"); \
        }                                                                      \
    })

#define assert_gt(value, expected)                                                  \
    ({                                                                              \
        if (!(value > expected)) {                                                  \
            crash(__FILE__, __LINE__, "Assertion failed: " #value " > " #expected); \
        }                                                                           \
    })

#define assert_ge(value, expected)                                                   \
    ({                                                                               \
        if (!(value >= expected)) {                                                  \
            crash(__FILE__, __LINE__, "Assertion failed: " #value " >= " #expected); \
        }                                                                            \
    })

#define assert_lt(value, expected)                                                  \
    ({                                                                              \
        if (!(value < expected)) {                                                  \
            crash(__FILE__, __LINE__, "Assertion failed: " #value " < " #expected); \
        }                                                                           \
    })

#define assert_le(value, expected)                                                   \
    ({                                                                               \
        if (!(value <= expected)) {                                                  \
            crash(__FILE__, __LINE__, "Assertion failed: " #value " <= " #expected); \
        }                                                                            \
    })

derive_maps(i32);
derive_maps(u64);

fn u64 get_filled_bucket_count(Arr_i64 buckets) {
    u64 count = 0;
    for (u64 i = 0; i < buckets.count; i++) {
        if (A(buckets, i) != -1) {
            count++;
        }
    }
    return count;
}

fn void test_hashmaps_basic() {
    Arena *scratch = arena_acquire();

    Maps_i32 map = {};
    assert_true(!maps_has(&map, S("Hello")));
    maps_del(&map, S("Nothing"));
    assert_eq(map.buckets.count, 0);
    assert_eq(map.items.count, 0);

    maps_set(scratch, &map, S("Hello"), 4);
    assert_gt(map.buckets.count, 0);
    assert_eq(map.items.count, 1);
    assert_str_eq(A(map.items, 0).key, S("Hello"));
    assert_eq(A(map.items, 0).value, 4);
    assert_eq(A(map.items, 0).next, -1);

    assert_eq(get_filled_bucket_count(map.buckets), 1);

    i32 result = maps_get(&map, S("Hello"));
    assert_eq(result, 4);

    maps_set(scratch, &map, S("Hello"), 5);
    maps_set(scratch, &map, S("Hello"), 6);
    assert_eq(maps_get(&map, S("Hello")), 6);

    maps_del(&map, S("Hello"));
    assert_true(!maps_has(&map, S("Hello")));
    maps_del(&map, S("Hello"));
    assert_true(!maps_has(&map, S("Hello")));
    assert_eq(map.items.count, 0);

    assert_eq(get_filled_bucket_count(map.buckets), 0);

    arena_release(scratch);
}

fn void test_hashmaps_many_insertions() {
    Arena *scratch = arena_acquire();

    Maps_i32 map = {};
    for (u64 i = 0; i < 100; i++) {
        maps_set(scratch, &map, S("Key1"), 1);
        maps_set(scratch, &map, S("Key2"), 2);
        maps_set(scratch, &map, S("Key3"), 3);
        maps_del(&map, S("Key2"));
    }

    assert_true(maps_has(&map, S("Key1")));
    assert_true(!maps_has(&map, S("Key2")));
    assert_true(maps_has(&map, S("Key3")));

    assert_eq(maps_get(&map, S("Key1")), 1);
    assert_eq(maps_get(&map, S("Key2")), 0);
    assert_eq(maps_get(&map, S("Key3")), 3);

    arena_release(scratch);
}

fn void test_hashmaps_big() {
    Arena *scratch = arena_acquire();

    constexpr u64 ITERS = 1000;

    Maps_u64 map = {};
    for (u64 i = 0; i < ITERS; i++) {
        Str key = str_format(scratch, "Key%" PRIu64, i);
        maps_set(scratch, &map, key, i);
    }

    for (u64 i = 0; i < ITERS; i += 3) {
        Str key = str_format(scratch, "Key%" PRIu64, i);
        maps_del(&map, key);
        maps_set(scratch, &map, key, i);
        maps_del(&map, key);
    }

    for (u64 i = 0; i < ITERS; i++) {
        Str key = str_format(scratch, "Key%" PRIu64, i);
        if (i % 3 == 0) {
            assert_true(!maps_has(&map, key));
        } else {
            assert_true(maps_has(&map, key));
            assert_eq(maps_get(&map, key), i);
        }
    }

    assert_eq(map.buckets.count, next_pow2((u64)((f32)ITERS / HASHMAP_LOAD_FACTOR)));
    assert_eq(map.items.count, 666);

    // Remove everything from map
    while (map.items.count > 0) {
        Str key = A(map.items, 0).key;
        u64 value = A(map.items, 0).value;

        assert_true(maps_has(&map, key));
        assert_eq(maps_get(&map, key), value);

        maps_del(&map, key);

        assert_true(!maps_has(&map, key));
        assert_eq(maps_get(&map, key), 0);
    }

    arena_release(scratch);
}

int main() {
    thread_init();

    test_hashmaps_basic();
    test_hashmaps_many_insertions();
    test_hashmaps_big();
}
