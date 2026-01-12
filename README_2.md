<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->
**Table of Contents**

- [C++ Documentation Tools](#c-documentation-tools)
  - [Components](#components)
    - [1. GitHub Docs Fetcher (`src/main.cpp`)](#1-github-docs-fetcher-srcmaincpp)
    - [2. Static Site Generator (`src/main2.cpp`)](#2-static-site-generator-srcmain2cpp)
    - [3. Docs Updater Bot (`src/test.cpp`)](#3-docs-updater-bot-srctestcpp)
  - [Requirements](#requirements)
  - [Building](#building)
  - [Setup](#setup)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

# C++ Documentation Tools

This repository contains a set of C++ tools designed for managing, fetching, and generating documentation from GitHub repositories.

## Components

The project consists of three main utilities located in the `src/` directory:

### 1. GitHub Docs Fetcher (`src/main.cpp`)

A C++23 application that connects to a GitHub repository to retrieve documentation files.

- **Functionality**:
  - Fetches the file list from a `docs/` folder in a specified repository.
  - Filters for directories named with a 4-digit year (YYYY).
  - Downloads `.md` files and extracts their "Front Matter" (YAML-style metadata between `---`).
  - Prints metadata fields like `TITLE`, `DESCRIPTION`, `AUTHOR`, etc., to the console.
- **Configuration**: Requires a `.env` file with `GITHUB_USER`, `GITHUB_REPO`, and `GITHUB_TOKEN`.
- **Dependencies**: `libcurl`, `nlohmann/json`.

### 2. Static Site Generator (`src/main2.cpp`)

A tool to convert a directory of Markdown and HTML files into a static website.

- **Functionality**:
  - Scans an input directory recursively for `.md` and `.htm` files.
  - Converts Markdown to HTML using the **MD4C** library (GitHub Flavored Markdown).
  - Generates a context-aware navigation tree (sidebar) for all pages.
  - Wraps content with a configurable HTML header and footer.
- **Usage**: `./ssg <config_file> <input_folder>`
- **Configuration**: A text file specifying `header=<path>`, `footer=<path>`, and `output=<dir>`.
- **Dependencies**: `md4c` (libmd4c-html).

### 3. Docs Updater Bot (`src/test.cpp`)

A utility for automating updates to documentation files directly on GitHub.

- **Functionality**:
  - Iterates through `.md` files in the `docs/` folder of a repository.
  - Downloads the current content of each file.
  - Appends a marker string (`<!-- Updated by C++ bot -->`) to the content.
  - Commits the changes back to the repository via the GitHub API.
- **Configuration**: Hardcoded credentials (needs to be updated or replaced with env vars before real use).
- **Dependencies**: `libcurl`, `nlohmann/json`.

## Requirements

To build and run these projects, you will need:

- **C++ Compiler**: Support for C++23 (e.g., GCC 13+, Clang 16+).
- **Libraries**:
  - `libcurl` (for network requests)
  - `nlohmann/json` (for JSON parsing)
  - `md4c` (for Markdown processing)

## Building

If using CMake:

```bash
mkdir -p build && cd build
cmake ..
make
```

## Setup

1.  **Environment Variables**:
    Create a `.env` file in your working directory for `main.cpp`:

    ```ini
    GITHUB_USER=your_username
    GITHUB_REPO=your_repo
    GITHUB_TOKEN=your_personal_access_token
    BRANCH=main
    ```

2.  **SSG Config**:
    Create a config text file for `main2.cpp`:
    ```ini
    header=assets/header.htm
    footer=assets/footer.htm
    output=dist
    ```
