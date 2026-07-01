#include "storage/json_storage.h"
#include "model/canvas.h"
#include "model/node.h"
#include "version/version.h"
#include "util/strutil.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace ic {
namespace fs = std::filesystem;

bool JsonStorage::saveCanvas(const Canvas& canvas, const std::string& path) {
    try {
        // Create directory structure
        fs::create_directories(path);
        fs::create_directories(nodesDir(path));

        // Save manifest
        {
            std::ofstream mf(manifestPath(path));
            mf << "{\n";
            mf << "  \"format_version\": \"" << IC_STORAGE_FORMAT_STRING << "\",\n";
            mf << "  \"app_version\": \"" << IC_VERSION_STRING << "\",\n";
            mf << "  \"canvas_name\": \"" << canvas.canvasName << "\",\n";
            mf << "  \"created_at\": " << canvas.createdAt << ",\n";
            mf << "  \"modified_at\": " << canvas.modifiedAt << "\n";
            mf << "}\n";
        }

        // Save nodes as text files
        for (const auto& [uuid, node] : canvas.allNodes()) {
            std::ofstream nf(nodeTextPath(path, uuid));
            nf << node->text;
        }

        // Save index
        {
            std::ofstream idx(fs::path(path) / "index.json");
            idx << "{\n";
            idx << "  \"format_version\": \"" << IC_STORAGE_FORMAT_STRING << "\",\n";

            // Viewport
            idx << "  \"viewport\": {\n";
            idx << "    \"centerX\": " << canvas.viewport_.centerX << ",\n";
            idx << "    \"centerY\": " << canvas.viewport_.centerY << ",\n";
            idx << "    \"zoomLevel\": " << canvas.viewport_.zoomLevel << ",\n";
            idx << "    \"textScale\": " << canvas.viewport_.textScale << "\n";
            idx << "  },\n";

            // Nodes
            idx << "  \"nodes\": [\n";
            bool first = true;
            for (const auto& [uuid, node] : canvas.allNodes()) {
                if (!first) idx << ",\n";
                first = false;

                idx << "    {\n";
                idx << "      \"uuid\": \"" << uuid << "\",\n";
                idx << "      \"name\": \"" << node->name << "\",\n";
                idx << "      \"parentId\": \"" << node->parentId << "\",\n";
                idx << "      \"x\": " << node->rect.x << ",\n";
                idx << "      \"y\": " << node->rect.y << ",\n";
                idx << "      \"width\": " << node->rect.width << ",\n";
                idx << "      \"height\": " << node->rect.height << ",\n";
                idx << "      \"zoomLevel\": " << node->zoomLevel << ",\n";
                idx << "      \"tag\": \"" << node->tag << "\",\n";
                idx << "      \"hasFrame\": " << (node->hasFrame ? "true" : "false") << ",\n";
                idx << "      \"createdAt\": " << node->createdAt << ",\n";
                idx << "      \"modifiedAt\": " << node->modifiedAt << "\n";
                idx << "    }";
            }
            idx << "\n  ],\n";

            // Connections
            idx << "  \"connections\": [\n";
            first = true;
            for (const auto& [uuid, conn] : canvas.allConnections()) {
                if (!first) idx << ",\n";
                first = false;

                idx << "    {\n";
                idx << "      \"uuid\": \"" << conn->uuid << "\",\n";
                idx << "      \"name\": \"" << conn->name << "\",\n";
                idx << "      \"fromUuid\": \"" << conn->fromUuid << "\",\n";
                idx << "      \"toUuid\": \"" << conn->toUuid << "\",\n";
                idx << "      \"label\": \"" << conn->label << "\",\n";
                idx << "      \"type\": \"" << static_cast<int>(conn->type) << "\",\n";
                idx << "      \"color\": \"" << static_cast<int>(conn->color) << "\",\n";
                idx << "      \"fromOffsetX\": " << conn->fromOffsetX << ",\n";
                idx << "      \"fromOffsetY\": " << conn->fromOffsetY << ",\n";
                idx << "      \"toOffsetX\": " << conn->toOffsetX << ",\n";
                idx << "      \"toOffsetY\": " << conn->toOffsetY << "\n";
                idx << "    }";
            }
            idx << "\n  ]\n";
            idx << "}\n";
        }

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving canvas: " << e.what() << "\n";
        return false;
    }
}

bool JsonStorage::loadCanvas(Canvas& canvas, const std::string& path) {
    try {
        // Check format version
        auto version = getFormatVersion(path);
        if (version == std::make_pair(0, 0)) return false;

        // Check if migration is needed
        auto currentVersion = std::make_pair(IC_STORAGE_FORMAT_MAJOR, IC_STORAGE_FORMAT_MINOR);
        if (version < currentVersion) {
            if (!migrateToCurrent(path, version)) {
                std::cerr << "Failed to migrate from format "
                          << version.first << "." << version.second << "\n";
                return false;
            }
        }

        // Load manifest
        {
            std::ifstream mf(manifestPath(path));
            // For now, we parse manually since we control the format
            // In production, use nlohmann/json or similar
            std::string line;
            while (std::getline(mf, line)) {
                // Parse canvas_name
                if (line.find("canvas_name") != std::string::npos) {
                    size_t q1 = line.find('"', line.find(':') + 1);
                    size_t q2 = line.find('"', q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        canvas.canvasName = line.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
        }

        // Load index
        {
            std::ifstream idx(fs::path(path) / "index.json");
            std::string content((std::istreambuf_iterator<char>(idx)),
                                 std::istreambuf_iterator<char>());

            // Simple manual parsing for now
            // Parse viewport
            size_t vpPos = content.find("\"viewport\"");
            if (vpPos != std::string::npos) {
                size_t cxPos = content.find("\"centerX\"", vpPos);
                if (cxPos != std::string::npos) {
                    size_t colon = content.find(':', cxPos);
                    if (colon != std::string::npos) {
                        size_t comma = content.find(',', colon + 1);
                        if (comma != std::string::npos) {
                            std::string cxStr = content.substr(colon + 1, comma - colon - 1);
                            canvas.viewport_.centerX = std::stod(cxStr);
                        }
                    }
                }

                // Similar for other viewport fields
                size_t cyPos = content.find("\"centerY\"", vpPos);
                if (cyPos != std::string::npos) {
                    size_t colon = content.find(':', cyPos);
                    if (colon != std::string::npos) {
                        size_t comma = content.find(',', colon + 1);
                        if (comma != std::string::npos) {
                            std::string cyStr = content.substr(colon + 1, comma - colon - 1);
                            canvas.viewport_.centerY = std::stod(cyStr);
                        }
                    }
                }

                size_t zlPos = content.find("\"zoomLevel\"", vpPos);
                if (zlPos != std::string::npos) {
                    size_t colon = content.find(':', zlPos);
                    if (colon != std::string::npos) {
                        size_t comma = content.find(',', colon + 1);
                        if (comma != std::string::npos) {
                            std::string zlStr = content.substr(colon + 1, comma - colon - 1);
                            canvas.viewport_.zoomLevel = std::stoi(zlStr);
                        }
                    }
                }
            }

            // Parse nodes
            size_t nodesPos = content.find("\"nodes\"");
            if (nodesPos != std::string::npos) {
                // Find each node object
                size_t pos = nodesPos;
                while ((pos = content.find("{", pos)) != std::string::npos) {
                    size_t endBrace = content.find("}", pos);
                    if (endBrace == std::string::npos) break;

                    std::string nodeStr = content.substr(pos, endBrace - pos + 1);

                    // Extract fields
                    auto extractField = [&](const std::string& field,
                                           const std::string& s) -> std::string {
                        size_t fp = s.find("\"" + field + "\"");
                        if (fp == std::string::npos) return "";
                        size_t colon = s.find(':', fp);
                        if (colon == std::string::npos) return "";
                        size_t q1 = s.find('"', colon);
                        if (q1 == std::string::npos) {
                            // Numeric value
                            size_t comma = s.find(',', colon + 1);
                            size_t brace = s.find('}', colon + 1);
                            size_t end = (comma != std::string::npos && brace != std::string::npos)
                                          ? std::min(comma, brace) : brace;
                            if (end == std::string::npos) return "";
                            return ic::trim(s.substr(colon + 1, end - colon - 1));
                        }
                        size_t q2 = s.find('"', q1 + 1);
                        if (q2 == std::string::npos) return "";
                        return s.substr(q1 + 1, q2 - q1 - 1);
                    };

                    // For simplicity, create nodes and load text
                    std::string uuid = extractField("uuid", nodeStr);
                    if (uuid.empty()) { pos = endBrace + 1; continue; }

                    Rect rect;
                    std::string xStr = extractField("x", nodeStr);
                    std::string yStr = extractField("y", nodeStr);
                    std::string wStr = extractField("width", nodeStr);
                    std::string hStr = extractField("height", nodeStr);
                    if (!xStr.empty()) rect.x = std::stod(xStr);
                    if (!yStr.empty()) rect.y = std::stod(yStr);
                    if (!wStr.empty()) rect.width = std::stod(wStr);
                    if (!hStr.empty()) rect.height = std::stod(hStr);

                    std::string text = loadNodeText(path, uuid);
                    std::string name = extractField("name", nodeStr);
                    std::string parentId = extractField("parentId", nodeStr);
                    std::string levelStr = extractField("zoomLevel", nodeStr);
                    int level = 0;
                    if (!levelStr.empty()) level = std::stoi(levelStr);

                    canvas.createNode(text, rect, name, parentId, level);

                    pos = endBrace + 1;
                }
            }
        }

        // Apply incremental .meta files on top of loaded nodes
        {
            std::string nd = nodesDir(path);
            if (fs::exists(nd)) {
                for (const auto& e : fs::directory_iterator(nd)) {
                    std::string fn = e.path().filename().u8string();
                    if (fn.size() > 5 && fn.substr(fn.size() - 5) == ".meta") {
                        // Parse simple JSON for x, y, name
                        std::ifstream mf(e.path());
                        std::string mc((std::istreambuf_iterator<char>(mf)),
                                       std::istreambuf_iterator<char>());
                        auto extract = [&](const std::string& field) -> std::string {
                            size_t fp = mc.find("\"" + field + "\"");
                            if (fp == std::string::npos) return "";
                            size_t colon = mc.find(':', fp);
                            if (colon == std::string::npos) return "";
                            size_t q1 = mc.find('"', colon);
                            if (q1 == std::string::npos) {
                                size_t comma = mc.find(',', colon + 1);
                                size_t brace = mc.find('}', colon + 1);
                                size_t end = (comma != std::string::npos && brace != std::string::npos)
                                              ? std::min(comma, brace) : brace;
                                if (end == std::string::npos) return "";
                                return ic::trim(mc.substr(colon + 1, end - colon - 1));
                            }
                            size_t q2 = mc.find('"', q1 + 1);
                            if (q2 == std::string::npos) return "";
                            return mc.substr(q1 + 1, q2 - q1 - 1);
                        };
                        std::string uuid = extract("uuid");
                        if (uuid.empty()) continue;
                        auto node = canvas.getNode(uuid);
                        if (!node) continue;
                        std::string xStr = extract("x");
                        std::string yStr = extract("y");
                        std::string wStr = extract("width");
                        std::string hStr = extract("height");
                        if (!xStr.empty()) node->rect.x = std::stod(xStr);
                        if (!yStr.empty()) node->rect.y = std::stod(yStr);
                        if (!wStr.empty()) node->rect.width = std::stod(wStr);
                        if (!hStr.empty()) node->rect.height = std::stod(hStr);
                        std::string mName = extract("name");
                        if (!mName.empty()) node->name = mName;
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
    std::ifstream mf(manifestPath(path));
    if (!mf.is_open()) return {0, 0};

    std::string line;
    while (std::getline(mf, line)) {
        if (line.find("format_version") != std::string::npos) {
            size_t q1 = line.find('"', line.find(':') + 1);
            size_t q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                std::string verStr = line.substr(q1 + 1, q2 - q1 - 1);
                return parseVersion(verStr);
            }
        }
    }
    return {0, 0};
}

bool JsonStorage::isValid(const std::string& path) const {
    return fs::exists(path) &&
           fs::is_directory(path) &&
           fs::exists(manifestPath(path)) &&
           fs::exists(fs::path(path) / "index.json");
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

bool JsonStorage::saveNodeText(const std::string& canvasPath,
                                const std::string& uuid,
                                const std::string& text) {
    std::ofstream nf(nodeTextPath(canvasPath, uuid));
    if (!nf.is_open()) return false;
    nf << text;
    return true;
}

std::string JsonStorage::loadNodeText(const std::string& canvasPath,
                                       const std::string& uuid) const {
    std::ifstream nf(nodeTextPath(canvasPath, uuid));
    if (!nf.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(nf)),
                       std::istreambuf_iterator<char>());
}

bool JsonStorage::migrateToCurrent(const std::string& path,
                                    std::pair<int, int> fromVersion) {
    // Migration from 1.0 -> 1.0 is a no-op
    // Future migrations will go here:
    // if (fromVersion == std::make_pair(1, 0)) {
    //     return migrate_1_0_to_1_1(path);
    // }
    // if (fromVersion == std::make_pair(1, 1)) {
    //     return migrate_1_1_to_2_0(path);
    // }
    return true;
}

std::pair<int, int> JsonStorage::parseVersion(const std::string& str) {
    size_t dot = str.find('.');
    if (dot == std::string::npos) {
        return {std::stoi(str), 0};
    }
    return {std::stoi(str.substr(0, dot)),
            std::stoi(str.substr(dot + 1))};
}

std::string JsonStorage::nodesDir(const std::string& canvasPath) {
    return canvasPath + "/nodes";
}

std::string JsonStorage::nodeTextPath(const std::string& canvasPath,
                                       const std::string& uuid) {
    return nodesDir(canvasPath) + "/" + uuid + ".txt";
}

std::string JsonStorage::manifestPath(const std::string& canvasPath) {
    return canvasPath + "/manifest.json";
}

bool JsonStorage::saveNode(const Node* node, const std::string& canvasPath) {
    try {
        fs::create_directories(nodesDir(canvasPath));
        std::ofstream nf(nodeTextPath(canvasPath, node->uuid) + ".meta");
        if (!nf.is_open()) return false;
        nf << "{\n";
        nf << "  \"uuid\": \"" << node->uuid << "\",\n";
        nf << "  \"name\": \"" << node->name << "\",\n";
        nf << "  \"parentId\": \"" << node->parentId << "\",\n";
        nf << "  \"x\": " << node->rect.x << ",\n";
        nf << "  \"y\": " << node->rect.y << ",\n";
        nf << "  \"width\": " << node->rect.width << ",\n";
        nf << "  \"height\": " << node->rect.height << ",\n";
        nf << "  \"zoomLevel\": " << node->zoomLevel << ",\n";
        nf << "  \"tag\": \"" << node->tag << "\",\n";
        nf << "  \"hasFrame\": " << (node->hasFrame ? "true" : "false") << "\n";
        nf << "}\n";

        // Also update text
        std::ofstream tf(nodeTextPath(canvasPath, node->uuid));
        if (tf.is_open()) tf << node->text;

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving node: " << e.what() << "\n";
        return false;
    }
}

} // namespace ic
