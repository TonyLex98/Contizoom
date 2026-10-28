#include "storage/json_storage.h"
#include "model/canvas.h"
#include "model/node.h"
#include "model/connection.h"
#include "version/version.h"
#include "util/strutil.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <set>

namespace ic {
namespace fs = std::filesystem;

// Escape string for JSON
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

// Unescape JSON string value
static std::string jsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"':  out += '"'; ++i; break;
                case '\\': out += '\\'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 'r':  out += '\r'; ++i; break;
                case 't':  out += '\t'; ++i; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Extract string field from JSON, handling escape sequences
static std::string extractStr(const std::string& json, const std::string& field) {
    std::string search = "\"" + field + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    size_t colon = json.find(':', pos + search.size());
    if (colon == std::string::npos) return "";
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    // Find closing quote, skipping escaped quotes
    size_t q2 = q1 + 1;
    while (q2 < json.size()) {
        if (json[q2] == '\\' && q2 + 1 < json.size()) {
            q2 += 2; // Skip escaped character
        } else if (json[q2] == '"') {
            break;
        } else {
            ++q2;
        }
    }
    if (q2 >= json.size()) return "";
    std::string raw = json.substr(q1 + 1, q2 - q1 - 1);
    return jsonUnescape(raw);
}

// Extract numeric field from JSON object string
static std::string extractNum(const std::string& json, const std::string& field) {
    std::string search = "\"" + field + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    size_t colon = json.find(':', pos + search.size());
    if (colon == std::string::npos) return "";
    size_t start = colon + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n'))
        start++;
    size_t end = start;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != '\n')
        end++;
    return ic::trim(json.substr(start, end - start));
}

// ====================================================================
// Save
// ====================================================================

bool JsonStorage::saveCanvas(const Canvas& canvas, const std::string& path) {
    try {
        fs::create_directories(path);
        fs::create_directories(nodesDir(path));

        // Save each node to its own file
        std::set<std::string> liveNodeFiles;
        for (const auto& [uuid, node] : canvas.allNodes()) {
            saveNode(node.get(), path);
            liveNodeFiles.insert(uuid + ".json");
        }

        for (const auto& entry : fs::directory_iterator(nodesDir(path))) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            if (liveNodeFiles.find(entry.path().filename().string()) == liveNodeFiles.end()) {
                fs::remove(entry.path());
            }
        }

        // Save config.json
        std::ofstream cfg(configPath(path));
        cfg << "{\n";
        cfg << "  \"canvas_name\": \"" << jsonEscape(canvas.canvasName) << "\",\n";
        cfg << "  \"created_at\": " << canvas.createdAt << ",\n";
        cfg << "  \"modified_at\": " << canvas.modifiedAt << ",\n";
        cfg << "  \"viewport\": {\n";
        cfg << "    \"centerX\": " << canvas.viewport_.centerX << ",\n";
        cfg << "    \"centerY\": " << canvas.viewport_.centerY << ",\n";
        cfg << "    \"zoomLevel\": " << canvas.viewport_.zoomLevel << ",\n";
        cfg << "    \"textScale\": " << canvas.viewport_.textScale << "\n";
        cfg << "  },\n";
        cfg << "  \"connections\": [\n";
        bool first = true;
        for (const auto& [uuid, conn] : canvas.allConnections()) {
            if (!first) cfg << ",\n";
            first = false;
            cfg << "    {\n";
            cfg << "      \"uuid\": \"" << conn->uuid << "\",\n";
            cfg << "      \"name\": \"" << jsonEscape(conn->name) << "\",\n";
            cfg << "      \"fromUuid\": \"" << conn->fromUuid << "\",\n";
            cfg << "      \"toUuid\": \"" << conn->toUuid << "\",\n";
            cfg << "      \"type\": " << static_cast<int>(conn->type) << ",\n";
            cfg << "      \"fromAnchorEdge\": " << conn->fromAnchorEdge << ",\n";
            cfg << "      \"fromAnchorIndex\": " << conn->fromAnchorIndex << ",\n";
            cfg << "      \"toAnchorEdge\": " << conn->toAnchorEdge << ",\n";
            cfg << "      \"toAnchorIndex\": " << conn->toAnchorIndex << "\n";
            cfg << "    }";
        }
        cfg << "\n  ]\n";
        cfg << "}\n";

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving canvas: " << e.what() << "\n";
        return false;
    }
}

bool JsonStorage::saveNode(const Node* node, const std::string& canvasPath) {
    try {
        fs::create_directories(nodesDir(canvasPath));
        std::string fpath = nodeFilePath(canvasPath, node->uuid);
        std::ofstream f(fpath);
        if (!f.is_open()) return false;

        f << "{\n";
        f << "  \"uuid\": \"" << node->uuid << "\",\n";
        f << "  \"name\": \"" << jsonEscape(node->name) << "\",\n";
        f << "  \"text\": \"" << jsonEscape(node->text) << "\",\n";
        f << "  \"parentId\": \"" << node->parentId << "\",\n";
        f << "  \"x\": " << node->rect.x << ",\n";
        f << "  \"y\": " << node->rect.y << ",\n";
        f << "  \"width\": " << node->rect.width << ",\n";
        f << "  \"height\": " << node->rect.height << ",\n";
        f << "  \"zoomLevel\": " << node->zoomLevel << ",\n";
        f << "  \"tag\": \"" << jsonEscape(node->tag) << "\",\n";
        f << "  \"hasFrame\": " << (node->hasFrame ? "true" : "false") << ",\n";
        f << "  \"createdAt\": " << node->createdAt << ",\n";
        f << "  \"modifiedAt\": " << node->modifiedAt << "\n";
        f << "}\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving node " << node->uuid << ": " << e.what() << "\n";
        return false;
    }
}

// ====================================================================
// Load
// ====================================================================

bool JsonStorage::loadCanvas(Canvas& canvas, const std::string& path) {
    try {
        // Load config.json
        std::ifstream cfg(configPath(path));
        if (!cfg.is_open()) {
            std::cerr << "Cannot open config: " << configPath(path) << "\n";
            return false;
        }
        std::string cfgContent((std::istreambuf_iterator<char>(cfg)),
                                std::istreambuf_iterator<char>());

        canvas.clear();

        canvas.canvasName = extractStr(cfgContent, "canvas_name");
        std::string createdAt = extractNum(cfgContent, "created_at");
        if (!createdAt.empty()) canvas.createdAt = std::stoull(createdAt);
        std::string modifiedAt = extractNum(cfgContent, "modified_at");
        if (!modifiedAt.empty()) canvas.modifiedAt = std::stoull(modifiedAt);

        // Extract viewport
        {
            std::string vp = extractStr(cfgContent, "viewport");
            size_t vpStart = cfgContent.find("\"viewport\"");
            if (vpStart != std::string::npos) {
                size_t brace = cfgContent.find('{', vpStart);
                size_t endBrace = cfgContent.find('}', brace);
                if (brace != std::string::npos && endBrace != std::string::npos) {
                    std::string vpStr = cfgContent.substr(brace, endBrace - brace + 1);
                    std::string cx = extractNum(vpStr, "centerX");
                    if (!cx.empty()) canvas.viewport_.centerX = std::stod(cx);
                    std::string cy = extractNum(vpStr, "centerY");
                    if (!cy.empty()) canvas.viewport_.centerY = std::stod(cy);
                    std::string zl = extractNum(vpStr, "zoomLevel");
                    if (!zl.empty()) canvas.viewport_.zoomLevel = std::stoi(zl);
                    std::string ts = extractNum(vpStr, "textScale");
                    if (!ts.empty()) canvas.viewport_.textScale = std::stod(ts);
                }
            }
        }

        // Load all node files from nodes/ directory (before connections)
        std::string nd = nodesDir(path);
        if (fs::exists(nd)) {
            for (const auto& entry : fs::directory_iterator(nd)) {
                std::string fn = entry.path().filename().string();
                if (fn.size() < 5 || fn.substr(fn.size() - 5) != ".json") continue;

                std::ifstream nf(entry.path());
                if (!nf.is_open()) continue;
                std::string content((std::istreambuf_iterator<char>(nf)),
                                     std::istreambuf_iterator<char>());

                std::string uuid = extractStr(content, "uuid");
                if (uuid.empty()) continue;

                std::string text = extractStr(content, "text");
                std::string name = extractStr(content, "name");
                std::string parentId = extractStr(content, "parentId");

                Rect rect;
                {
                    std::string x = extractNum(content, "x");
                    if (!x.empty()) rect.x = std::stod(x);
                    std::string y = extractNum(content, "y");
                    if (!y.empty()) rect.y = std::stod(y);
                    std::string w = extractNum(content, "width");
                    if (!w.empty()) rect.width = std::stod(w);
                    std::string h = extractNum(content, "height");
                    if (!h.empty()) rect.height = std::stod(h);
                }

                int level = 0;
                {
                    std::string zl = extractNum(content, "zoomLevel");
                    if (!zl.empty()) level = std::stoi(zl);
                }

                uint64_t createdAtVal = 0;
                {
                    std::string ca = extractNum(content, "createdAt");
                    if (!ca.empty()) createdAtVal = std::stoull(ca);
                }
                uint64_t modifiedAtVal = 0;
                {
                    std::string ma = extractNum(content, "modifiedAt");
                    if (!ma.empty()) modifiedAtVal = std::stoull(ma);
                }

                bool hasFrame = false;
                {
                    std::string hf = extractNum(content, "hasFrame");
                    hasFrame = (hf == "true");
                }
                std::string tag = extractStr(content, "tag");

                auto node = std::make_shared<Node>(uuid, text, rect, level);
                node->name = name;
                node->parentId = parentId;
                node->createdAt = createdAtVal;
                node->modifiedAt = modifiedAtVal;
                node->hasFrame = hasFrame;
                node->tag = tag;

                canvas.insertNode(uuid, node);
            }
        }
        canvas.rebuildHierarchy();

        // Load connections from config.json (after nodes)
        {
            size_t connArr = cfgContent.find("\"connections\"");
            if (connArr != std::string::npos) {
                size_t bracket = cfgContent.find('[', connArr);
                size_t endBracket = cfgContent.find(']', bracket);
                if (bracket != std::string::npos && endBracket != std::string::npos) {
                    std::string connSection = cfgContent.substr(bracket, endBracket - bracket + 1);
                    size_t pos = 0;
                    while ((pos = connSection.find('{', pos)) != std::string::npos) {
                        size_t endBrace = connSection.find('}', pos);
                        if (endBrace == std::string::npos) break;
                        std::string connStr = connSection.substr(pos, endBrace - pos + 1);
                        std::string cFrom = extractStr(connStr, "fromUuid");
                        std::string cTo = extractStr(connStr, "toUuid");
                        std::string cName = extractStr(connStr, "name");
                        std::string cType = extractNum(connStr, "type");
                        if (!cFrom.empty() && !cTo.empty()) {
                            ConnType type = ConnType::Arrow;
                            if (!cType.empty()) type = static_cast<ConnType>(std::stoi(cType));
                            std::string cUuid = extractStr(connStr, "uuid");
                            if (cUuid.empty()) cUuid = cFrom + "-" + cTo;
                            auto conn = std::make_shared<Connection>(cUuid, cFrom, cTo, type);
                            conn->name = cName;
                            auto readAnchor = [&](const char* key) {
                                std::string v = extractNum(connStr, key);
                                return v.empty() ? -1 : std::stoi(v);
                            };
                            conn->fromAnchorEdge = readAnchor("fromAnchorEdge");
                            conn->fromAnchorIndex = readAnchor("fromAnchorIndex");
                            conn->toAnchorEdge = readAnchor("toAnchorEdge");
                            conn->toAnchorIndex = readAnchor("toAnchorIndex");
                            canvas.insertConnection(cUuid, conn);
                        }
                        pos = endBrace + 1;
                    }
                }
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading canvas: " << e.what() << "\n";
        return false;
    }
}

std::pair<int, int> JsonStorage::getFormatVersion(const std::string& path) const {
    std::ifstream cfg(configPath(path));
    return cfg.is_open() ? std::make_pair(1, 0) : std::make_pair(0, 0);
}

bool JsonStorage::isValid(const std::string& path) const {
    return fs::exists(path) && fs::is_directory(path) && fs::exists(configPath(path));
}

std::vector<std::string> JsonStorage::listCanvases(const std::string& dir) const {
    std::vector<std::string> result;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return result;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_directory() && isValid(entry.path().string())) {
            result.push_back(entry.path().string());
        }
    }
    return result;
}

std::string JsonStorage::nodesDir(const std::string& canvasPath) {
    return canvasPath + "/nodes";
}

std::string JsonStorage::nodeFilePath(const std::string& canvasPath, const std::string& uuid) {
    return nodesDir(canvasPath) + "/" + uuid + ".json";
}

std::string JsonStorage::configPath(const std::string& canvasPath) {
    return canvasPath + "/config.json";
}

} // namespace ic
