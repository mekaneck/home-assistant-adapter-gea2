import json
from pathlib import Path
try:
    from SCons.Script import Import  # type: ignore
    Import("env")
    PIO_MODE = True
except ImportError:
    env = None
    PIO_MODE = False
    print("⚠️ Running outside PlatformIO build context.")


def generate_header():
    # Path to project root
    project_root = Path(__file__).resolve().parent.parent

    # full path to the ERD json file
    file_path = project_root / ".pio" / "libdeps" / "xiao_c3" / "public-appliance-api-documentation" / "appliance_api_erd_definitions.json"
    output_dir = project_root / "include"
    output_file = output_dir / "gea2_addresses.h"

    try:
        # Load the JSON file
        with open(file_path, "r", encoding="utf-8") as f:
            data = json.load(f)

        # Print top-level keys
        print("Top-level keys in appliance_api_erd_definitions.json:")
        addresses = [item["id"] for item in data["erds"]]
        print(addresses)

        with open(output_file, "w") as f:
            f.write("#ifndef ADDRESSES_H\n")
            f.write("#define ADDRESSES_H\n\n")
            f.write("#include <cstdint>\n\n")
            f.write("const uint32_t memoryAddresses[] = {\n")

            # Write each address
            for i, addr in enumerate(addresses):
                line_end = ",\n" if i < len(addresses) - 1 else "\n"
                f.write(f"    {addr}{line_end}")

            f.write("};\n\n")
            f.write("const uint32_t memoryAddressCount = sizeof(memoryAddresses) / sizeof(memoryAddresses[0]);\n\n")
            f.write("#endif // ADDRESSES_H\n")

        print(f"Header file '{output_file}' generated with {len(addresses)} addresses.")


    except FileNotFoundError:
        print(f"Error: File not found at {file_path}")
    except json.JSONDecodeError as e:
        print(f"Error: Failed to decode JSON - {e}")

# --- PlatformIO hook ---
if PIO_MODE:
    # Register to run before build
    env.AddPreAction("buildprog", generate_header)
else:
    # Run directly when executed manually
    generate_header()
