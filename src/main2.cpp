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

// MD4C Header einbinden (muss installiert sein)
#include <md4c-html.h>

namespace fs = std::filesystem;

// --- Strukturen ---
struct Config {
  fs::path headerPath;
  fs::path footerPath;
  fs::path outputDir = "output_site";
};

struct DirNode {
  fs::path relativePath;
  std::string dirName;
  std::vector<fs::path> files; // Speichert Original-Dateinamen (z.B. "info.md")
  std::vector<DirNode> subdirs;
};

// --- Hilfsfunktionen IO ---
std::string readFile(const fs::path &path) {
  std::ifstream in(path, std::ios::in | std::ios::binary);
  if (!in)
    throw std::runtime_error(
        std::format("Konnte Datei nicht lesen: {}", path.string()));
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return content;
}

void writeFile(const fs::path &path, std::string_view content) {
  std::ofstream out(path, std::ios::out | std::ios::binary);
  if (!out)
    throw std::runtime_error(
        std::format("Konnte Datei nicht schreiben: {}", path.string()));
  out << content;
}

// --- Markdown Logik (MD4C Callback) ---
// Callback, den MD4C aufruft, um geparste HTML-Schnipsel zurückzugeben
void md_process_output(const MD_CHAR *text, MD_SIZE size, void *userdata) {
  std::string *out = static_cast<std::string *>(userdata);
  out->append(text, size);
}

std::string renderMarkdown(const std::string &mdContent) {
  std::string htmlOutput;
  // MD_DIALECT_GITHUB aktiviert GitHub Flavored Markdown (Tabellen, Tasklists
  // etc.) 0 sind die Renderer-Flags
  int ret = md_html(mdContent.c_str(), static_cast<MD_SIZE>(mdContent.size()),
                    md_process_output, &htmlOutput, MD_DIALECT_GITHUB, 0);

  if (ret != 0) {
    throw std::runtime_error("Markdown Parsing fehlgeschlagen.");
  }
  return htmlOutput;
}

Config parseConfig(const fs::path &configPath) {
  Config cfg;
  std::ifstream file(configPath);
  if (!file)
    throw std::runtime_error("Konfigurationsdatei nicht gefunden.");
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

// --- Logik: Baum aufbauen ---
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
      // Check auf .htm UND .md
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

// --- Hilfsfunktion: Extension Swap ---
// Macht aus "file.md" -> "file.html" und lässt "file.htm" -> "file.htm" (oder
// .html wenn gewünscht)
fs::path getTargetFilename(const fs::path &sourceFile) {
  fs::path p = sourceFile;
  if (p.extension() == ".md") {
    p.replace_extension(".html");
  }
  return p;
}

// --- Navigations-Generator ---
void generateNavHtml(const DirNode &currentNode, std::string &html,
                     const std::string &urlPrefix,
                     const fs::path &activeTargetFile) {

  html += "<ul>\n";

  // 1. Dateien
  for (const auto &file : currentNode.files) {
    std::string nameNoExt = file.stem().string();

    // WICHTIG: Link zeigt immer auf das Ziel-Format (.html für .md Dateien)
    fs::path targetFile = getTargetFilename(file);
    fs::path fullLinkPath = currentNode.relativePath / targetFile;

    std::string href = urlPrefix + fullLinkPath.generic_string();

    // Active Check vergleicht Ziel-Dateinamen
    std::string classAttr =
        (fullLinkPath == activeTargetFile) ? " class=\"active\"" : "";

    html += std::format("  <li><a href=\"{}\"{}>{}</a></li>\n", href, classAttr,
                        nameNoExt);
  }

  // 2. Unterordner
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

// --- Verarbeitung ---
void processFiles(const DirNode &currentNode, const DirNode &rootNode,
                  const fs::path &inputRoot, const Config &cfg,
                  const std::string &headerContent,
                  const std::string &footerContent) {

  fs::path currentOutputDir = cfg.outputDir / currentNode.relativePath;
  fs::create_directories(currentOutputDir);
  std::string backPrefix = getBackPrefix(currentNode.relativePath);

  for (const auto &file : currentNode.files) {
    fs::path inputPath = inputRoot / currentNode.relativePath / file;

    // Zieldateiname berechnen (.md -> .html)
    fs::path targetFilename = getTargetFilename(file);
    fs::path outputPath = currentOutputDir / targetFilename;

    // Active-Status bezieht sich auf die Zieldatei (HTML Struktur)
    fs::path currentActiveFile = currentNode.relativePath / targetFilename;

    // Navi generieren
    std::string contextAwareNav;
    generateNavHtml(rootNode, contextAwareNav, backPrefix, currentActiveFile);
    std::string fullNav =
        "<nav class='main-nav'>\n" + contextAwareNav + "</nav>\n";

    // Inhalt lesen & ggf. konvertieren
    std::string rawContent = readFile(inputPath);
    std::string processedContent;

    if (inputPath.extension() == ".md") {
      processedContent = renderMarkdown(rawContent);
      std::println("Markdown verarbeitet: {}", file.string());
    } else {
      processedContent = rawContent; // .htm bleibt wie es ist
    }

    std::string finalHtml = headerContent + "\n" + fullNav + "\n<main>\n" +
                            processedContent + "\n</main>\n" + footerContent;

    writeFile(outputPath, finalHtml);
    std::println("Erstellt: {}", outputPath.string());
  }

  for (const auto &sub : currentNode.subdirs) {
    processFiles(sub, rootNode, inputRoot, cfg, headerContent, footerContent);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::println(stderr, "Nutzung: {} <pfad_zu_config> <input_ordner>",
                 argv[0]);
    return 1;
  }

  fs::path configPath = argv[1];
  fs::path inputDir = argv[2];

  try {
    Config cfg = parseConfig(configPath);
    std::string header = readFile(cfg.headerPath);
    std::string footer = readFile(cfg.footerPath);

    if (!fs::exists(inputDir))
      throw std::runtime_error("Input Ordner existiert nicht.");

    std::println("Scanne Struktur (htm & md)...");
    DirNode rootNode = buildTree(inputDir, inputDir);

    if (fs::exists(cfg.outputDir))
      fs::remove_all(cfg.outputDir);
    fs::create_directories(cfg.outputDir);

    std::println("Generiere Seiten...");
    processFiles(rootNode, rootNode, inputDir, cfg, header, footer);

    std::println("Fertig! Output in: {}", cfg.outputDir.string());

  } catch (const std::exception &e) {
    std::println(stderr, "Fehler: {}", e.what());
    return 1;
  }

  return 0;
}