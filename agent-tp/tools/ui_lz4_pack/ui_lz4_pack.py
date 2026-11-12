import argparse
import pathlib
import re

import lz4.block


MAGIC = b"LZ4I"


def read_image_source(path):
    source = path.read_text(encoding="utf-8")

    array_match = re.search(
        r"uint8_t\s+(ui_img_[A-Za-z0-9_]+)_data\[\]\s*=\s*"
        r"\{(.*?)\};",
        source,
        re.DOTALL,
    )
    if not array_match:
        raise RuntimeError("没有找到图片数据数组")

    symbol = array_match.group(1)
    array_text = array_match.group(2)

    values = re.findall(r"0x([0-9A-Fa-f]{2})", array_text)
    raw_data = bytes(int(value, 16) for value in values)

    width_match = re.search(r"\.header\.w\s*=\s*(\d+)", source)
    height_match = re.search(r"\.header\.h\s*=\s*(\d+)", source)

    if not width_match or not height_match:
        raise RuntimeError("没有找到图片宽度或高度")

    width = int(width_match.group(1))
    height = int(height_match.group(1))

    if "LV_IMG_CF_TRUE_COLOR_ALPHA" not in source:
        raise RuntimeError(
            "当前脚本只处理LV_IMG_CF_TRUE_COLOR_ALPHA图片"
        )

    expected_size = width * height * 3

    if len(raw_data) != expected_size:
        raise RuntimeError(
            f"数组大小不匹配：实际={len(raw_data)}，"
            f"预计={expected_size}"
        )

    return symbol, width, height, raw_data


def format_bytes(data):
    lines = []

    for offset in range(0, len(data), 12):
        chunk = data[offset:offset + 12]
        values = ",".join(f"0x{value:02X}" for value in chunk)
        lines.append(f"    {values},")

    return "\n".join(lines)


def write_compressed_source(
    output_path,
    symbol,
    width,
    height,
    raw_data,
    compressed_data,
):
    output_symbol = f"{symbol}_lz4"
    packed_data = MAGIC + compressed_data

    content = f"""\
#include "../ui.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

const LV_ATTRIBUTE_MEM_ALIGN uint8_t
    {output_symbol}_data[] = {{
{format_bytes(packed_data)}
}};

const lv_img_dsc_t {output_symbol} = {{
    .header.always_zero = 0,
    .header.w = {width},
    .header.h = {height},
    .data_size = sizeof({output_symbol}_data),
    .header.cf = LV_IMG_CF_RAW_ALPHA,
    .data = {output_symbol}_data
}};
"""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(content, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(
        description="将LVGL原始图片数组压缩成LZ4 HC -9 Block"
    )
    parser.add_argument(
        "input",
        type=pathlib.Path,
        help="SquareLine生成的原始图片.c文件",
    )
    parser.add_argument(
        "output",
        type=pathlib.Path,
        help="输出的LZ4图片.c文件",
    )
    parser.add_argument(
        "--symbol-prefix",
        default="",
        help="Prefix inserted after ui_img_ to avoid symbol collisions",
    )
    args = parser.parse_args()

    symbol, width, height, raw_data = read_image_source(args.input)
    if args.symbol_prefix:
        symbol = symbol.replace("ui_img_", f"ui_img_{args.symbol_prefix}", 1)

    compressed_data = lz4.block.compress(
        raw_data,
        mode="high_compression",
        compression=9,
        store_size=False,
    )

    decoded_data = lz4.block.decompress(
        compressed_data,
        uncompressed_size=len(raw_data),
    )

    if decoded_data != raw_data:
        raise RuntimeError("电脑端LZ4解压校验失败")

    saved_size = len(raw_data) - len(compressed_data)
    ratio = len(compressed_data) / len(raw_data) * 100

    print(f"图片名称：{symbol}")
    print(f"图片尺寸：{width} x {height}")
    print(f"原始大小：{len(raw_data):,} 字节")
    print(f"压缩大小：{len(compressed_data):,} 字节")
    print(f"压缩占比：{ratio:.2f}%")
    print(f"节省空间：{saved_size:,} 字节")
    print("解压校验：通过")

    if len(compressed_data) >= len(raw_data):
        print("压缩后没有变小，本图片建议继续使用RAW格式")
        return

    write_compressed_source(
        args.output,
        symbol,
        width,
        height,
        raw_data,
        compressed_data,
    )

    print(f"输出文件：{args.output}")


if __name__ == "__main__":
    main()
