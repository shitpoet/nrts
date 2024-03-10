string hl_string(string s) {
    const std::string red("\033[0;31m");
    const std::string green("\033[1;32m");
    const std::string yellow("\033[1;33m");
    const std::string cyan("\033[0;36m");
    const std::string magenta("\033[0;35m");
    const std::string reset("\033[0m");
    return cyan + s + reset;
}

// hl - highlight
void draw(const field f, const field hl = empty_field, bool check = false, bool segmented = false, bool draw_bottom = false) {
    //printf("draw check %d segmented %d\n", check, segmented);
    cout << endl;
    //if (check_field(f)) exit(1);
    int lines = 0;
    seg prev_line;
    for (int i = 0; i < ns; i++) {
        seg occ = f[i];
        seg seg_hl = hl[i];
        int m = segmented ? sh : esh;
        if (i == ns - 1 && draw_bottom) {
            m++;
        }
        for (uint y = 0; y < m; y++) {
            //printf("%d-%02d  ", i, lines);
            printf("%d-%d  ", i, y);
            seg line = (occ >> (y * pw)) & line_mask;
            seg hline = (seg_hl >> (y * pw)) & line_mask;

            if (check) {
                if (i > 0 && y == 0) {
                    if (line != prev_line) {
                        cout << endl;
                        printf("field desync:");
                        cout << bitset<pw>(line) << endl;
                        cout << bitset<pw>(prev_line) << endl;
                        exit(1);
                    }
                }
                if (y == esh - 1) {
                    prev_line = (occ >> ((sh - 1) * pw)) & line_mask;
                }
            }

            auto bs = bitset<pw>(line);
            auto hbs = bitset<pw>(hline);
            for (int x = 0; x < pw; x++) {
                auto ch =
                    hbs[x] && bs[x] ? hl_string("o") :
                    bs[x] ? "o" :
                    ".";
                cout << ch << ' ';
            }
            cout << endl;
            lines++;
        }
        if (segmented) cout << endl;
        //cout << endl;
        const int show_lines = 20;
        if (!segmented && lines == show_lines) break;
        if (segmented && lines == show_lines + ns) break;
    }
    //_print_mat("f", f[ns - 1], 7);
    //cout << endl;
}

void draw(field f, int hl_seg_idx, seg seg_hl) {
    field hl;
    init_field(hl, false);
    hl[hl_seg_idx] = seg_hl;
    draw(f, hl);
}

void draw(field f, int hl_1, seg hl_seg_1, int hl_2, seg hl_seg_2) {
    field hl;
    init_field(hl, false);
    hl[hl_1] = hl_seg_1;
    if (hl_2 < ns) {
        hl[hl_2] = hl_seg_2;
    }
    draw(f, hl);
}

bool check_field(const field f) {
    int lines = 0;
    seg prev_line;
    for (int i = 0; i < ns; i++) {
        seg occ = f[i];
        for (uint y = 0; y < esh; y++) {
            //printf("%d-%02d  ", i, lines);
            seg line = (occ >> (y * pw)) & line_mask;
            if (i > 0 && y == 0) {
                if (line != prev_line) {
                    cout << endl;
                    printf("field desync:");
                    printf("at %d-%d\n", i, y);
                    cout << bitset<pw>(line) << endl;
                    cout << bitset<pw>(prev_line) << endl;

                    draw(f, f, false, true);

                    exit(1);
                    return false;
                }
            }
            if (y == esh - 1) {
                prev_line = (occ >> ((sh - 1) * pw)) & line_mask;
            }
            auto bs = bitset<pw>(line);
            lines++;
        }
        if (lines == 20) break;
    }
    return true;
}
