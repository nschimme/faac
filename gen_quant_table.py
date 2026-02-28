import math

def generate_quant_table():
    # Use the more precise version of sfstep
    sfstep = 1.0 / math.log10(math.sqrt(math.sqrt(2.0)))

    with open("libfaac/quant_tables.h", "w") as f:
        f.write("/* Pre-calculated quantization scale factor table */\n")
        f.write("#ifndef QUANT_TABLES_H\n")
        f.write("#define QUANT_TABLES_H\n\n")
        f.write("#include \"faac_real.h\"\n\n")
        f.write("static const faac_real pow10_sfstep[256] = {\n")

        for i in range(256):
            sfac = i - 128
            val = math.pow(10.0, sfac / sfstep)
            # Use enough precision for double, but it will be cast to faac_real
            f.write(f"    (faac_real){val:.20e},\n")

        f.write("};\n\n")
        f.write("#endif\n")

if __name__ == "__main__":
    generate_quant_table()
