#ifndef COMPANYINFO_HPP
#define COMPANYINFO_HPP

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

using namespace std;

// --- Struct to hold parsed backend info ---
class CompanyInfo {
public:
    string id;
    string name;
    int version = -1;
    bool parse_success = false;
    optional<string> created_on;
    optional<string> closed_on;
    optional<string> tin;
    optional<string> dissolved_on;

    string to_string() const { // Mark method as const as it doesn't modify the object
        std::ostringstream oss; // Use a stringstream for efficient concatenation

        oss << "CompanyInfo {\n"; // Start with a clear indicator and newline
        oss << "  Id: " << id << "\n";
        oss << "  Name: " << name << "\n";
        oss << "  Version: " << version << "\n";
        // Use std::boolalpha to print 'true' or 'false' instead of '1' or '0'
        oss << "  Parse Success: " << std::boolalpha << parse_success << "\n";

        // Check if optional fields have a value before accessing and printing them
        if (created_on) { // Shorthand for created_on.has_value()
            oss << "  Created On: " << *created_on << "\n"; // Shorthand for created_on.value()
        }
        if (closed_on) {
            oss << "  Closed On: " << *closed_on << "\n";
        }
        if (tin) {
            oss << "  TIN: " << *tin << "\n";
        }
        if (dissolved_on) {
            oss << "  Dissolved On: " << *dissolved_on << "\n";
        }

        oss << "}"; // Closing brace

        return oss.str(); // Return the constructed string
    }
};

#endif // COMPANYINFO_HPP