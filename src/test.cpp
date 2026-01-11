#include <curl/curl.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

// ------------------------------------------------------------
// Base64 (einfach, ausreichend für GitHub API)
// ------------------------------------------------------------
static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                        "abcdefghijklmnopqrstuvwxyz"
                                        "0123456789+/";

std::string base64_encode(const std::string &in) {
  std::string out;
  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(base64_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6)
    out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4)
    out.push_back('=');
  return out;
}

// ------------------------------------------------------------
// HTTP Helper
// ------------------------------------------------------------
static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            std::string *s) {
  s->append((char *)contents, size * nmemb);
  return size * nmemb;
}

std::string http_get_raw(const std::string &url,
                         const std::string &token = "") {
  CURL *curl = curl_easy_init();
  std::string response;

  struct curl_slist *headers = nullptr;
  if (!token.empty()) {
    headers =
        curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "C++ GitHub Client");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  if (headers)
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  return response;
}

json http_get_json(const std::string &url, const std::string &token = "") {
  return json::parse(http_get_raw(url, token));
}

json http_put_json(const std::string &url, const json &body,
                   const std::string &token) {
  CURL *curl = curl_easy_init();
  std::string response;

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers =
      curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());

  std::string bodyStr = body.dump();

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  return json::parse(response);
}

// ------------------------------------------------------------
// Hauptprogramm
// ------------------------------------------------------------
int main() {
  const std::string user = "<USER>";
  const std::string repo = "<REPO>";
  const std::string branch = "main";
  const std::string token = "<GITHUB_TOKEN>";

  const std::string api_base =
      "https://api.github.com/repos/" + user + "/" + repo + "/contents/docs";

  // --------------------------------------------------------
  // 1. Liste aller Dateien in docs/
  // --------------------------------------------------------
  json files = http_get_json(api_base, token);

  for (auto &f : files) {
    if (f["type"] != "file")
      continue;
    if (!f["name"].get<std::string>().ends_with(".md"))
      continue;

    std::string path = f["path"];
    std::string sha = f["sha"];
    std::string download_url = f["download_url"];

    std::cout << "Bearbeite: " << path << "\n";

    // ----------------------------------------------------
    // 2. Dateiinhalt herunterladen
    // ----------------------------------------------------
    std::string content_raw = http_get_raw(download_url, token);

    // ----------------------------------------------------
    // 3. Inhalt verändern
    // ----------------------------------------------------
    std::string newContent = content_raw + "\n\n<!-- Updated by C++ bot -->\n";

    // ----------------------------------------------------
    // 4. Datei zurückschreiben
    // ----------------------------------------------------
    json body;
    body["message"] = "Automatisches Update von " + path;
    body["content"] = base64_encode(newContent);
    body["sha"] = sha;
    body["branch"] = branch;

    std::string put_url = "https://api.github.com/repos/" + user + "/" + repo +
                          "/contents/" + path;

    json result = http_put_json(put_url, body, token);

    std::cout << "→ Commit: " << result["commit"]["sha"] << "\n\n";
  }

  std::cout << "Fertig.\n";
  return 0;
}
