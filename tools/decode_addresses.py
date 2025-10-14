import json
import os
from pathlib import Path

def main():
    # Path to the directory this script lives in
    tools_dir = Path(__file__).resolve().parent

    # Navigate up to project root (one level above tools/)
    project_root = tools_dir.parent

    # full path to the ERD json file
    file_path = project_root / ".pio" / "libdeps" / "xiao_c3" / "public-appliance-api-documentation" / "appliance_api_erd_definitions.json"

    # List of known addresses to search (paste from MQTT broker)
    #known_addresses = ["0x0001","0x0002","0x0004","0x0008","0x000A","0x000E","0x0030","0x0032","0x0034","0x0039","0x0099","0x0105","0x2000","0x2001","0x2002","0x2007","0x200A","0x200E","0x2010","0x2012","0x2013","0x2015","0x2016","0x2017","0x2018","0x201D","0x2025","0x2026","0x202B","0x202C","0x202D","0x2031","0x2033","0x2034","0x2035","0x2036","0x2038","0x2039","0x2040"]
    known_addresses = ["0x0001","0x0002","0x0008","0x0099","0x0105","0x2000","0x2001","0x2002","0x2007","0x200A","0x200E","0x2010","0x2012","0x201B","0x201C","0x201D","0x2038","0x2039","0x2040","0x2041"]
    try:
        # Load the JSON file
        with open(file_path, "r", encoding="utf-8") as f:
            data = json.load(f)

        # Print the name of each known address
        print("Descriptions of known addresses:")
        for item in data["erds"]:
            addr = item["id"]
            if addr.casefold() in [known_address.casefold() for known_address in known_addresses]:
                name = item.get("name", "No name available")
                print(f"{addr}: {name}")
    
    except FileNotFoundError:
        print(f"Error: File not found at {file_path}")
    except json.JSONDecodeError as e:
        print(f"Error: Failed to decode JSON - {e}")

if __name__ == "__main__":
    main()
