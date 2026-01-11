/*
macOS(Homebrew): brew install md4c
Linux(Debian / Ubuntu): sudo apt install libmd4c-dev

macOS:
clang++ -std=c++23 -o ssg main.cpp -I/opt/homebrew/include -L/opt/homebrew/lib
-lmd4c-html -lmd4c
Linux: g++ -std=c++23 -o ssg main.cpp -lmd4c-html -lmd4c

*/

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <vector>

// Include MD4C Header (must be installed)
#include <md4c-html.h>

namespace fs = std::filesystem;

// --- Structures ---
struct Config {
  fs::path headerPath;
  fs::path footerPath;
  fs::path outputDir = "output_site";
};

struct DirNode {
  fs::path relativePath;
  std::string dirName;
  std::vector<fs::path> files; // Stores original filenames (e.g. "info.md")
  std::vector<DirNode> subdirs;
};

// --- IO Helper Functions ---
std::string readFile(const fs::path &path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in)
    throw std::runtime_error(
        std::format("Could not read file: {}", path.string()));
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return content;
}

void writeFile(const fs::path &path, std::string_view content) {
  std::ofstream out(path, std::ios::out | std::ios::binary);
  if (!out)
    throw std::runtime_error(
        std::format("Could not write file: {}", path.string()));
  out << content;
}

// --- Markdown Logic (MD4C Callback) ---
// Callback called by MD4C to return parsed HTML snippets
void md_process_output(const MD_CHAR *text, MD_SIZE size, void *userdata) {
  std::string *out = static_cast<std::string *>(userdata);
  out->append(text, size);
}

std::string renderMarkdown(const std::string &mdContent) {
  std::string htmlOutput;
  // MD_DIALECT_GITHUB enables GitHub Flavored Markdown (tables, tasklists
  // etc.) 0 are the renderer flags
  int ret = md_html(mdContent.c_str(), static_cast<MD_SIZE>(mdContent.size()),
                    md_process_output, &htmlOutput, MD_DIALECT_GITHUB, 0);

  if (ret != 0) {
    throw std::runtime_error("Markdown parsing failed.");
  }
  return htmlOutput;
}

Config parseConfig(const fs::path &configPath) {
  Config cfg;
  std::ifstream file(configPath);
  if (!file)
    throw std::runtime_error("Configuration file not found.");
  std::string line;
  while (std::getline(file, line)) {
    auto delimiterPos = line.find('=');
    if (delimiterPos != std::string::npos) {
      std::string key = line.substr(0, delimiterPos);
      std::string value = line.substr(delimiterPos + 1);
      if (key == "header")
        cfg.headerPath = value;
      else if (key == "footer")
        cfg.footerPath = value;
      else if (key == "output")
        cfg.outputDir = value;
    }
  }
  return cfg;
}

// --- Logic: Build Tree ---
DirNode buildTree(const fs::path &currentPath, const fs::path &rootPath) {
  DirNode node;
  node.dirName = currentPath.filename().string();
  node.relativePath = fs::relative(currentPath, rootPath);
  if (node.relativePath == ".")
    node.relativePath = "";

  for (const auto &entry : fs::directory_iterator(currentPath)) {
    if (entry.is_directory()) {
      node.subdirs.push_back(buildTree(entry.path(), rootPath));
    } else if (entry.is_regular_file()) {
      // Check for .htm AND .md
      auto ext = entry.path().extension();
      if (ext == ".htm" || ext == ".md") {
        node.files.push_back(entry.path().filename());
      }
    }
  }

  std::ranges::sort(node.subdirs, {}, &DirNode::dirName);
  std::ranges::sort(node.files);
  return node;
}

std::string getBackPrefix(const fs::path &currentRelPath) {
  std::string prefix = "";
  for (const auto &_ : currentRelPath) {
    prefix += "../";
  }
  return prefix;
}

// --- Helper Function: Extension Swap ---
// Converts "file.md" -> "file.html" and leaves "file.htm" -> "file.htm" (or
// .html if desired)
fs::path getTargetFilename(const fs::path &sourceFile) {
  fs::path p = sourceFile;
  if (p.extension() == ".md") {
    p.replace_extension(".html");
  }
  return p;
}

// --- Navigation Generator ---
void generateNavHtml(const DirNode &currentNode, std::string &html,
                     const std::string &urlPrefix,
                     const fs::path &activeTargetFile) {

  html += "<ul>\n";

  // 1. Files
  for (const auto &file : currentNode.files) {
    std::string nameNoExt = file.stem().string();

    // IMPORTANT: Link always points to the target format (.html for .md files)
    fs::path targetFile = getTargetFilename(file);
    fs::path fullLinkPath = currentNode.relativePath / targetFile;

    std::string href = urlPrefix + fullLinkPath.generic_string();

    // Active check compares target filenames
    std::string classAttr =
        (fullLinkPath == activeTargetFile) ? " class=\"active\"" : "";

    html += std::format("  <li><a href=\"{}\"{}>{}</a></li>\n", href, classAttr,
                        nameNoExt);
  }

  // 2. Subdirectories
  for (const auto &sub : currentNode.subdirs) {
    size_t fileCount = sub.files.size();

    if (fileCount == 1) {
      // Req 5
      fs::path targetFile = getTargetFilename(sub.files[0]);
      fs::path linkPath = sub.relativePath / targetFile;
      std::string href = urlPrefix + linkPath.generic_string();

      std::string classAttr =
          (linkPath == activeTargetFile) ? " class=\"active\"" : "";

      html += std::format("  <li><a href=\"{}\"{}>{}</a></li>\n", href,
                          classAttr, sub.dirName);
    } else {
      // Req 6
      html += std::format("  <li><strong>{}</strong>\n", sub.dirName);
      generateNavHtml(sub, html, urlPrefix, activeTargetFile);
      html += "  </li>\n";
    }
  }
  html += "</ul>\n";
}

// --- Processing ---
void processFiles(const DirNode &currentNode, const DirNode &rootNode,
                  const fs::path &inputRoot, const Config &cfg,
                  const std::string &headerContent,
                  const std::string &footerContent) {

  fs::path currentOutputDir = cfg.outputDir / currentNode.relativePath;
  fs::create_directories(currentOutputDir);
  std::string backPrefix = getBackPrefix(currentNode.relativePath);

  for (const auto &file : currentNode.files) {
    fs::path inputPath = inputRoot / currentNode.relativePath / file;

    // Calculate target filename (.md -> .html)
    fs::path targetFilename = getTargetFilename(file);
    fs::path outputPath = currentOutputDir / targetFilename;

    // Active status refers to the target file (HTML structure)
    fs::path currentActiveFile = currentNode.relativePath / targetFilename;

    // Generate Nav
    std::string contextAwareNav;
    generateNavHtml(rootNode, contextAwareNav, backPrefix, currentActiveFile);
    std::string fullNav =
        "<nav class='main-nav'>\n" + contextAwareNav + "</nav>\n";

    // Read content & convert if necessary
    std::string rawContent = readFile(inputPath);
    std::string processedContent;

    if (inputPath.extension() == ".md") {
      processedContent = renderMarkdown(rawContent);
      std::println("Markdown processed: {}", file.string());
    } else {
      processedContent = rawContent; // .htm stays as is
    }

    std::string finalHtml = headerContent + "\n" + fullNav + "\n<main>\n" +
                            processedContent + "\n</main>\n" + footerContent;

    writeFile(outputPath, finalHtml);
    std::println("Created: {}", outputPath.string());
  }

  for (const auto &sub : currentNode.subdirs) {
    processFiles(sub, rootNode, inputRoot, cfg, headerContent, footerContent);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::println(stderr, "Usage: {} <path_to_config> <input_folder>", argv[0]);
    return 1;
  }

  fs::path configPath = argv[1];
  fs::path inputDir = argv[2];

  try {
    Config cfg = parseConfig(configPath);
    std::string header = readFile(cfg.headerPath);
    std::string footer = readFile(cfg.footerPath);

    if (!fs::exists(inputDir))
      throw std::runtime_error("Input folder does not exist.");

    std::println("Scanning structure (htm & md)...");
    DirNode rootNode = buildTree(inputDir, inputDir);

    if (fs::exists(cfg.outputDir))
      fs::remove_all(cfg.outputDir);
    fs::create_directories(cfg.outputDir);

    std::println("Generating pages...");
    processFiles(rootNode, rootNode, inputDir, cfg, header, footer);

    std::println("Done! Output in: {}", cfg.outputDir.string());

  } catch (const std::exception &e) {
    std::println(stderr, "Error: {}", e.what());
    return 1;
  }

  return 0;
}