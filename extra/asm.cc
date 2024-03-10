//#define ASMJIT_EMBED
//#define ASMJIT_STATIC
//#define ASMJIT_NO_ABI_NAMESPACE
//#define ASMJIT_BUILD_RELEASE
#include "asmjit/src/asmjit/asmjit.h"

using namespace asmjit;

JitRuntime rt;
x86::Assembler as;

/*eval_func_t compile_eval_func(int test) {
    CodeHolder asch;
    asch.init(rt.environment(), rt.cpuFeatures());
    asch.attach(&as);
    as.mov(x86::rax, test);
    as.ret();
    eval_func_t fn;
    Error err = rt.add(&fn, &asch);
    if (err) fail(DebugUtils::errorAsString(err));
    asch.detach(&as);
    return fn;
}*/

void free_func(eval_func_t fn) {
    rt.release(fn);
}

// operands: 4 + 4 == 8
// ops: 5 + 1 + 4 + 1 == 10
//      binary op
//      intermediate not
//      post-op
//      post-not
// imm: 64

struct  __attribute__((packed)) op_t {
    u64 a:4;
    u64 b:4;
    u64 bop:4;
    u64 mid_not:1;
    u64 shift_dir:1;
    u64 shift:6;
    u64 post_op:3;
    u64 post_not:1;
    u64 imm;
};

x86::Gpq compile_op(auto& as, op_t op) {
    using namespace x86;
    static Gpq reg_tab[] = {
        rax, rbx, rcx, rdx,
        /*rsp*/rdi, rbp, rdi, rsi,
        r8, r9, r10, r11,
        r12, r13, r14, r15
    };
    auto a = reg_tab[op.a];
    auto b = reg_tab[op.b];
    auto bop = op.bop;
    auto mid_not = op.mid_not;
    auto post_op = op.post_op;
    auto post_not = op.post_not;
    auto imm = op.imm;
    // preserve rsp
    /*if (a == rsp) {
        a = r13;
    }
    if (b == rsp) {
        b = r14;
    }*/
    auto t = r15;
    assume(bop >= 0 && bop <= 15);
    assume(mid_not >= 0 && mid_not <= 1);
    assume(post_op >= 0 && post_op <= 7);
    assume(post_not >= 0 && post_not <= 1);
    switch (bop) {
        case 0b0000:  // a <- i  (load const)
            as.mov(a, imm);
            break;
        case 0b0001:  // a & b
            as.and_(a, b);
            break;
        case 0b0010:  // swap
            as.xchg(a, b);
            break;
        case 0b0011:  // a & ~b
            as.mov(t, b);
            as.not_(t);
            as.and_(a, t);
            break;
        case 0b0100:  // a * b
            as.imul(a, b);
            break;
        case 0b0101:  // a << b
            if (a == rcx && b == rcx) {
                as.shl(a, cl);
            } else if (a == rcx && b != rcx) {
                as.xchg(a, b);
                as.shl(b, cl);
                as.xchg(a, b);
            } else if (a != rcx && b == rcx) {
                as.shl(a, cl);
            } else /*if (a != rcx && b != rcx)*/ {
                if (a != b) {
                    as.xchg(rcx, b);
                    as.shl(a, cl);
                    as.xchg(rcx, b);
                } else {
                    as.xchg(rcx, b);
                    as.shl(rcx, cl);
                    as.xchg(rcx, b);
                }
            }
            break;
        case 0b0110:
            /* none */
            break;
        case 0b0111:  // min
            as.cmp(a, b);
            as.cmovg(a, b);
            break;
        case 0b1000:  // a >> b
            if (a == rcx && b == rcx) {
                as.shr(a, cl);
            } else if (a == rcx && b != rcx) {
                as.xchg(a, b);
                as.shr(b, cl);
                as.xchg(a, b);
            } else if (a != rcx && b == rcx) {
                as.shr(a, cl);
            } else /*if (a != rcx && b != rcx)*/ {
                if (a != b) {
                    as.xchg(rcx, b);
                    as.shr(a, cl);
                    as.xchg(rcx, b);
                } else {
                    as.xchg(rcx, b);
                    as.shr(rcx, cl);
                    as.xchg(rcx, b);
                }
            }
            break;
        case 0b1001: // |a - b| == ((a - b) + mask) ^ mask;
            as.sub(a, b);
            as.mov(t, a);
            as.sar(t, 63);
            as.add(a, t);
            as.xor_(a, t);
            break;
        case 0b1010:  // a - b
            as.sub(a, b);
            break;
        case 0b1011:  // a <- b (copy)
            as.mov(a, b);
            break;
        case 0b1100:  // a | b
            as.or_(a, b);
            break;
        case 0b1101:  // a + b
            as.add(a, b);
            break;
        case 0b1110:  // a ^ b
            as.xor_(a, b);
            break;
        case 0b1111:    // max
            as.cmp(a, b);
            as.cmovl(a, b);
            break;
    }
    if (mid_not) {
        as.not_(a);
    }
    switch (post_op) {
        case 0b000:
            /* none / copy */
            break;
        case 0b001:
            as.popcnt(a, a);
            break;
        case 0b010:  // lsb1 == ctz, after not == cto
            as.mov(t, 64);
            as.bsf(a, a);
            as.cmovz(a, t);
            break;
        case 0b011:  // 63 - msb1 == clz, after not == clo
            as.mov(t, -1);
            as.bsr(a, a);
            as.cmovz(a, t);
            // main case:
            // 63 - msb1pos(0..63)
            // zero case:
            // 63 - n == 64
            // 63 - 64 == n  (n == -1)
            as.mov(t, 63);
            as.sub(t, a);
            as.mov(a, t);
            break;
        case 0b100:  // a & ~REP(i)
            as.mov(t, ~REP_MASK(imm));
            as.and_(a, t);
            break;
        case 0b101:  // a & REP(i)
            as.mov(t, REP_MASK(imm));
            as.and_(a, t);
            break;
        case 0b110:  // a & i
            as.mov(t, imm);
            as.and_(a, t);
            break;
        case 0b111:  // a & ~i
            as.mov(t, ~imm);
            as.and_(a, t);
            break;
    }
    if (post_not) {
        as.not_(a);
    }
    // shift
    if (a == rcx) {
        as.xchg(rcx, rax);
        as.push(rcx);
        as.mov(rcx, op.shift);
        if (op.shift_dir == 0) {
            as.shl(rax, cl);
        } else {
            as.shr(rax, cl);
        }
        as.pop(rcx);
        as.xchg(rcx, rax);
    } else {
        as.push(rcx);
        as.mov(rcx, op.shift);
        if (op.shift_dir == 0) {
            as.shl(rax, cl);
        } else {
            as.shr(rax, cl);
        }
        as.pop(rcx);
    }
    return a;
}

constexpr int prog_ops = 32;

struct  __attribute__((packed)) prog_t {
    op_t map_ops[prog_ops]; // called per segment
    op_t reduce_ops[prog_ops];
};

using compiled_subprog_t = eval_func_t;

struct compiled_prog_t {
    eval_func_t map;
    eval_func_t reduce;
    eval_func_t eval_func;
};

eval_func_t compile_subprogram(const op_t ops[prog_ops]) {
    CodeHolder asch;
    asch.init(rt.environment(), rt.cpuFeatures());
    asch.attach(&as);

    using namespace x86;

    // linux x86-64 calling convention arguments:
    // rdi rsi rdx rcx r8 r9  (6 registers)

    // save rbx rsp rbp r12 r13 r14 r15
    as.push(rbx); as.push(rbp);
    as.push(r12); as.push(r13); as.push(r14); as.push(r15);

    // fill registers with some useful constants
    as.mov(rax, line_mask);
    as.mov(rbx, seg_mask);
    as.mov(r8, piece_info[0]);
    as.mov(r9, piece_info[1]);
    as.mov(r10, piece_info[2]);
    as.mov(r11, piece_info[3]);
    as.mov(r12, piece_info[4]);
    as.mov(r13, piece_info[5]);
    as.mov(r14, piece_info[6]);
    as.mov(r15, 1);

    auto last_reg = rax;
    for (int i = 0; i < prog_ops; i++) {
        const op_t& op = ops[i];
        last_reg = compile_op(as, op);
    }
    as.mov(rax, last_reg);

    // restore rbx rsp rbp r12 r13 r14 r15
    as.pop(r15); as.pop(r14); as.pop(r13); as.pop(r12);
    as.pop(rbp); as.pop(rbx);
    as.ret();

    //asch.flatten();

    eval_func_t fn;
    Error err = rt.add(&fn, &asch);
    if (err) fail(DebugUtils::errorAsString(err));

    //dump_disasm(asch);

    asch.detach(&as);
    //cout << fn(1, 2, 3, 4) << endl;
    //cout << bitset<64>(fn(1, 2, 3, 4)) << endl;
    return fn;
}

eval_func_t compile_eval_func(eval_func_t map_func, eval_func_t reduce_func) {
    CodeHolder asch;
    asch.init(rt.environment(), rt.cpuFeatures());
    asch.attach(&as);
    using namespace x86;
    // rdi - a, rsi - b, rdx - c, rcx - d
    as.push(r10); as.push(r11); as.push(r12); as.push(r13);

    as.push(rcx);
    as.push(rdx);
    as.push(rsi);

    //as.mov(rdi, rdi);
    as.mov(rsi, rdi);
    as.mov(rdx, rdi);
    as.mov(rcx, rdi);
    as.call(map_func);
    as.mov(r10, rax);

    as.pop(rdi);
    as.mov(rsi, rdi);
    as.mov(rdx, rdi);
    as.mov(rcx, rdi);
    as.call(map_func);
    as.mov(r11, rax);

    as.pop(rdi);
    as.mov(rsi, rdi);
    as.mov(rdx, rdi);
    as.mov(rcx, rdi);
    as.call(map_func);
    as.mov(r12, rax);

    as.pop(rdi);
    as.mov(rsi, rdi);
    as.mov(rdx, rdi);
    as.mov(rcx, rdi);
    as.call(map_func);
    as.mov(r13, rax);

    as.mov(rdi, r10);
    as.mov(rsi, r11);
    as.mov(rdx, r12);
    as.mov(rcx, r13);
    as.call(reduce_func);

    as.pop(r13); as.pop(r12); as.pop(r11); as.pop(r10);
    as.ret();

    eval_func_t fn;
    Error err = rt.add(&fn, &asch);
    if (err) fail(DebugUtils::errorAsString(err));
    //dump_disasm(asch);
    asch.detach(&as);

    return fn;
}

compiled_prog_t compile_program(const prog_t& prog) {
    compiled_prog_t cp;
    cp.map = compile_subprogram(prog.map_ops);
    cp.reduce = compile_subprogram(prog.reduce_ops);
    cp.eval_func = compile_eval_func(
        cp.map, cp.reduce
    );
    return cp;
}

op_t generate_random_op() {
    op_t op;
    u8* p = (u8*)&op;
    for (int i = 0; i < sizeof(op_t); i++) {
        *p++ = (u8)rand();
    }
    return op;
}

// RAND_MAX should be more or less 32-bit (or bigger)
prog_t generate_random_program() {
    prog_t p;
    for (int i = 0; i < prog_ops; i++) {
        p.map_ops[i] = generate_random_op();
    }
    for (int i = 0; i < prog_ops; i++) {
        p.reduce_ops[i] = generate_random_op();
    }
    return p;
}

void free_program(compiled_prog_t cp) {
    free_func(cp.map);
    free_func(cp.reduce);
    free_func(cp.eval_func);
}

} // end of anonymous namespace

/*int main() {
    srand(0);
    cout << sizeof(op_t) << " bytes" << endl;
    prog_t p = generate_random_program();
    compiled_prog_t cp = compile_program(p);
    cout << cp.eval_func(0, 0, -33, -44) << endl;
    return 0;
}*/

constexpr int pop_size = 3;

struct {
    prog_t p;
    int pieces;
    int lines;
} pop[pop_size];

void generate_random_population() {
    for (int i = 0; i < pop_size; i++) {
        pop[i].p = generate_random_program();
        pop[i].pieces = 0;
        pop[i].lines = 0;
    }
}
