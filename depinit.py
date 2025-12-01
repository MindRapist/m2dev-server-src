#!/usr/bin/env python3

import os
import subprocess
import sys
import shutil

# --- Configuration ---
# All paths are relative to the project root.

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
INCLUDE_DIR = os.path.join(PROJECT_ROOT, "include")

# Define all dependencies, their Git URLs, and which files need copying.
DEPS_INFO = [
	# Full Submodules (Consumed via add_subdirectory)
	{"name": "spdlog", "path": "vendor/spdlog", "url": "https://github.com/gabime/spdlog.git", "copy": []},
	{"name": "mariadb-connector-c", "path": "vendor/mariadb-connector-c", "url": "https://github.com/MariaDB/mariadb-connector-c.git", "copy": []},

	# Header-Only Submodules (Headers copied to include/, then vendor/ is cleaned up)
	{"name": "stb", "path": "vendor/stb", "url": "https://github.com/nothings/stb.git", "copy": ["stb_image.h", "stb_image_write.h"]},
	{"name": "pcg-cpp", "path": "vendor/pcg-cpp", "url": "https://github.com/imneme/pcg-cpp.git", "copy": ["pcg_random.hpp", "pcg_extras.hpp", "pcg_uint128.hpp"]},
	# Cryptopp requires slightly more specific header paths
	{"name": "cryptopp/src", "path": "vendor/cryptopp/src", "url": "https://github.com/weidai11/cryptopp.git", "copy": []},
]

def run_git_command(command, cwd=PROJECT_ROOT):
	"""Executes a git command and prints output."""
	print(f"  -> Executing: {' '.join(command)}")
	try:
		subprocess.run(command, check=True, cwd=cwd, stdout=sys.stdout, stderr=sys.stderr)
		return True
	except subprocess.CalledProcessError as e:
		print(f" ❌ Git command failed with code {e.returncode}: {e}")
		return False
	except FileNotFoundError:
		print(" ❌ FATAL ERROR: 'git' command not found. Ensure Git is installed and in your PATH.")
		sys.exit(1)


def check_and_add_submodule(dep):
	"""Checks if a submodule path exists, and runs git submodule add if it doesn't."""
	full_path = os.path.join(PROJECT_ROOT, dep['path'])
	
	if os.path.isdir(full_path):
		# Path exists, assume it's ready or will be fixed by the update
		return True

	print(f"❌ Submodule path not found: {dep['path']}")
	print("  -> Attempting to use 'git submodule add' to fix the repository index.")
	print("  *** WARNING: This command modifies your Git working tree and index. ***")
	
	# Run the add command
	if not run_git_command(["git", "submodule", "add", dep['url'], dep['path']]):
		print(f" ❌ Failed to 'git submodule add' for {dep['name']}.")
		return False

	# The 'add' command only registers it; run a final update to fetch content cleanly
	return run_git_command(["git", "submodule", "update", "--init", "--recursive", dep['path']])


def copy_headers_and_cleanup(dep):
	"""Copies required headers to include/ and removes the submodule directory."""
	if not dep['copy']:
		return True # Nothing to copy/cleanup

	# Check if a target header file already exists as a proxy for the entire set
	first_header_name = dep['copy'][0].split('/')[-1]
	target_path = os.path.join(INCLUDE_DIR, first_header_name)

	if os.path.exists(target_path):
		print(f" ⚠️ Headers for {dep['name']} already exist in include/.")
		return True
		
	print(f"  -> Copying headers for {dep['name']} to include/...")
	
	# 1. Ensure the include directory exists
	os.makedirs(INCLUDE_DIR, exist_ok=True)
	
	# 2. Copy files
	source_dir = os.path.join(PROJECT_ROOT, dep['path'])
	for file_path_rel in dep['copy']:
		# For cryptopp/src, the file_path_rel is "cryptopp/files.h", so we join it.
		src = os.path.join(source_dir, file_path_rel)
		# The destination is just the filename for simplicity
		dst_filename = file_path_rel.split('/')[-1] 
		dst = os.path.join(INCLUDE_DIR, dst_filename)
		
		try:
			# We copy the source file to the destination filename in include/
			shutil.copyfile(src, dst)
			print(f"	- Copied: {dst_filename}")
		except FileNotFoundError:
			print(f" ⚠️ Source file not found: {src}. Submodule content may be missing or incorrect.")
			return False

	# 3. Cleanup the submodule directory and remove the git tracking pointer
	print(f"  -> Cleaning up temporary submodule directory {dep['path']}...")

	try:
		shutil.rmtree(source_dir)
		# Note: We don't run git rm because we used 'git submodule add' which is destructive enough.
	except Exception as e:
		print(f" ⚠️ WARNING: Could not remove directory {dep['path']}: {e}")
		
	return True


def main():
	"""Main execution logic."""
	print("--- Dependency Initialization Script Running ---")
	
	# --- ADD SYNC HERE ---
	# 1. Sync all local submodule configurations with .gitmodules (crucial if URLs have changed)
	run_git_command(["git", "submodule", "sync"])

	# 2. Run a quick check/update for all submodules first to ensure they are consistent
	run_git_command(["git", "submodule", "update", "--init", "--recursive"])
	
	all_successful = True
	
	for dep in DEPS_INFO:
		print(f"\n[Processing {dep['name']}]")
		
		# 1. Check/Add Submodule: Ensures the folder and git pointer exist
		if not check_and_add_submodule(dep):
			all_successful = False
			continue
			
		# 2. Copy Headers/Cleanup: Handles headers for header-only libraries
		if dep['copy'] and not copy_headers_and_cleanup(dep):
			all_successful = False
			continue

	if all_successful:
		print("\n✅ All dependencies successfully checked and prepared.")
		return 0
	else:
		print("\n❌ FATAL ERROR: One or more dependency steps failed. Please check the logs.")
		return 1

if __name__ == "__main__":
	sys.exit(main())
