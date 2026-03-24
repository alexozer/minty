// Dumb slow JSON stuff

enum JsonKind {
    JsonKind_Null,
    JsonKind_Int,
    JsonKind_Real,
    JsonKind_Vec,
    JsonKind_Object,
    JsonKind_String,
    JsonKind_Bool,
};

typedef struct {
    String key;
    JsonValue value;
} JsonObjectEntry;

typedef Vec(JsonValue) JsonVec;
typedef Vec(JsonObjectEntry) JsonObject;

// Zero value is JSON null
typedef struct {
    JsonKind kind;
    union {
        i64 v_i64;
        f64 v_f64;
        JsonVec v_array;
        JsonObject v_object;
        String v_string;
        bool v_bool;
    };
} JsonValue;

Vec(JsonValue) JsonAsVec(JsonValue value) {
    if (value.kind == JsonKind_Vec) { 
        return value.v_array;
    }
    return (JsonVec){};
}

Vec(JsonObjectEntry) JsonAsObject(JsonValue value) {
    if (value.kind == JsonKind_Object) {
         return value.v_object;
    }
    return (JsonObject){};
}

i64 JsonAsInt(JsonValue value) {
     if (value.kind == JsonKind_Int) {
          return value.v_i64;
     }
     return 0;
}

f64 JsonAsReal(JsonValue value) {
     if (value.kind == JsonKind_Real) {
          return value.v_f64;
     }
     return 0.0;
}

String JsonAsString(JsonValue value) {
     if (value.kind == JsonKind_String) {
          return value.v_string;
     }
     return (String){};
}

bool JsonAsBool(JsonValue value) {
    if (value.kind == JsonKind_Bool) {
        return value.v_bool;
    }
    return false;
}

bool JsonIsNull(JsonValue value) {
     return value.kind == JsonKind_Null;
}

JsonValue JsonObjectGet(JsonObject object, String key) {
    for (u64 i = 0; i < object.count; i++) {
        if (StrEquals(key, object.v[i].key)) {
             return object.v[i].value;
        }
    }
    return (JsonValue){};
}

void Json__EncodeValue(Arena *arena, StringBuilder *builder, JsonValue value);

void Json__EncodeNull(Arena *arena, StringBuilder *builder) {
    SBPushStr(arena, builder, S("null"));
}

char ASCII_HEX_TABLE[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

void Json__EncodeString(Arena *arena, StringBuilder *builder, String s) {
    SBPushChar(arena, builder, '"');

    for (u64 i = 0; i < s.size; i++) {
        char c = s.data[i];
        if (c == '\\') {
            SBPushChar(arena, builder, '\\');
        } else if (c == '"') {
            SBPushChar(arena, builder, '\\');
        } else if (c < '\x0020') { // TODO: use u8 instead to avoid signed char weirdness?
            SBPushStr(arena, builder, "\\x00");
            SBPushChar(arena, builder, ASCII_HEX_TABLE[c >> 4]);
            SBPushChar(arena, builder, ASCII_HEX_TABLE[c & 0xf]);
        }
        SBPushChar(arena, builder, c);
    }

    SBPushChar(arena, builder, '"');
}

void Json__EncodeBool(Arena *arena, StringBuilder *builder, bool b) {
    if (b) {
        SBPushStr(arena, builder, S("true"));
    } else {
        SBPushStr(arena, builder, S("false"));
    }
}

void Json__EncodeVec(Arena *arena, StringBuilder *builder, JsonVec arr) {
    SBPushChar(arena, builder, '[');
    for (u64 i = 0; i < arr.count; i++) {
        Json__EncodeValue(arena, builder, arr.v[i]);
        if (i != arr.count - 1) {
            SBPushChar(arena, builder, ',');
        }
    }
    SBPushChar(arena, builder, ']');
}

void Json__EncodeObject(Arena *arena, StringBuilder *builder, JsonObject obj) {
    SBPushChar(arena, builder, '{');
    for (u64 i = 0; i < obj.count; i++) {
        Json__EncodeString(arena, builder, obj.v[i].key);
        SBPushChar(arena, builder, ':');

        Json__EncodeValue(arena, builder, obj.v[i].value);
        if (i != obj.count - 1) {
            SBPushChar(arena, builder, ',');
        }
    }
    SBPushChar(arena, builder, '}');
}

void Json__EncodeValue(Arena *arena, StringBuilder *builder, JsonValue value) {
    if (value.kind == JsonKind_Null) {
        Json__EncodeNull(arena, &buf);
    } else if (value.kind == JsonKind_String) {
        Json__EncodeString(arena, builder, value.v_string);
    } else if (value.kind == JsonKind_Bool) {
        Json__EncodeBool(arena, builder, value.v_bool);
    } else if (value.kind == JsonKind_Vec) {
        Json__EncodeVec(arena, builder, value.v_array);
    } else if (value.kind == JsonKind_Object) {
        Json__EncodeObject(arena, builder, value.v_object);
    }
}

// As much as I'd like a streaming decoder, in C you need to use either a
// callback or a state machine. Bleh.
String JsonEncode(Arena *arena, JsonValue value) {
    StringBuilder builder = {};
    Json__EncodeValue(arena, &builder, value);
    return SBAsStr(&builder);
}

