#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <iomanip>

// Simple JSON builder without external dependencies
class JSONBuilder {
public:
    JSONBuilder() {}

    void add(const std::string& key, long long value) {
        items_[key] = std::to_string(value);
    }

    void add(const std::string& key, double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        items_[key] = oss.str();
    }

    void add(const std::string& key, const std::string& value) {
        items_[key] = "\"" + value + "\"";
    }

    void add(const std::string& key, bool value) {
        items_[key] = value ? "true" : "false";
    }

    std::string build() {
        std::ostringstream oss;
        oss << "{\n";
        bool first = true;
        for (const auto& p : items_) {
            if (!first) oss << ",\n";
            oss << "  \"" << p.first << "\": " << p.second;
            first = false;
        }
        oss << "\n}";
        return oss.str();
    }

private:
    std::map<std::string, std::string> items_;
};

#endif // JSON_UTILS_H
