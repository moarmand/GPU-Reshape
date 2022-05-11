#include "GenTypes.h"
#include "Types.h"

// Std
#include <vector>
#include <iostream>
#include <sstream>
#include <set>

struct DetourState {
    /// Current set of mapped functions
    std::set<std::string> functions;

    /// Current set of mapped interfaces
    std::set<std::string> interfaces;

    /// All streams
    std::stringstream includes;
    std::stringstream pfn;
    std::stringstream tables;
    std::stringstream offsets;
    std::stringstream typedefs;
    std::stringstream populators;
};

/// Detour a given interface
static bool DetourInterface(const GeneratorInfo &info, DetourState& state, const std::string& key, const nlohmann::json& interface) {
    // For all bases
    for (auto&& base : interface["bases"]) {
        auto baseKey = base.get<std::string>();

        auto&& baseInterface = info.specification["interfaces"][baseKey];

        // Detour base
        if (!DetourInterface(info, state, baseKey, baseInterface)) {
            return false;
        }
    }

    // For all vtable fields
    for (auto&& method : interface["vtable"]) {
        // Get name of function
        auto fieldName = method["name"].get<std::string>();

        // To function type
        std::string pfnName = "PFN_" + key + fieldName;

        // Emit PFN once
        if (!state.functions.contains(pfnName)) {
            state.pfn << "using " << pfnName << " = ";

            // Print return
            if (!PrettyPrintType(state.pfn, method["returnType"])) {
                return false;
            }

            state.pfn << "(*)(";
            state.pfn << key << "* _this";

            for (auto&& param : method["params"]) {
                state.pfn << ", ";

                if (!PrettyPrintParameter(state.pfn, param["type"], param["name"].get<std::string>())) {
                    return false;
                }
            }

            state.pfn << ");\n";
            state.functions.insert(pfnName);
        }

        // Append table and offset members
        state.tables << "\t" << "" << pfnName << " next_" << fieldName << ";\n";
        state.offsets << "\t" << fieldName << ",\n";
    }

    // OK
    return true;
}

/// Detour a given interface
static bool DetourObject(const GeneratorInfo &info, DetourState& state, const std::string& key) {
    // Already detoured?
    if (state.interfaces.contains(key)) {
        return true;
    }

    // Append
    state.interfaces.insert(key);

    // Get interface
    auto&& interface = info.specification["interfaces"][key];

    // Detour all bases
    for (auto&& base : interface["bases"]) {
        DetourObject(info, state, base.get<std::string>());
    }

    // Emit types
    state.tables << "struct " << key << "DetourVTable {\n";
    state.offsets << "enum class " << key << "DetourOffsets : uint32_t {\n";

    // Generate contents
    if (!DetourInterface(info, state, key, interface)) {
        return false;
    }

    state.tables << "};\n\n";
    state.offsets << "};\n\n";

    return true;
}

static void DetourBaseQuery(const GeneratorInfo &info, DetourState& state, const std::string& key, bool top = true) {
    auto&& obj = info.specification["interfaces"][key];

    // Revision table name
    std::string vtblName = key + "DetourVTable";

    // Keep it clean
    if (!top) {
        state.populators << " else ";
    }
    // Copy vtable contents
    state.populators << "if (SUCCEEDED(object->QueryInterface(__uuidof(" << key << "), &_interface))) {\n";
    state.populators << "\t\tstd::memcpy(&out, *(" << vtblName << "**)_interface, sizeof(" << vtblName << "));\n";
    state.populators << "\t\tobject->Release();\n";
    state.populators << "\t}";

    // Detour all bases
    for (auto&& base : obj["bases"]) {
        DetourBaseQuery(info, state, base.get<std::string>(), false);
    }
}

bool Generators::Detour(const GeneratorInfo &info, TemplateEngine &templateEngine) {
    DetourState state;

    // Print includes
    for (auto&& include : info.hooks["files"]) {
        state.includes << "#include <" << include.get<std::string>() << ">\n";
    }

    // Common
    auto&& interfaces = info.specification["interfaces"];
    auto&& objects    = info.hooks["objects"];

    // Generate detours for all hooked objects
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        std::string key = it.key();

        // Get outer revision
        std::string outerRevision = GetOuterRevision(info, key);

        // Detour outer
        if (!DetourObject(info, state, outerRevision)) {
            return false;
        }

        // Typedef
        state.typedefs << "using " << key << "TopDetourVTable = " << outerRevision << "DetourVTable;\n";

        // Populators
        state.populators << "static " << key << "TopDetourVTable PopulateTopDetourVTable(" << key << "* object) {\n";
        state.populators << "\t" << key << "TopDetourVTable out{};\n";
        state.populators << "\n";
        state.populators << "\tvoid* _interface;\n";

        // Detour all bases
        state.populators << "\t";
        DetourBaseQuery(info, state, outerRevision);

        // DOne
        state.populators << "\n";
        state.populators << "\treturn out;\n";
        state.populators << "}\n\n";
    }

    // Replace keys
    templateEngine.Substitute("$INCLUDES", state.includes.str().c_str());
    templateEngine.Substitute("$PFN", state.pfn.str().c_str());
    templateEngine.Substitute("$TABLES", state.tables.str().c_str());
    templateEngine.Substitute("$OFFSETS", state.offsets.str().c_str());
    templateEngine.Substitute("$TYPEDEFS", state.typedefs.str().c_str());
    templateEngine.Substitute("$POPULATORS", state.populators.str().c_str());

    // OK
    return true;
}
