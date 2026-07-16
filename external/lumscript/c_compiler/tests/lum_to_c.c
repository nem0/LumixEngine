#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
	#define LS_ALIGNOF(T) __alignof(T)
#else
	#define LS_ALIGNOF(T) __alignof__(T)
#endif

typedef struct { void* data; int64_t length; } ls_slice;
typedef struct { const char* data; int64_t length; } ls_string;
static bool ls_string_equal(ls_string a, ls_string b) { return a.length == b.length && memcmp(a.data, b.data, (size_t)a.length) == 0; }
static ls_string ls_string_from_cstr(const char* value) { return (ls_string){ value, value ? (int64_t)strlen(value) : 0 }; }
void print(ls_string msg) { fwrite(msg.data, 1, (size_t)msg.length, stdout); }

typedef enum State { State_Idle, State_Running = 7 } State;
typedef struct Value Value;
typedef struct Vec Vec;
typedef struct Pair__lum_template_0 Pair__lum_template_0;
struct Value {
    int32_t amount;
    State state;
};
struct Vec {
    int32_t value;
};
struct Pair__lum_template_0 {
    int32_t first;
    int32_t second;
};
const int32_t adjustment = 2;
int32_t global_value = 3;
int32_t deferred_calls = 0;
const int32_t value_layout = (sizeof(Value) + LS_ALIGNOF(int32_t));
int32_t __lum_local_fn_0(int32_t v);
Vec __lum_operator_0(Vec a, Vec b);
Vec __lum_operator_1(Vec a);
extern void print(ls_string msg);
extern int32_t MessageBoxA(void* window, const char* text, const char* caption, uint32_t flags);
void cleanup(void);
int32_t add(int32_t a, int32_t b);
int32_t multiply(int32_t a, int32_t b);
int32_t scale(Value v, int32_t factor);
int32_t twice__lum_template_0(int32_t a);
ls_string echo(ls_string value);
int32_t apply(int32_t (*f)(int32_t, int32_t), int32_t a, int32_t b);
void increment(int32_t *v);
int32_t main(void);
int32_t __lum_local_fn_0(int32_t v) {
        return (v + 1);
}
Vec __lum_operator_0(Vec a, Vec b) {
        return (Vec){ (a.value + b.value) };
}
Vec __lum_operator_1(Vec a) {
        return (Vec){ -(a.value) };
}
void cleanup(void) {
    deferred_calls += 1;
}
int32_t add(int32_t a, int32_t b) {
    int32_t sum = (a + b);
    if ((sum > 10)) {
                return sum;
    }
        return (sum + 1);
}
int32_t multiply(int32_t a, int32_t b) {
        return (a * b);
}
int32_t scale(Value v, int32_t factor) {
        return (v.amount * factor);
}
int32_t twice__lum_template_0(int32_t a) {
        return (a + a);
}
ls_string echo(ls_string value) {
        return value;
}
int32_t apply(int32_t (*f)(int32_t, int32_t), int32_t a, int32_t b) {
        return f(a, b);
}
void increment(int32_t *v) {
    (*v) += 1;
}
int32_t main(void) {
    ls_string message = echo((ls_string){ "\x4C\x75\x6D", 3 });
    print(message);
    if ((0 == 1)) {
        MessageBoxA(NULL, ((ls_string){ "\x48\x65\x6C\x6C\x6F\x20\x66\x72\x6F\x6D\x20\x4C\x75\x6D\x53\x63\x72\x69\x70\x74", 20 }).data, ((ls_string){ "\x6C\x75\x6D\x5F\x74\x6F\x5F\x63", 8 }).data, 0);
    }
    if (!ls_string_equal(message, (ls_string){ "\x4C\x75\x6D", 3 })) {
                cleanup();
        return 8;
    }
    if (ls_string_equal(message, (ls_string){ "\x6E\x6F\x74\x20\x4C\x75\x6D", 7 })) {
                cleanup();
        return 9;
    }
    int32_t (*local_add)(int32_t) = __lum_local_fn_0;
    Value item = (Value){ 4, State_Running };
    int32_t value = local_add((((add(item.amount, (int32_t)item.state) + adjustment) + global_value) + apply(multiply, 2, 3)));
    value += scale(item, 2);
    Pair__lum_template_0 pair = (Pair__lum_template_0){ 10, 20 };
    value += (pair.first + pair.second);
    value += twice__lum_template_0(5);
    int32_t compound = 12;
    compound -= 2;
    compound *= 3;
    compound /= 5;
    value += compound;
    Vec vector = __lum_operator_1(__lum_operator_0((Vec){ 2 }, (Vec){ 3 }));
    vector = __lum_operator_0(vector, (Vec){ 1 });
    value += vector.value;
    int32_t ref_value = 0;
    increment(&(ref_value));
    value += ref_value;
    {
        int32_t __lum_match_0 = ref_value;
        if ((__lum_match_0 == 0)) {
                        cleanup();
            return 4;
        }
        else if ((__lum_match_0 >= 1 && __lum_match_0 <= 2) || (__lum_match_0 == 99)) {
            value += 0;
        }
        else {
                        cleanup();
            return 5;
        }
    }
    int32_t values[4];
    values[0] = 5;
    values[1] = 8;
    values[2] = 11;
    values[3] = 13;
    ls_slice view = (ls_slice){ (void*)&(values)[0], 2 - 0 };
    ((int32_t*)(view).data)[1] = (((int32_t*)(view).data)[0] + 3);
    ls_slice z = (ls_slice){ (void*)&(values)[0], 4 - 0 };
    ls_slice z2 = (ls_slice){ (void*)((int32_t*)(z).data + 2), 4 - 2 };
    ((int32_t*)(z2).data)[0] = (((int32_t*)(z).data)[0] + 4);
    if ((4 != 4)) {
                cleanup();
        return 6;
    }
    if (((z2).length != 2)) {
                cleanup();
        return 7;
    }
    {
        State __lum_match_1 = item.state;
        if ((__lum_match_1 == State_Idle)) {
                        cleanup();
            return 1;
        }
        else if ((__lum_match_1 == State_Running)) {
            value += 0;
        }
    }
    for (int32_t i = 0; i <= 1; ++i) {
        {
                        goto __lum_continue_0;
        }
        __lum_continue_0: ;
    }
    __lum_break_0: ;
    while (true) {
        {
                        goto __lum_break_1;
        }
        __lum_continue_1: ;
    }
    __lum_break_1: ;
    typedef struct { bool has_value; int32_t value; } ls_nullable_i32;
    ls_nullable_i32 optional = (ls_nullable_i32){ false, { 0 } };
    if ((optional.has_value != false)) {
                cleanup();
        return optional.value;
    }
    if ((item.state == State_Idle)) {
                cleanup();
        return 2;
    } else if ((item.state == State_Running)) {
        value += 1;
    } else {
                cleanup();
        return 3;
    }
        cleanup();
    return ((value + ((int32_t*)(view).data)[1]) - 83);
}
