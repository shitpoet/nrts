template <int n>
seg reverse_slow(seg x) {
    auto str = bitset<n>(x).to_string();
    std::reverse(str.begin(), str.end());
    return (seg)std::bitset<n>(str).to_ulong();
}

seg MAT(initializer_list<seg> list) {
    seg shift = 0;
    seg res = 0;
    for (seg el : list) {
        el = reverse_slow<10>(el);
        res |= (el << shift);
        shift += 10;
    }
    return res;
}

using bs = bitset<64>;

void _print(bs b) {
    for (int i = b.size() - 1; i >= 0; i--) {
        cout << (b[i] ? '1' : '.');
    }
    cout << endl;
}

void _print(string label, bs b) {
    cout << std::setw(14) << label << ' ';
    _print(b);
}

#define print(x) _print(#x, x)

void nl() { cout << endl; }

bs operator+(bs a, bs b) {
    return a.to_ulong() + b.to_ulong();
}

bs operator-(bs a, bs b) {
    return a.to_ulong() - b.to_ulong();
}

bs operator*(bs a, bs b) {
    return a.to_ulong() * b.to_ulong();
}

void _print_mat(string label, bs m, int h = 6) {
    int r = 0;
    cout << label; nl();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < 10; x++) {
            cout << (m[r++] ? '1' : '.');
        }
        nl();
    }
    nl();
}

seg to64(bs x) {
    return x.to_ulong();
}

#define print_mat(m) _print_mat(#m, m)
