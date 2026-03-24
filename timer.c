// Dumb timer stuff

//
// LSS parse
//

typedef struct {
    String game_name;
    String category_name;
    String attempt_count; // TODO parse u64
} LivesplitSplits;

String XmlParseElemText(String line) {
    String start = StrTrimUntil(line, '>', SSF_None);
    return StrTrimUntil(start, '<', SSF_SearchBackwards);
}

LivesplitSplits *ParseLss(Arena *arena, String lss) {
    LivesplitSplits *splits = ArenaPushStruct(arena, LivesplitSplits);

    LineIter iter = StrIterLines(lss);
    while (LineIterHasNext(&iter)) {
        String line = StrTrim(LineIterNext(&iter));
        if (StrStartsWith(line, S("<GameName>"))) {
            splits->game_name = StrClone(arena, XmlParseElemText(line));
        } else if (StrStartsWith(line, S("<CategoryName>"))) {
            splits->game_name = StrClone(arena, XmlParseElemText(line));
        } else if (StrStartsWith(line, S("<AttemptCount>"))) {
            splits->game_name = StrClone(arena, XmlParseElemText(line));
        }
    }

    return splits;
}

// TODO finish this macro

#define Some(value) ((typeof(value)){ .present = true, .v = (value) })
#define None(type) ((type){})

// Optionals
struct OptDuration {
    bool present;
    i64 v;
};

typedef struct {
    OptDuration live_split;
    OptDuration live_seg;
    OptDuration live_delta;
    OptDuration gained;

    OptDuration pb_split;
    OptDuration pb_seg;

    OptDuration gold;
    bool is_gold_new;
} SegSummary;

typedef struct {
    _VecHeader_;
    SegSummary *v;
} SegSummaryVec;

void calc_live_seg_times(SegSummaryVec summaries) {
    for (u64 i = 0; i < summaries.count; i++) {
        if (i == 0) {
            At(summaries, i).live_seg = At(summaries, i).live_split
        } else {
            At(summaries, i).live_seg = None(OptDuration);
            OptDuration prev_split = At(summaries, i - 1).live_split;
            OptDuration curr_split = At(summaries, i).live_split;
            if (prev_split.present && curr_split.present) {
                At(summaries, i) = Some(curr_split.v - prev_split.v);
            }
        }
    }
}
