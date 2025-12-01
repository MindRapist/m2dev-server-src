# m2dev-server-src
[![build](https://github.com/d1str4ught/m2dev-server-src/actions/workflows/main.yml/badge.svg)](https://github.com/d1str4ught/m2dev-server-src/actions/workflows/main.yml)


Clean server sources for educational purposes.

It builds as it is, without external dependencies.

## 🚀 How to Install and Manage Dependencies

This repository uses **Git submodules** to manage official and up-to-date dependencies, ensuring that the source code for libraries like MariaDB Connector C, spdlog, and Cryptopp are handled directly by Git.

**Crucially, the `cmake` command automatically checks for and fetches all required dependencies (including header files) every time it runs, eliminating most manual setup.**

### 📥 Installation

The initial setup requires cloning the repository and then running the standard build commands.

```
# 1. Clone the repository

git clone <repo>
cd <repo>
```

### 📦 Manual Dependency Management (For Updates or Rollbacks)
To manually install all dependencies (and copy external headers) for the first time, you just need to run the Git initialization command. While the cmake command will run the specific checks, this command is the general way to ensure everything is set up:

```
# Initializes and updates all submodules to the version specified by this repository
# Note: Subsequent 'cmake ..' runs will copy the headers to the 'includes' folder.

git submodule update --init --recursive
```

To manually update all existing dependencies (and copy headers) to the latest commit defined in this repository:

```
# Updates all submodules to the latest upstream version defined in this repository's commit history

git submodule update --remote
```

To manually rollback to an older version of a dependency (example: mariadb-connector-c 3.4.5), you need to specify the commit hash or tag:

```
# 1. Change the submodule pointer to a specific tag or commit hash
#    (Example uses tag 'v3.4.5' for mariadb-connector-c)
#    THE SUBMODULE MUST BE ALREADY INSTALLED IN ORDER TO SWITCH VERSIONS!

git submodule update --checkout v3.4.5 vendor/mariadb-connector-c

# 2. Run CMake to recopy the /include headers from the old version (if applicable)

mkdir -p build && cmake -B build
```

## ⚙️ How to Build
After the initial clone, the project can be built with the standard CMake two-step process.

```
# 1. Create a build directory
mkdir build

# 2. Navigate to the build directory
cd build

# 3. Configure the project (Triggers automated dependency checks and fetching)
cmake ..

# 4. Build the project
cmake --build .
```


