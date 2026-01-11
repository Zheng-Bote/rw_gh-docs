// main.cpp
// C++23 — Reads all docs/YYYY/*.md from a GitHub repo, extracts headers
// between --- and outputs the fields. Requires: libcurl, nlohmann::json
// (FetchContent in CMake). .env in working directory with GITHUB_USER,
// GITHUB_REPO, GITHUB_TOKEN, optional BRANCH.

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;

// -------------------- Utility --------------------
static std::string trim(const std::string &s) {
  size_t a = 0;
  while (a < s.size() && std::isspace((unsigned char)s[a]))
    ++a;
  size_t b = s.size();
  while (b > a && std::isspace((unsigned char)s[b - 1]))
    --b;
  return s.substr(a, b - a);
}

std::map<std::string, std::string> load_dotenv(const fs::path &path) {
  std::map<std::string, std::string> env;
  std::ifstream in(path);
  if (!in.is_open())
    return env;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#')
      continue;
    auto pos = line.find('=');
    if (pos == std::string::npos)
      continue;
    std::string key = trim(line.substr(0, pos));
    std::string val = trim(line.substr(pos + 1));
    if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                            (val.front() == '\'' && val.back() == '\''))) {
      val = val.substr(1, val.size() - 2);
    }
    env[key] = val;
  }
  return env;
}

// -------------------- Base64 --------------------
static const std::string b64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_decode(const std::string &in) {
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; ++i)
    T[(unsigned char)b64_chars[i]] = i;
  std::string out;
  int val = 0, valb = -8;
  for (unsigned char c : in) {
    if (T[c] == -1) {
      if (c == '=')
        break;
      continue;
    }
    val = (val << 6) + T[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

// -------------------- CURL helpers --------------------
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  std::string *s = static_cast<std::string *>(userp);
  s->append(static_cast<char *>(contents), size * nmemb);
  return size * nmemb;
}

struct CurlHandle {
  CURL *curl;
  CurlHandle() { curl = curl_easy_init(); }
  ~CurlHandle() {
    if (curl)
      curl_easy_cleanup(curl);
  }
};

struct HttpResult {
  long http_code = 0;
  std::string body;
  CURLcode curl_code = CURLE_OK;
};

HttpResult http_get_raw_with_status(const std::string &url,
                                    const std::string &token = "") {
  HttpResult res;
  CurlHandle ch;
  if (!ch.curl) {
    res.curl_code = CURLE_FAILED_INIT;
    return res;
  }

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "User-Agent: Cpp-GitHub-Client");
  if (!token.empty()) {
    std::string auth = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth.c_str());
  }

  curl_easy_setopt(ch.curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(ch.curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(ch.curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(ch.curl, CURLOPT_WRITEDATA, &res.body);

  res.curl_code = curl_easy_perform(ch.curl);
  curl_easy_getinfo(ch.curl, CURLINFO_RESPONSE_CODE, &res.http_code);

  if (headers)
    curl_slist_free_all(headers);
  return res;
}

std::optional<json> http_get_json(const std::string &url,
                                  const std::string &token = "") {
  auto r = http_get_raw_with_status(url, token);
  if (r.curl_code != CURLE_OK) {
    std::cerr << "curl GET failed: " << curl_easy_strerror(r.curl_code)
              << " for " << url << "\n";
    return std::nullopt;
  }
  if (r.http_code >= 400) {
    std::cerr << "HTTP GET " << r.http_code << " for " << url << "\n";
    std::string preview = r.body.substr(0, 1024);
    if (!preview.empty())
      std::cerr << "response preview:\n" << preview << "\n";
    return std::nullopt;
  }
  try {
    return json::parse(r.body);
  } catch (const json::parse_error &e) {
    std::cerr << "JSON parse error for " << url << ": " << e.what() << "\n";
    std::string preview = r.body.substr(0, 1024);
    if (!preview.empty())
      std::cerr << "response preview:\n" << preview << "\n";
    return std::nullopt;
  }
}

// -------------------- Header parsing --------------------
std::map<std::string, std::string> parse_front_matter(const std::string &text) {
  // Expects YAML-like header between '---' at the beginning and the next
  // '---'
  std::map<std::string, std::string> result;
  size_t pos = 0;
  // skip leading whitespace
  while (pos < text.size() && std::isspace((unsigned char)text[pos]))
    ++pos;
  if (pos >= text.size())
    return result;
  if (text.compare(pos, 3, "---") != 0)
    return result; // no header
  pos += 3;
  // skip possible newline after first ---
  if (pos < text.size() && (text[pos] == '\r' || text[pos] == '\n')) {
    // consume single CRLF or LF
    if (text[pos] == '\r' && pos + 1 < text.size() && text[pos + 1] == '\n')
      pos += 2;
    else
      pos += 1;
  }
  size_t end = text.find("\n---", pos);
  if (end == std::string::npos) {
    // try CRLF variant
    end = text.find("\r\n---", pos);
    if (end == std::string::npos)
      return result;
  }
  std::string header = text.substr(pos, end - pos);
  // split lines
  std::istringstream iss(header);
  std::string line;
  while (std::getline(iss, line)) {
    // remove CR if present
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    line = trim(line);
    if (line.empty())
      continue;
    // expected format: KEY: value
    auto colon = line.find(':');
    if (colon == std::string::npos)
      continue;
    std::string key = trim(line.substr(0, colon));
    std::string val = trim(line.substr(colon + 1));
    result[key] = val;
  }
  return result;
}

// -------------------- Main --------------------
int main(int argc, char **argv) {
  curl_global_init(CURL_GLOBAL_DEFAULT);

  fs::path cwd = fs::current_path();
  fs::path env_path = cwd / ".env";
  auto env = load_dotenv(env_path);

  auto get_env = [&](const std::string &key, const std::string &def = "") {
    auto it = env.find(key);
    return it != env.end() ? it->second : def;
  };

  std::string user = get_env("GITHUB_USER");
  std::string repo = get_env("GITHUB_REPO");
  std::string token = get_env("GITHUB_TOKEN");
  std::string branch = get_env("BRANCH", "main");

  if (user.empty() || repo.empty() || token.empty()) {
    std::cerr << "Missing configuration in .env. Please set GITHUB_USER, "
                 "GITHUB_REPO, GITHUB_TOKEN\n";
    curl_global_cleanup();
    return 1;
  }

  // 1) List entries in docs/
  std::string api_docs =
      "https://api.github.com/repos/" + user + "/" + repo + "/contents/docs";
  auto docs_list_opt = http_get_json(api_docs, token);
  if (!docs_list_opt.has_value()) {
    std::cerr << "Could not fetch docs/.\n";
    curl_global_cleanup();
    return 1;
  }
  json docs_list = *docs_list_opt;
  if (!docs_list.is_array()) {
    std::cerr << "Expected array for docs/ content.\n";
    curl_global_cleanup();
    return 1;
  }

  // For each entry in docs/, check if it is a directory YYYY
  for (auto &entry : docs_list) {
    if (!entry.is_object())
      continue;
    std::string type = entry.value("type", "");
    std::string name = entry.value("name", "");
    if (type != "dir")
      continue;
    // optional: check if name is a 4-digit year
    bool is_year =
        (name.size() == 4) && std::all_of(name.begin(), name.end(), ::isdigit);
    if (!is_year)
      continue;

    std::string subdir_api = "https://api.github.com/repos/" + user + "/" +
                             repo + "/contents/docs/" + name;
    auto sub_list_opt = http_get_json(subdir_api, token);
    if (!sub_list_opt.has_value()) {
      std::cerr << "Could not fetch directory docs/" << name << " .\n";
      continue;
    }
    json sub_list = *sub_list_opt;
    if (!sub_list.is_array())
      continue;

    for (auto &file_entry : sub_list) {
      if (!file_entry.is_object())
        continue;
      std::string ftype = file_entry.value("type", "");
      std::string fname = file_entry.value("name", "");
      if (ftype != "file")
        continue;
      if (fname.size() < 3)
        continue;
      if (fname.substr(fname.size() - 3) != ".md")
        continue;

      std::string path = file_entry.value("path", "");
      std::cout << "Processing: " << path << "\n";

      // 2) Get file metadata (contains base64 content and sha)
      std::string file_api = "https://api.github.com/repos/" + user + "/" +
                             repo + "/contents/" + path;
      auto file_json_opt = http_get_json(file_api, token);
      if (!file_json_opt.has_value()) {
        std::cerr << "Could not fetch file metadata: " << path << "\n";
        continue;
      }
      json file_json = *file_json_opt;
      if (!file_json.contains("content")) {
        std::cerr << "No content found for " << path << "\n";
        continue;
      }
      std::string content_b64 = file_json.value("content", "");
      // Remove newlines in base64
      content_b64.erase(std::remove_if(content_b64.begin(), content_b64.end(),
                                       [](unsigned char c) {
                                         return c == '\n' || c == '\r';
                                       }),
                        content_b64.end());
      std::string content_raw = base64_decode(content_b64);

      // 3) Parse Front Matter
      auto header = parse_front_matter(content_raw);
      if (header.empty()) {
        std::cout << "No Front Matter found in " << path << "\n\n";
        continue;
      }

      // 4) Output via println (std::cout)
      // Desired fields: TITLE, DESCRIPTION, AUTHOR, CREATED, LAST_MODIFIED,
      // TARGET_DATE
      auto print_field = [&](const std::string &key) {
        auto it = header.find(key);
        if (it != header.end()) {
          std::cout << key << ": " << it->second << "\n";
        } else {
          std::cout << key << ": " << "(not present)" << "\n";
        }
      };

      print_field("TITLE");
      print_field("DESCRIPTION");
      print_field("AUTHOR");
      print_field("CREATED");
      print_field("LAST_MODIFIED");
      print_field("TARGET_DATE");
      std::cout << "\n";
    }
  }

  std::cout << "All files processed.\n";
  curl_global_cleanup();
  return 0;
}
