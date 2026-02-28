import math
import sys

def print_table(name, table, type_name="faac_real", is_float=True, is_decl=False):
    if is_decl:
        print(f"extern const {type_name} {name}[{len(table)}];")
    else:
        print(f"const {type_name} {name}[] = {{")
        for i in range(0, len(table), 4):
            if is_float:
                line = "    " + ", ".join(f"FR({x:.18e})" for x in table[i:i+4])
            else:
                line = "    " + ", ".join(str(x) for x in table[i:i+4])
            if i + 4 < len(table):
                line += ","
            print(line)
        print("};")

def generate_pow10_table():
    sfstep = 1.0 / math.log10(math.sqrt(math.sqrt(2.0)))
    return [math.pow(10, (i - 128) / sfstep) for i in range(256)]

def generate_fft_tables(logm):
    size = 1 << logm
    costbl = [math.cos(2.0 * math.pi * i / size) for i in range(size // 2)]
    negsintbl = [-math.sin(2.0 * math.pi * i / size) for i in range(size // 2)]
    return costbl, negsintbl

def generate_reorder_table(logm):
    size = 1 << logm
    table = []
    for i in range(size):
        reversed_val = 0
        tmp = i
        for _ in range(logm):
            reversed_val = (reversed_val << 1) | (tmp & 1)
            tmp >>= 1
        table.append(reversed_val)
    return table

def generate_sin_window(length):
    return [math.sin((math.pi / (2 * length)) * (i + 0.5)) for i in range(length)]

def izero(x):
    epsilon = 1e-41
    sum_val = u = n = 1.0
    halfx = x / 2.0
    while True:
        temp = halfx / n
        n += 1
        temp *= temp
        u *= temp
        sum_val += u
        if u < epsilon * sum_val: break
    return sum_val

def generate_kbd_window(alpha, length):
    alpha_pi = alpha * math.pi
    ibeta = 1.0 / izero(alpha_pi)
    win = []
    for i in range(length // 2):
        tmp = 4.0 * i / length - 1.0
        win.append(izero(alpha_pi * math.sqrt(1.0 - tmp * tmp)) * ibeta)
    sum_val = sum(win)
    inv_sum = 1.0 / sum_val
    tmp = 0.0
    kbd_win = []
    for i in range(length // 2):
        tmp += win[i]
        kbd_win.append(math.sqrt(tmp * inv_sum))
    return kbd_win

def generate_twiddles(n):
    twiddles = []
    freq = 2.0 * math.pi / n
    cfreq = math.cos(freq); sfreq = math.sin(freq)
    c = math.cos(freq * 0.125); s = math.sin(freq * 0.125)
    for i in range(n >> 2):
        twiddles.extend([c, s])
        cold = c
        c = c * cfreq - s * sfreq
        s = s * cfreq + cold * sfreq
    return twiddles

mode = sys.argv[1] if len(sys.argv) > 1 else "header"

if mode == "header":
    print("#ifndef BUILTIN_TABLES_H\n#define BUILTIN_TABLES_H\n\n#include \"faac_real.h\"\n")
    is_decl = True
else:
    print("#include \"builtin_tables.h\"\n")
    is_decl = False

if mode == "source":
    print("#ifdef FAAC_PRECISION_SINGLE\n#define FR(x) x##f\n#else\n#define FR(x) x\n#endif\n")

print_table("pow10_sfstep_table", generate_pow10_table(), is_decl=is_decl)

print("#ifndef DRM")
for logm in [6, 9]:
    c, s = generate_fft_tables(logm)
    print_table(f"fft_costbl_{logm}", c, is_decl=is_decl)
    print_table(f"fft_negsintbl_{logm}", s, is_decl=is_decl)
    print_table(f"fft_reorder_{logm}", generate_reorder_table(logm), "unsigned short", False, is_decl=is_decl)
print("#endif")

print_table("sin_window_long_table", generate_sin_window(1024), is_decl=is_decl)
print_table("sin_window_short_table", generate_sin_window(128), is_decl=is_decl)
print_table("kbd_window_long_table", generate_kbd_window(4, 2048), is_decl=is_decl)
print_table("kbd_window_short_table", generate_kbd_window(6, 256), is_decl=is_decl)
print_table("mdct_twiddles_long_table", generate_twiddles(2048), is_decl=is_decl)
print_table("mdct_twiddles_short_table", generate_twiddles(256), is_decl=is_decl)

if mode == "header":
    print("#endif")
