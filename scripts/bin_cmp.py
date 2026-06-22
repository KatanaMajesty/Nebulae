import argparse

def compare_binary_ranges(file1_path, offset1, file2_path, offset2, size):
    """
    Compare binary data between two files from specified offsets and for a given size.

    :param file1_path: Path to the first binary file.
    :param offset1: Offset in bytes to start reading from the first file.
    :param file2_path: Path to the second binary file.
    :param offset2: Offset in bytes to start reading from the second file.
    :param size: Number of bytes to compare.
    :return: True if the specified ranges match exactly, False otherwise.
    """
    with open(file1_path, 'rb') as f1, open(file2_path, 'rb') as f2:
        f1.seek(offset1)
        f2.seek(offset2)

        data1 = f1.read(size)
        data2 = f2.read(size)

        if data1 == data2:
            print("Binary ranges are identical.")
            return True
        else:
            print("Binary ranges differ.")
            for i in range(min(len(data1), len(data2))):
                if data1[i] != data2[i]:
                    print(f"Difference at byte {i}: {data1[i]:02X} != {data2[i]:02X}")
            if len(data1) != len(data2):
                print("Warning: Ranges have different lengths.")
            return False

def parse_args():
    parser = argparse.ArgumentParser(description="Compare binary ranges from two files.")
    parser.add_argument("file1", help="Path to the first binary file.")
    parser.add_argument("offset1", type=lambda x: int(x, 0), help="Offset in first file (e.g., 0x100).")
    parser.add_argument("file2", help="Path to the second binary file.")
    parser.add_argument("offset2", type=lambda x: int(x, 0), help="Offset in second file (e.g., 0x200).")
    parser.add_argument("size", type=lambda x: int(x, 0), help="Number of bytes to compare.")
    return parser.parse_args()

if __name__ == "__main__":
    args = parse_args()
    compare_binary_ranges(args.file1, args.offset1, args.file2, args.offset2, args.size)
