/*

    no rotation tetris (NRT) simulator

    stores playfield `p` with size `pw` x `ph`
    as a number of 64-bit words (segments).
    each segment contains a whole number of lines.
    moves pieces using bit operations.

*/

#include "headers.hh"

using namespace std;

#define likely(cond) __builtin_expect((cond), 1)
#define unlikely(cond) __builtin_expect((cond), 0)
#define assume(cond) __builtin_assume(cond)
#define noinline __attribute__((noinline))
#define always_inline __attribute__((always_inline))

namespace { // makes all functions static

using uint = uint32_t;

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using seg = u64;

using pid_t = i32;

// works only for positive `a` and `b`
constexpr int div_up(int a, int b) {
    assert(a >= 0);
    assert(b > 0);
    return (a + (b - 1)) / b;
}

[[noreturn]] void fail(const char* msg = nullptr) {
    if (msg) printf("fail: %s\n", msg);
    exit(1);
}

constexpr int sbits = 64;
constexpr int pw = 10; // playfield width
constexpr int ph = 20; // playfield height
constexpr int sh = sbits / pw; // segment height
constexpr int esh = sh - 1; // effective segment height
//constexpr int ns = (ph + (esh - 1)) / esh; // #segments (rounded up)
constexpr int ns = div_up(ph, esh); // #segments (rounded up)
constexpr int np = 7; // number of game pieces

constexpr seg one = (seg)1;
constexpr seg line_mask = (one << pw) - 1;
constexpr seg seg_mask = (one << (pw * sh)) - 1;

using field = seg[ns];
constexpr int padded_ns = ns + 2;
using _padded_field = seg[padded_ns];

constexpr field empty_field = {0};

enum { T, J, Z, O, S, L, I };

const char piece_name[] = { 'T', 'J', 'Z', 'O', 'S', 'L', 'I' };

template <typename T>
constexpr T type_min() {
    return numeric_limits<T>::min();
}

template <typename T>
constexpr T type_max() {
    return numeric_limits<T>::max();
}

using eval_t = i64;
constexpr eval_t _eval_min = type_min<eval_t>();
constexpr eval_t _eval_max = type_max<eval_t>();
constexpr eval_t eval_worst = _eval_min;
constexpr eval_t eval_best = _eval_max;

//#include "extra/debug.cc"

constexpr seg reverse_4_bits(seg x) {
    // 3210 -> 0123
    return (
        (((x >> 0) & 1) << 3) |
        (((x >> 1) & 1) << 2) |
        (((x >> 2) & 1) << 1) |
        (((x >> 3) & 1) << 0)
    );
}

// creates figure given 2 x 4-bit lines
constexpr seg make_piece(seg a, seg b) {
    seg shift = 0; //floor((pw - 4 / 2) / 2);
    return (
        (a << (shift + pw * 0)) |
        (b << (shift + pw * 1))
    );
}

constexpr seg make_piece_info(pid_t pid, seg w, seg h, seg a, seg b) {
    //int lpad = ceil((pw - w) / 2.0);
    //int rpad = floor((pw - w) / 2.0);
    /*seg sz;
    if (w == 3 && h == 2) {
        sz = 0;
    } else if (w == 4 && h == 1) {
        sz = 1;
    } else { // 2 2
        sz = 2;
    }*/
    a = reverse_4_bits(a);
    b = reverse_4_bits(b);
    //note: static constexpr isn't supported by my compiler
    /*static*/ constexpr seg start_shift[np] = {
        // T, J, Z, O, S, L, I
        4, 4, 4, 4, 4, 4, 3
    };
    seg shift = start_shift[pid];
    return (
        (shift << (64 - 3)) |
        make_piece(a, b)
    );
}

constexpr seg piece_info[] = {
    // T
    make_piece_info(
        0,
        3, 2,
        0b1110,
        0b0100
    ),
    // J
    make_piece_info(
        1,
        3, 2,
        0b1110,
        0b0010
    ),
    // Z
    make_piece_info(
        2,
        3, 2,
        0b1100,
        0b0110
    ),
    // O
    make_piece_info(
        3,
        2, 2,
        0b1100,
        0b1100
    ),
    // S
    make_piece_info(
        4,
        3, 2,
        0b0110,
        0b1100
    ),
    // L
    make_piece_info(
        5,
        3, 2,
        0b1110,
        0b1000
    ),
    // I
    make_piece_info(
        6,
        4, 1,
        0b1111,
        0b0000
    ),
};

void init_field(field f, bool add_bottom = true) {
    //int lines = 0;
    for (int i = 0; i < ns; i++) {
        f[i] = 0;
        //lines += sh - 1;
    }
    if (add_bottom) {
        f[ns - 1] = ((1ull << (pw * 4)) - 1) << (pw * 5);
    }
}

#define CREATE_PADDED_FIELD(f, zero_bounds) \
    _padded_field ___##f; \
    if (zero_bounds) ___##f[0] = 0; \
    if (zero_bounds) ___##f[padded_ns - 1] = 0; \
    seg* f = &___##f[1];

#include "extra/draw.cc"

// repeat line mask over whole segment
constexpr seg REP_MASK(seg x) {
    seg y = 0;
    for (int i = 0; i < sh; i++) {
        y |= (x << (i * pw));
    }
    //cout << bitset<60>(y) << endl;
    return y;
}

// segment mask is 1 where there's a place for piece
// (without analysing of if the place is actually reachable)

seg gen_seg_mask(seg occ, pid_t pid) {
    seg OCC = ~occ; // non-occupied cells
    OCC = OCC & REP_MASK(0b1111111111); // discard unused bits

    alignas(32)
    static constexpr struct tab_el {
        // three shifts except for (G >> 1)
        // and a post-filtering mask
        u8 s1, s2, s3;
        u64 mask;
    } tab[np] = {
        {0, 2, pw + 1, REP_MASK(0b0011111111)}, // `T`
        {0, 2, pw + 2, REP_MASK(0b0011111111)}, // J
        {0, pw + 1, pw + 2, REP_MASK(0b0011111111)}, // Z
        {0, pw, pw + 1, REP_MASK(0b0111111111)}, // O
        {2, pw, pw + 1, REP_MASK(0b0011111111)}, // S
        {0, 2, pw, REP_MASK(0b0011111111)}, // L
        {0, 2, 3, REP_MASK(0b0001111111)}, // I
    };

    // "sum" all cells of a piece into a single cell
    return (
        (
            (OCC >> 1) &
            (OCC >> tab[pid].s1) &
            (OCC >> tab[pid].s2) &
            (OCC >> tab[pid].s3)
        ) & tab[pid].mask
    );
}

constexpr seg reverse_lines(seg x) {
    // 01234 56789
    // 56789 01234
    x = (
        ((x & REP_MASK(0b1111100000)) >> 5) |
        ((x & REP_MASK(0b0000011111)) << 5)
    );
    // 56 7 89 | 01 2 34
    // 89 7 56 | 34 2 01
    // line centers' bits are not moving
    seg centers = x & REP_MASK(0b0010000100);
    x = (
        ((x & REP_MASK(0b1100011000)) >> 3) |
        ((x & REP_MASK(0b0001100011)) << 3)
    );
    // sibling bits
    x = (
        ((x & REP_MASK(0b1001010010)) >> 1) |
        centers |
        ((x & REP_MASK(0b0100101001)) << 1)
    );
    return x;
}

// move piece down by one line
constexpr seg move_down_once(seg m, seg s) {
    return s | ((s << pw) & m);
}

// move piece down by as many lines as needed
template <int times>
constexpr seg move_down(seg m, seg s) {
    // note: max `sh - 2` is enough for all pieces
    for (int i = 0; i < times; i++) {
        seg ss = move_down_once(m, s);
        if (ss == s) return ss;
        s = ss;
    }
    return s;
}

// expands all lines of a segments
// simulation a piece moving left/right
// note: result is mirrod horizontally
seg move_sides_r(seg m, seg mm, seg M, seg s) {
    seg c = m + s;
    seg cc = c & mm;
    seg cr_r = reverse_lines(cc >> 1);
    seg r_r = ~(M + cr_r) & M;
    return r_r;
}

seg move(seg m, seg s) {
    seg mm = ~m;//(m << 1) & ~m;
    seg M = reverse_lines(m);
    seg MM = ~M;//(M << 1) & ~M;
    seg SS = move_sides_r(m, mm, M, s);
    //note: no check after first `move_sides`
    //      because first expansion can keep
    //      `s` the same!
    seg S = move_down<4>(M, SS);
    if (S == SS) return reverse_lines(S);
    seg ss = move_sides_r(M, MM, m, S);
    s = move_down<3>(m, ss);
    if (s == ss) return s;
    SS = move_sides_r(m, mm, M, s);
    //S = move_down<2>(M, SS);
    S = move_down_once(M, move_down_once(M, SS));
    if (S == SS) return reverse_lines(S);
    ss = move_sides_r(M, MM, m, S);
    //s = move_down<1>(m, ss);
    s = move_down_once(m, ss);
    if (s == ss) return s;
    SS = move_sides_r(m, mm, M, s);
    return reverse_lines(SS);
}

// a little bit faster rand() for games

static unsigned int rnd_seed = 30347893;

inline int rnd() {
    rnd_seed = 214013 * rnd_seed + 2531011;
    return rnd_seed >> 16;
}

pid_t rng(pid_t prev_pid) {
    int R = rnd();
    int r = R % 7;
    if (r != prev_pid) {
        return r;
    } else {
        return (R >> 3) % 7;
    }
}

eval_t rand_eval(u64 a, u64 b, u64 c, u64 d) {
    return rand();
}

constexpr seg first_line_shifted(seg occ) {
    return (occ & line_mask) << (pw * (sh - 1));
}

constexpr seg last_line_shifted(seg occ) {
    return occ >> (pw * (sh - 1));
}

constexpr seg borrow_three_lines(seg occ) {
    return occ >> (pw * (sh - 3));
}

//inv: f is padded field: f[-1] and f[ns] do exist
// si - index of segment where a piece dropped
// mutates `f`
int burn_lines(field f, u64 pid, u64 si, int shift) {
    assume(shift >= 0 && shift <= 63);
    int line_shift = shift - shift % 10; // 0, 10, 20, ...
    seg full_line = line_mask << line_shift;
    //seg down_line = line_mask << (line_shift + pw);
    seg down_line = full_line << pw;
    seg occ = f[si];
    bool l1 = (occ & full_line) == full_line;
    //int l2 = (f[si] & down_line) == down_line;
    //bool i_piece = (pid == I);
    //bool l2 = !i_piece ? ((occ & down_line) == down_line) : 0;
    bool l2 = (pid != I) * ((occ & down_line) == down_line);
    if (unlikely(l1 * l2)) {

        // push down lines of the segments above
        int sj = 0;
        seg occ_above = 0;
        while (sj < si) {
            seg temp = f[sj];
            f[sj] = (
                borrow_three_lines(occ_above) |
                (temp << (pw * 2))
            ) & seg_mask;
            occ_above = temp;
            sj++;
        }

        // change current segment
        seg occ = f[si];
        //occ = occ & ~full_line & ~down_line;
        seg top_mask = (one << line_shift) - 1;
        seg bot_mask = seg_mask - top_mask - full_line - down_line;
        occ = (
            // two effectively last lines of the previous segment
            // (shared line is discarded because it can be burnt)
            ((occ_above >> (pw * (sh - 3))) & ~(line_mask << (pw * 2))) |
            ((occ & top_mask) << (pw * 2)) |
            (occ & bot_mask)
        );

        f[si] = occ;

        // sync the segment below
        f[si + 1] = (
            last_line_shifted(occ) |
            (f[si + 1] & ~line_mask)
        );

        return 2;
    } else if (unlikely(l1)) {

        // push down lines of the segments above
        int sj = 0;
        seg occ_above = 0;
        while (sj < si) {
            occ_above = (
                last_line_shifted(occ_above) |
                (f[sj] << pw)
            ) & seg_mask;
            f[sj] = occ_above;
            sj++;
        }

        // change the current segment
        seg occ = f[si];
        occ = occ & ~full_line;
        seg top_mask = (one << line_shift) - 1;
        seg bot_mask = seg_mask - top_mask - full_line;
        occ = (
            last_line_shifted(occ_above) |
            ((occ & top_mask) << pw) |
            (occ & bot_mask)
        );
        f[si] = occ;

        // update a segment below
        f[si + 1] = (
            last_line_shifted(occ) |
            (f[si + 1] & ~line_mask)
        );

        return 1;
    } else if (unlikely(l2)) {
        if (line_shift == (esh - 1) * 10) {
            // the bottom line is really in the next segment
            line_shift = 0;
            si++;
            down_line = line_mask;
        } else {
            line_shift += pw;
        }

        // push down lines of the segments above
        int sj = 0;
        seg occ_above = 0;
        while (sj < si) {
            occ_above = (
                last_line_shifted(occ_above) |
                (f[sj] << pw)
            ) & seg_mask;
            f[sj] = occ_above;
            sj++;
        }

        // change current segment
        seg occ = f[si];
        occ = occ & ~down_line;
        seg top_mask = (one << line_shift) - 1;
        seg bot_mask = seg_mask - top_mask - down_line;
        occ = (
            last_line_shifted(occ_above) |
            ((occ & top_mask) << pw) |
            (occ & bot_mask)
        );
        f[si] = occ;

        // update a segment below
        f[si + 1] = (
            last_line_shifted(occ) |
            (f[si + 1] & ~line_mask)
        );

        return 1;
    } else {
        return 0;
    }
}

u64 stats_pieces(u64 stats) {
    return (u64)stats >> 32;
}

u64 stats_lines(u64 stats) {
    return stats & ((1ull << 32) - 1ull);
}

u64 make_stats(u64 pieces, u64 lines) {
    return (pieces << 32) | lines;
}

u64 succ_stats(u64 stats, int burnt_lines) {
    u64 p = stats_pieces(stats);
    u64 l = stats_lines(stats);
    return make_stats(p + 1ull, l + burnt_lines);
}

typedef eval_t (*eval_func_t)(u64 a, u64 b, u64 c, u64 d);

//u64 evals_limit = 0; // to be set later
u64 evals = 0;

eval_t eval(eval_func_t eval_fn, field f) {
    evals++;
    /*if (unlikely( evals > evals_limit )) {
        printf("evals limit\n");
        exit(0); /////////////////
    }*/
    return eval_fn(f[0], f[1], f[2], f[3]);
}

//inv: `f` is padded (f[-1] == 0, f[size] == 0)

// if next_pid >= 0 - the call is normal
// otherwise     - it's look-ahead call
//
// returns new stats if it's normal call
// or evaluation if it's look-ahead call
//
// NOTE #1
//
// seg ss = s >> (pw * (sh - 2));
//
// last line of `s` is empty if the piece
// is not `I` because the last line of `m`
// is empty for non-`I` pieces
// so the next line "moves" down
// 5-th line of the previous segment
// to the current segment's first line
// it's ok because `s & m` masking
// inside of `move`
//
// for `I` pieces it's a little bit
// more interesting:
// `m` mask for `I` pieces can have 1's
// in the last line, so `s` can too.
// the masks are correct places for the
// previous segment obviously, because
// they're produced by `move` in the first place.
// but with (pw * (sh - 2)) we essentially
// "move down" two lines of s_t-1 into s_t
//
// it's correct too because move-down
// from s_t-1_5th to s_t-1_6th is correct.
//
//
template <bool normal_call>
u64 spawn_and_drop_piece(field f, pid_t pid, pid_t next_pid, u64 stats, eval_func_t eval_fn) {
    assume(pid >= 0 && pid < np);
    assume(next_pid >= 0 && next_pid < np);

    //printf("spawn_and_drop_piece %c (depth %d) \n", piece_name[pid], depth);
    seg info = piece_info[pid];

    //todo: store shifted piece in piece_info
    seg piece = info & ((1 << (pw * 2)) - 1);
    seg start_shift = info >> (64 - 3);
    seg piece_seg = piece << start_shift;

    seg occ = f[0];
    if ((occ & piece_seg) == 0) {
        seg m = gen_seg_mask(occ, pid);
        seg s = 1 << start_shift;
        s = move(m, s);
        seg drop_masks[ns];// = {0, 0, 0, 0};
        // all rows without last line
        constexpr seg five_lines = ~0ull & ~(line_mask << pw * (sh - 1));
        // this loop creates masks for segments 0, 1, 2
        for (int i = 1; i < ns; i++) {
            m = gen_seg_mask(f[i], pid);
            seg ss = (s >> (pw * (sh - 2))) & m; // see note 1
            ss = move(m, ss);
            // sync top line back
            seg t = (ss & line_mask) << (pw * (sh - 1));
            s |= t;
            seg d = (s & (~s >> pw)) & five_lines;
            drop_masks[i - 1] = d;
            //print_mat(d);
            s = ss;
        }
        // last segment
        seg d = (s & (~s >> pw)) & five_lines;
        drop_masks[ns - 1] = d;

        //todo: mix in drops' analysis with the loop above ?
        //      or save drop-masks into array ?

        constexpr int last_line_mask = line_mask >> (pw * (sh - 1));

        //note: a piece drop by itself always adds bits, never removes it
        //      but burning of lines changes the field a lot
        CREATE_PADDED_FIELD(ff, true);

        eval_t best_e = eval_worst;
        int best_i;
        int best_shift;

        for (int i = 0; i < ns; i++) {
            seg d = drop_masks[i];
            if (d != 0) {
                seg occ_above = f[i - 1];  // save for easier restore
                seg occ_above_masked = occ_above & ~last_line_mask;
                seg occ = f[i];
                seg occ_below = f[i + 1];
                memcpy(ff, f, sizeof(f[0]) * ns);
                do {
                    //int drops = popcount(d);

                    int shift = __builtin_ctzll(d);
                    // alt: int shift = 63 - __builtin_clzll(d);

                    d &= ~(one << shift);
                    // alt: d = d & (d - 1);

                    seg added = piece << shift;
                    seg added_below = added >> (pw * esh);

                    seg new_occ = occ | added;

                    ff[i - 1] = occ_above_masked | first_line_shifted(new_occ);
                    ff[i] = new_occ;
                    ff[i + 1] = occ_below | added_below;
                    burn_lines(ff, pid, i, shift);

                    eval_t e;
                    if constexpr (normal_call) {
                        // try to place the next peace
                        e = spawn_and_drop_piece<false>(ff, next_pid, 0, 0, eval_fn);
                    } else { // look-ahead call
                        e = eval(eval_fn, ff);
                    }
                    if (e >= best_e) {
                        best_e = e;
                        best_i = i;
                        best_shift = shift;
                    }

                    //occ -= added;
                    //occ_below -= added_below;

                    //f[i - 1] = occ_above;
                    //f[i] = occ;
                    //f[i + 1] = occ_below;

                } while (d != 0);
            }
        }

        if constexpr (normal_call) {
            // drop the current piece, chaning the playfield
            // and contine until top-out
            int i = best_i;
            int shift = best_shift;

            seg occ_above = f[i - 1];
            seg occ_above_masked = occ_above & ~last_line_mask;
            seg occ = f[i];

            seg added = piece << shift;
            seg added_below = added >> (pw * esh);

            seg new_occ = occ | added;

            f[i - 1] = occ_above_masked | first_line_shifted(new_occ);
            f[i] = new_occ;
            f[i + 1] |= added_below;
            int burnt = burn_lines(f, pid, i, shift);

            //draw(f, i, added, i + 1, added_below);

            u64 new_stats = succ_stats(stats, burnt);
            return spawn_and_drop_piece<true>(f, next_pid, rng(next_pid), new_stats, eval_fn);
        } else { // look-ahead call
            return best_e;
        }
    } else { // no room for a spawn
        if constexpr (normal_call) { // normal call
            return stats;
        } else { // look-ahead call
            return eval_worst;
        }
    }
}

// returns stats
u64 game(u32 seed, eval_func_t eval_fn) {
    rnd_seed = seed;
    CREATE_PADDED_FIELD(f, true);
    init_field(f);
    pid_t pid = rng(-1);
    pid_t next_pid = rng(pid);
    u64 stats = spawn_and_drop_piece<true>(f, pid, next_pid, 0, eval_fn);
    return stats;
}

//#include "extra/asm.cc"
//#include "extra/disasm.cc"

} // end of anonymous namespace

int main() {
    srand(0);
    //srand(time(0));
    //generate_random_population();

    auto begin = chrono::steady_clock::now();
    //for (int i = 0; i < 300000; i++) {
    for (int i = 0; i < 30000; i++) {
    //for (int i = 0; i < pop_size; i++) {
        // using genetic programming - wip
        /*prog_t p = pop[i].p;
        compiled_prog_t cp = compile_program(p);
        eval_func_t ef = cp.eval_func;
        for (int j = 0; j < 3; j++) {
            u64 stats = game(30347893 * i, ef);
            u64 pieces = stats_pieces(stats);
            u64 lines = stats_lines(stats);
            cout << pieces << " pieces, " << lines << " lines burnt" << endl;
        }
        free_program(cp);*/
        // just using random eval
        u64 stats = game(30347893 * i, rand_eval);
        u64 pieces = stats_pieces(stats);
        u64 lines = stats_lines(stats);
        //cout << pieces << " pieces, " << lines << " lines burnt" << endl;
        //cout << endl;
    }
    auto end = chrono::steady_clock::now();

    auto secs = chrono::duration_cast<chrono::nanoseconds>(end - begin).count() / 1000.0 / 1000.0 / 1000.0;
    cout << evals << " evals" << endl;
    printf("%.2f mn/s  \n", evals / secs / 1000.0 / 1000.0);

    return 0;
}



