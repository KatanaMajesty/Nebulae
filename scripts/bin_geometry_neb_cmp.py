import re
import os

def parse_offset_size_lines(text):
    entries = []
    pattern = re.compile(
        r"mesh (\d+), geometry (\d+): offsets = \[([\d, ]+)\], sizes = \[([\d, ]+)\]"
    )
    for line in text.splitlines():
        match = pattern.search(line)
        if match:
            mesh_index = int(match.group(1))
            geometry_index = int(match.group(2))
            offsets = list(map(int, match.group(3).split(",")))
            sizes = list(map(int, match.group(4).split(",")))
            entries.append((mesh_index, geometry_index, offsets, sizes))
    return entries

def read_file_range(filepath, offset, size):
    with open(filepath, 'rb') as f:
        f.seek(offset)
        return f.read(size)

def compare_binary(expected: bytes, actual: bytes):
    if expected == actual:
        return True
    else:
        for i in range(min(len(expected), len(actual))):
            if expected[i] != actual[i]:
                print(f"Mismatch at byte {i}: {expected[i]:02X} != {actual[i]:02X}")
                break
        if len(expected) != len(actual):
            print(f"Length mismatch: {len(expected)} != {len(actual)}")
        return False

def compare_all_ranges(offset_size_txt_path, gltf_buffer_path, gltf_tangent_buffer_path):
    with open(offset_size_txt_path, 'r') as f:
        text = f.read()

    entries = parse_offset_size_lines(text)
    print(f"Loaded {len(entries)} entries")

    for mesh_index, geometry_index, offsets, sizes in entries:
        pos_file = f"cpu_attr_mesh_{mesh_index}_geometry_{geometry_index}_positions.bin"
        norm_file = f"cpu_attr_mesh_{mesh_index}_geometry_{geometry_index}_normals.bin"
        uv_file = f"cpu_attr_mesh_{mesh_index}_geometry_{geometry_index}_tex_uvs.bin"
        tangent_file = f"cpu_attr_mesh_{mesh_index}_geometry_{geometry_index}_tangents.bin"

        for i, (file, offset, size) in enumerate(zip(
            [pos_file, norm_file, uv_file, tangent_file],
            offsets,
            sizes
        )):
            if not os.path.exists(file):
                print(f"Missing file: {file}")
                continue
            source = read_file_range(file, 0, size)
            gltf_source = read_file_range(
                gltf_tangent_buffer_path if i == 3 else gltf_buffer_path, offset, size
            )
            match = compare_binary(source, gltf_source)
            if not match:
                print(f"❌ Mismatch in mesh {mesh_index} geometry {geometry_index} attribute {i} ({file})")
            else:
                print(f"✅ Match in mesh {mesh_index} geometry {geometry_index} attribute {i} ({file})")

# Example usage
compare_all_ranges(
    offset_size_txt_path="D:/Development/WorkSpace/Nebulae/scripts/comparison_entries.txt",
    gltf_buffer_path="gltf_buffer_0.bin",
    gltf_tangent_buffer_path="gltf_tangent_buffer.bin"
)
