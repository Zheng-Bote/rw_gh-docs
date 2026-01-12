/*
Dependencies:
1. md4c (Markdown)
2. nlohmann/json
3. pantor/inja

Compile Example (assuming headers are in include path):
g++ -std=c++23 -o ssg main.cpp -lmd4c-html -lmd4c
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

// --- Strukturen ---
struct Config {
  fs::path templatePath; // Nur noch ein Template File
  fs::path outputDir = "output_site";
};

struct DirNode {
  fs::path relativePath;
  std::string dirName;
  std::vector<fs::path> files;
  std::vector<DirNode> subdirs;
};

// --- Helper ---
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

// --- Markdown Logic ---
void md_process_output(const MD_CHAR *text, MD_SIZE size, void *userdata) {
  std::string *out = static_cast<std::string *>(userdata);
  out->append(text, size);
}

std::string renderMarkdown(const std::string &mdContent) {
  std::string htmlOutput;
  int ret = md_html(mdContent.c_str(), static_cast<MD_SIZE>(mdContent.size()),
                    md_process_output, &htmlOutput, MD_DIALECT_GITHUB, 0);
  if (ret != 0)
    throw std::runtime_error("Markdown parsing failed.");
  return htmlOutput;
}

// --- Config Parser (Angepasst für Template) ---
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
      // ... (hier vereinfacht ohne Trim)

      if (key == "template")
        cfg.templatePath = value;
      else if (key == "output")
        cfg.outputDir = value;
    }
  }
  return cfg;
}

// --- Logic: Build Tree (Nur .md) ---
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
      // ANPASSUNG 1: Nur noch .md Dateien
      if (entry.path().extension() == ".md") {
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

fs::path getTargetFilename(const fs::path &sourceFile) {
  fs::path p = sourceFile;
  // Da wir nur MD einlesen, wird immer ersetzt
  if (p.extension() == ".md")
    p.replace_extension(".html");
  return p;
}

// --- Navigation Generator ---
// Wir generieren weiterhin HTML für die Navi hier, da rekursive Strukturen
// in Inja Templates komplex sind. Wir übergeben den HTML-String an Inja.
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
void processFiles(const DirNode &currentNode, const DirNode &rootNode,
                  const fs::path &inputRoot, const Config &cfg,
                  inja::Environment &env, const inja::Template &tmpl) {

  fs::path currentOutputDir = cfg.outputDir / currentNode.relativePath;
  fs::create_directories(currentOutputDir);

  // Prefix für Links (z.B. "../../") für CSS etc.
  std::string backPrefix = getBackPrefix(currentNode.relativePath);

  for (const auto &file : currentNode.files) {
    fs::path inputPath = inputRoot / currentNode.relativePath / file;
    fs::path targetFilename = getTargetFilename(file);
    fs::path outputPath = currentOutputDir / targetFilename;
    fs::path currentActiveFile = currentNode.relativePath / targetFilename;

    // 1. Navigation HTML generieren
    std::string navHtml;
    generateNavHtml(rootNode, navHtml, backPrefix, currentActiveFile);

    // 2. Markdown lesen und rendern
    std::string rawContent = readFile(inputPath);
    std::string htmlContent = renderMarkdown(rawContent);

    // 3. ANPASSUNG 2: Daten für Inja vorbereiten
    json data;
    data["base_path"] = backPrefix;       // Für CSS Links
    data["title"] = file.stem().string(); // Seitentitel
    data["navigation"] = navHtml;         // Die generierte Liste
    data["content"] = htmlContent;        // Der eigentliche Text

    // 4. Mit Template mergen
    try {
      std::string finalResult = env.render(tmpl, data);
      writeFile(outputPath, finalResult);
      std::println("Created: {}", outputPath.string());
    } catch (const std::exception &e) {
      std::println(stderr, "Template Error in {}: {}", file.string(), e.what());
    }
  }

  // Rekursion
  for (const auto &sub : currentNode.subdirs) {
    processFiles(sub, rootNode, inputRoot, cfg, env, tmpl);
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
    // Config laden
    Config cfg = parseConfig(configPath);

    if (!fs::exists(inputDir))
      throw std::runtime_error("Input folder does not exist.");
    if (!fs::exists(cfg.templatePath))
      throw std::runtime_error("Template file does not exist.");

    // Baum aufbauen
    std::println("Scanning structure (.md only)...");
    DirNode rootNode = buildTree(inputDir, inputDir);

    // Output vorbereiten
    if (fs::exists(cfg.outputDir))
      fs::remove_all(cfg.outputDir);
    fs::create_directories(cfg.outputDir);

    // Inja Environment initialisieren
    std::println("Loading template...");
    inja::Environment env;
    // Template einmal parsen für Performance
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