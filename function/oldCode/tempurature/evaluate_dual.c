/*
 * 双方案评估器: 读 CSV -> 每个样本同时用 predict_full 和 predict_ntc 预测,
 * 对比真实 liquid_temp_c, 输出两方案各自的 RMSE/MAE/最大误差/容差命中率/时段误差。
 *
 * 方案A predict_full: 输入 power/time/ntc/ambient 四路历史滑窗(各 FULL_WINDOW)。
 * 方案B predict_ntc : 输入过去 NTC_INPUT_DIM 条 ntc 历史。
 * time_min 支持 'M:SS'/'H:MM:SS'/'5'/'5.5'; 缺失 liquid_temp_c 的行跳过。
 *
 * 用法: evaluate_dual.exe [data.csv]   (默认 ../data/260811/all.csv)
 * 编译: gcc evaluate_dual.c -o evaluate_dual.exe -O2 -std=c11 -lm
 */
#include "dual_inference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_COLS 16
#define MAX_LINE 1024

typedef struct {
    int cycle_id;
    float time_min, power_w, ntc_temp_c, liquid_temp_c, ambient_temp_c;
} Row;

/* 按逗号分割, 保留空字段 (strtok 会吞掉空字段, 不适用于含空值的 CSV) */
static int split_line(char *line, char *fields[], int max) {
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
    int n = 0; char *p = line;
    while (n < max) {
        char *c = strchr(p, ',');
        if (c) { *c = '\0'; fields[n++] = p; p = c + 1; }
        else { fields[n++] = p; break; }
    }
    return n;
}

static const char *fld(char *f[], int nf, int i) {
    return (i >= 0 && i < nf && f[i]) ? f[i] : "";
}

/* 解析时间: 'M:SS'/'H:MM:SS'/'5'/'5.5' -> 分钟(浮点) */
static float parse_time(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    const char *c1 = strchr(s, ':');
    if (!c1) return (float)atof(s);
    float a = (float)atof(s);
    const char *p = c1 + 1;
    const char *c2 = strchr(p, ':');
    if (c2) { float b = (float)atof(p), c = (float)atof(c2 + 1); return a * 60 + b + c / 60; }
    float b = (float)atof(p);
    return a + b / 60;
}

static int cmp_row(const void *a, const void *b) {
    const Row *ra = a, *rb = b;
    if (ra->cycle_id != rb->cycle_id) return ra->cycle_id < rb->cycle_id ? -1 : 1;
    if (ra->time_min < rb->time_min) return -1;
    if (ra->time_min > rb->time_min) return 1;
    return 0;
}

/* 误差统计 */
typedef struct {
    double sum_sq, sum_abs; float max_abs; int n, c05, c10, c20;
    int bin_n[128]; double bin_sq[128], bin_ab[128]; float bin_mx[128];
} Stats;
static void st_init(Stats *s) { memset(s, 0, sizeof(*s)); }
static void st_add(Stats *s, float pred, float actual, float time_min) {
    float err = pred - actual, ae = fabsf(err);
    s->sum_sq += (double)err * err; s->sum_abs += ae; s->n++;
    if (ae > s->max_abs) s->max_abs = ae;
    if (ae <= 0.5f) s->c05++;
    if (ae <= 1.0f) s->c10++;
    if (ae <= 2.0f) s->c20++;
    int b = (int)(time_min / 10.0f);
    if (b >= 0 && b < 128) {
        s->bin_n[b]++; s->bin_sq[b] += (double)err * err; s->bin_ab[b] += ae;
        if (ae > s->bin_mx[b]) s->bin_mx[b] = ae;
    }
}
static void st_print(Stats *s, const char *name) {
    printf("  %-28s RMSE=%.3f MAE=%.3f max|err|=%.3f | +/-0.5C %.1f%% +/-1.0C %.1f%% +/-2.0C %.1f%%\n",
        name, sqrt(s->sum_sq / s->n), s->sum_abs / s->n, s->max_abs,
        100.0 * s->c05 / s->n, 100.0 * s->c10 / s->n, 100.0 * s->c20 / s->n);
    printf("    per-time-bin:\n");
    int wb = -1; double wr = -1.0;
    for (int b = 0; b < 128; b++) if (s->bin_n[b]) {
        double r = sqrt(s->bin_sq[b] / s->bin_n[b]);
        printf("      %2d-%-3dmin n=%-3d RMSE=%.3f MAE=%.3f max=%.3f\n",
            b * 10, (b + 1) * 10, s->bin_n[b], r, s->bin_ab[b] / s->bin_n[b], s->bin_mx[b]);
        if (r > wr) { wr = r; wb = b; }
    }
    if (wb >= 0) printf("    >> worst bin %d-%dmin RMSE=%.3f\n", wb * 10, (wb + 1) * 10, wr);
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "../data/260811/all.csv";
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }

    /* 跳过 UTF-8 BOM */
    unsigned char bom[3];
    if (fread(bom, 1, 3, f) != 3 || !(bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF))
        rewind(f);

    char line[MAX_LINE]; char *fields[MAX_COLS];
    if (!fgets(line, sizeof(line), f)) { fclose(f); fprintf(stderr, "empty file\n"); return 1; }
    int nf = split_line(line, fields, MAX_COLS);
    int i_cyc = -1, i_t = -1, i_p = -1, i_n = -1, i_l = -1, i_a = -1;
    for (int i = 0; i < nf; i++) {
        if      (strcmp(fields[i], "cycle_id") == 0)      i_cyc = i;
        else if (strcmp(fields[i], "time_min") == 0)       i_t = i;
        else if (strcmp(fields[i], "power_w") == 0)        i_p = i;
        else if (strcmp(fields[i], "ntc_temp_c") == 0)     i_n = i;
        else if (strcmp(fields[i], "liquid_temp_c") == 0) i_l = i;
        else if (strcmp(fields[i], "ambient_temp_c") == 0)i_a = i;
    }
    if (i_cyc < 0 || i_t < 0 || i_p < 0 || i_n < 0 || i_l < 0) {
        fprintf(stderr, "CSV missing required columns\n"); fclose(f); return 1;
    }

    int cap = 1024, nrows = 0, skipped = 0;
    Row *rows = (Row *)malloc(cap * sizeof(Row));
    while (fgets(line, sizeof(line), f)) {
        nf = split_line(line, fields, MAX_COLS);
        if (nf <= 0) continue;
        const char *ls = fld(fields, nf, i_l);
        if (ls[0] == '\0') { skipped++; continue; }
        if (nrows >= cap) { cap *= 2; rows = (Row *)realloc(rows, cap * sizeof(Row)); }
        Row r;
        r.cycle_id      = atoi(fld(fields, nf, i_cyc));
        r.time_min      = parse_time(fld(fields, nf, i_t));
        r.power_w       = (float)atof(fld(fields, nf, i_p));
        r.ntc_temp_c    = (float)atof(fld(fields, nf, i_n));
        r.liquid_temp_c = (float)atof(ls);
        r.ambient_temp_c = (i_a >= 0) ? (float)atof(fld(fields, nf, i_a)) : 0.0f;
        rows[nrows++] = r;
    }
    fclose(f);
    if (nrows == 0) { fprintf(stderr, "no usable rows\n"); free(rows); return 1; }
    qsort(rows, nrows, sizeof(Row), cmp_row);

    Stats sf, sn; st_init(&sf); st_init(&sn);
    int cycle_start = 0;
    printf("data: %s | samples: %d (skipped %d) | full_input=%d ntc_window=%d\n",
        path, nrows, skipped, FULL_INPUT_DIM, NTC_INPUT_DIM);
    printf("%5s %6s %9s %9s %9s %9s %9s\n", "cyc", "time", "predF", "predN", "actual", "errF", "errN");
    int shown = 0, cap_show = 100;
    for (int i = 0; i < nrows; i++) {
        if (i == 0 || rows[i].cycle_id != rows[i-1].cycle_id) cycle_start = i;
        int o = i - cycle_start;
        /* 四路滑窗 (不足回填起始值); FULL_WINDOW==NTC_INPUT_DIM 时 ntc_win 共用 */
        float power_win[FULL_WINDOW], time_win[FULL_WINDOW], ntc_win[FULL_WINDOW], ambient_win[FULL_WINDOW];
        for (int k = 0; k < FULL_WINDOW; k++) {
            int src = o - FULL_WINDOW + 1 + k;
            int idx = (src < 0) ? cycle_start : (cycle_start + src);
            power_win[k]   = rows[idx].power_w;
            time_win[k]    = rows[idx].time_min;
            ntc_win[k]     = rows[idx].ntc_temp_c;
            ambient_win[k] = rows[idx].ambient_temp_c;
        }
        float pf = predict_full(power_win, time_win, ntc_win, ambient_win);
        float pn = predict_ntc(ntc_win);
        float actual = rows[i].liquid_temp_c;
        st_add(&sf, pf, actual, rows[i].time_min);
        st_add(&sn, pn, actual, rows[i].time_min);
        if (shown < cap_show) {
            printf("%5d %6.1f %9.2f %9.2f %9.2f %9.2f %9.2f\n",
                rows[i].cycle_id, rows[i].time_min, pf, pn, actual, pf - actual, pn - actual);
            shown++;
        }
    }
    printf("\n== Summary ==\n");
    st_print(&sf, "FULL (4 hist windows)");
    st_print(&sn, "NTC  (10 ntc history)");
    free(rows);
    return 0;
}
