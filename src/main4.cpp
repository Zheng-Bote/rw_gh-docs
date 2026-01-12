/*
 * SPDX-License-Identifier: MIT
 * Author: Robert Zheng
 * Copyright (c) 2026 ZHENG Robert
 */

/**
 * @file main4.cpp
 * @brief Static Site Generator (Version 4 with Inja Templates)
 *
 * This version integrates the 'inja' template engine to separate logic and
 * view. It builds a directory tree of Markdown files, renders them to HTML, and
 * merges them with a central HTML template.
 *
 * Dependencies:
 * - md4c (Markdown processing)
 * - nlohmann/json (JSON data for Inja)
 * - pantor/inja (Template engine)
 *
 * Compile:
 * g++ -std=c++23 -o ssg main4.cpp -lmd4c-html -lmd4c
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <vector>

// Libs
#include <inja/inja.hpp>
#include <md4c-html.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// --- Structures ---

/**
 * @brief Configuration structure.
 */
struct Config {
  fs::path templatePath; ///< Path to the Inja template file.
  fs::path outputDir =
      "output_site"; ///< Directory where the site is generated.
};

/**
 * @brief Directory node structure.
 */
struct DirNode {
  fs::path relativePath;        ///< Path relative to root.
  std::string dirName;          ///< Name of the directory.
  std::vector<fs::path> files;  ///< List of files in this directory.
  std::vector<DirNode> subdirs; ///< List of subdirectories.
};

// --- Helper ---

/**
 * @brief Reads the contents of a file.
 * @param path Path to the file.
 * @return File content.
 */
std::string readFile(const fs::path &path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in)
    throw std::runtime_error(
        std::format("Could not read file: {}", path.string()));
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return content;
}

/**
 * @brief Writes content to a file.
 * @param path Path to the file.
 * @param content Content to write.
 */
void writeFile(const fs::path &path, std::string_view content) {
  std::ofstream out(path, std::ios::out | std::ios::binary);
  if (!out)
    throw std::runtime_error(
        std::format("Could not write file: {}", path.string()));
  out << content;
}

// --- Markdown Logic ---

/**
 * @brief MD4C callback.
 */
void md_process_output(const MD_CHAR *text, MD_SIZE size, void *userdata) {
  std::string *out = static_cast<std::string *>(userdata);
  out->append(text, size);
}

/**
 * @brief Renders Markdown to HTML.
 * @param mdContent Markdown string.
 * @return HTML string.
 */
std::string renderMarkdown(const std::string &mdContent) {
  std::string htmlOutput;
  int ret = md_html(mdContent.c_str(), static_cast<MD_SIZE>(mdContent.size()),
                    md_process_output, &htmlOutput, MD_DIALECT_GITHUB, 0);
  if (ret != 0)
    throw std::runtime_error("Markdown parsing failed.");
  return htmlOutput;
}

// --- Config Parser ---

/**
 * @brief Parses the configuration file.
 * @param configPath Path to config file.
 * @return Config object.
 */
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

      // Trim Whitespaces (optional but good practice)
      // ... (simplified here without trim)

      if (key == "template")
        cfg.templatePath = value;
      else if (key == "output")
        cfg.outputDir = value;
    }
  }
  return cfg;
}

// --- Logic: Build Tree (MD only) ---

/**
 * @brief Builds the directory tree, filtering for .md files.
 * @param currentPath Current scanning path.
 * @param rootPath Root input path.
 * @return DirNode structure.
 */
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
      // ADAPTATION 1: Only .md files
      if (entry.path().extension() == ".md") {
        node.files.push_back(entry.path().filename());
      }
    }
  }
  std::ranges::sort(node.subdirs, {}, &DirNode::dirName);
  std::ranges::sort(node.files);
  return node;
}

/**
 * @brief Generates back references (../) for relative paths.
 * @param currentRelPath Current relative path.
 * @return String with "../" sequences.
 */
std::string getBackPrefix(const fs::path &currentRelPath) {
  std::string prefix = "";
  for (const auto &_ : currentRelPath) {
    prefix += "../";
  }
  return prefix;
}

/**
 * @brief Converts filename extension from .md to .html.
 * @param sourceFile Source filename.
 * @return Target filename.
 */
fs::path getTargetFilename(const fs::path &sourceFile) {
  fs::path p = sourceFile;
  // Since we only read MD, we always replace
  if (p.extension() == ".md")
    p.replace_extension(".html");
  return p;
}

// --- Navigation Generator ---

/**
 * @brief Generates Navigation HTML.
 *
 * We allow C++ to handle recursion and structure logic, passing the final HTML
 * string to the Inja template.
 *
 * @param currentNode Current node.
 * @param html Output HTML string.
 * @param urlPrefix URL prefix.
 * @param activeTargetFile Active file for highlighting.
 */
void generateNavHtml(const DirNode &currentNode, std::string &html,
                     const std::string &urlPrefix,
                     const fs::path &activeTargetFile) {

  html += "<ul class=\"nav-list\">\n";

  // Files
  for (const auto &file : currentNode.files) {
    std::string nameNoExt = file.stem().string();
    fs::path targetFile = getTargetFilename(file);
    fs::path fullLinkPath = currentNode.relativePath / targetFile;
    std::string href = urlPrefix + fullLinkPath.generic_string();

    std::string classAttr =
        (fullLinkPath == activeTargetFile) ? " class=\"active\"" : "";

    html += std::format("  <li><a href=\"{}\"{}>{}</a></li>\n", href, classAttr,
                        nameNoExt);
  }

  // Subdirectories
  for (const auto &sub : currentNode.subdirs) {
    size_t fileCount = sub.files.size();
    if (fileCount == 1) {
      fs::path targetFile = getTargetFilename(sub.files[0]);
      fs::path linkPath = sub.relativePath / targetFile;
      std::string href = urlPrefix + linkPath.generic_string();
      std::string classAttr =
          (linkPath == activeTargetFile) ? " class=\"active\"" : "";

      html += std::format("  <li><a href=\"{}\"{}>{}</a></li>\n", href,
                          classAttr, sub.dirName);
    } else {
      html += std::format("  <li><strong>{}</strong>\n", sub.dirName);
      generateNavHtml(sub, html, urlPrefix, activeTargetFile);
      html += "  </li>\n";
    }
  }
  html += "</ul>\n";
}

// --- Processing with Inja ---

/**
 * @brief Processes files using Inja templates.
 * @param currentNode Current node.
 * @param rootNode Root node.
 * @param inputRoot Input root.
 * @param cfg Config.
 * @param env Inja environment.
 * @param tmpl Parsed Inja template.
 */
void processFiles(const DirNode &currentNode, const DirNode &rootNode,
                  const fs::path &inputRoot, const Config &cfg,
                  inja::Environment &env, const inja::Template &tmpl) {

  fs::path currentOutputDir = cfg.outputDir / currentNode.relativePath;
  fs::create_directories(currentOutputDir);

  // Prefix for links (e.g. "../../") for CSS etc.
  std::string backPrefix = getBackPrefix(currentNode.relativePath);

  for (const auto &file : currentNode.files) {
    fs::path inputPath = inputRoot / currentNode.relativePath / file;
    fs::path targetFilename = getTargetFilename(file);
    fs::path outputPath = currentOutputDir / targetFilename;
    fs::path currentActiveFile = currentNode.relativePath / targetFilename;

    // 1. Generate Navigation HTML
    std::string navHtml;
    generateNavHtml(rootNode, navHtml, backPrefix, currentActiveFile);

    // 2. Read and Render Markdown
    std::string rawContent = readFile(inputPath);
    std::string htmlContent = renderMarkdown(rawContent);

    // 3. ADAPTATION 2: Prepare data for Inja
    json data;
    data["base_path"] = backPrefix;       // For CSS Links
    data["title"] = file.stem().string(); // Page Title
    data["navigation"] = navHtml;         // The generated list
    data["content"] = htmlContent;        // The actual content

    // 4. Merge with Template
    try {
      std::string finalResult = env.render(tmpl, data);
      writeFile(outputPath, finalResult);
      std::println("Created: {}", outputPath.string());
    } catch (const std::exception &e) {
      std::println(stderr, "Template Error in {}: {}", file.string(), e.what());
    }
  }

  // Recursion
  for (const auto &sub : currentNode.subdirs) {
    processFiles(sub, rootNode, inputRoot, cfg, env, tmpl);
  }
}

/**
 * @brief Main entry point.
 */
int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::println(stderr, "Usage: {} <path_to_config> <input_folder>", argv[0]);
    return 1;
  }

  fs::path configPath = argv[1];
  fs::path inputDir = argv[2];

  try {
    // Load config
    Config cfg = parseConfig(configPath);

    if (!fs::exists(inputDir))
      throw std::runtime_error("Input folder does not exist.");
    if (!fs::exists(cfg.templatePath))
      throw std::runtime_error("Template file does not exist.");

    // Build tree
    std::println("Scanning structure (.md only)...");
    DirNode rootNode = buildTree(inputDir, inputDir);

    // Prepare output
    if (fs::exists(cfg.outputDir))
      fs::remove_all(cfg.outputDir);
    fs::create_directories(cfg.outputDir);

    // Initialize Inja Environment
    std::println("Loading template...");
    inja::Environment env;
    // Parse template once for performance
    inja::Template tmpl = env.parse_template(cfg.templatePath.string());

    std::println("Generating pages with Inja...");
    processFiles(rootNode, rootNode, inputDir, cfg, env, tmpl);

    std::println("Done! Output in: {}", cfg.outputDir.string());

  } catch (const std::exception &e) {
    std::println(stderr, "Error: {}", e.what());
    return 1;
  }

  return 0;
}