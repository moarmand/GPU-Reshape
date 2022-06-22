#include <ShaderExportGenerator.h>

#include <Backend/ShaderExport.h>
#include <sstream>
#include <iostream>

bool ShaderExportGenerator::Generate(Schema &schema, Language language, SchemaStream &out) {
    for (Message& message : schema.messages) {
        // Append shader guid if not disabled
        if (!message.attributes.GetBool("no-sguid")) {
            Field& sguid = *message.fields.emplace(message.fields.begin());
            sguid.name = "sguid";
            sguid.type = "uint16";

            // Attributes
            sguid.attributes.Add("bits", std::to_string(kShaderSGUIDBitCount));
        }
    }

    // Include emitter
    if (language == Language::CPP) {
        out.header << "#include <Backend/IL/Emitter.h>\n";
    }

    // OK
    return true;
}

bool ShaderExportGenerator::Generate(const Message &message, Language language, MessageStream &out) {
    switch (language) {
        case Language::CPP:
            return GenerateCPP(message, out);
        case Language::CS:
            return GenerateCS(message, out);
    }

    return false;
}

bool ShaderExportGenerator::GenerateCPP(const Message &message, MessageStream &out) {
    // Begin shader type
    out.types << "\tstruct ShaderExport {\n";

    // SGUID?
    bool noSGUID = message.attributes.GetBool("no-sguid");
    out.types << "\t\tstatic constexpr bool kNoSGUID = " << (noSGUID ? "true" : "false") << ";\n";

    // Structured?
    bool structured = message.attributes.GetBool("structured");
    out.types << "\t\tstatic constexpr bool kStructured = " << (structured ? "true" : "false") << ";\n\n";

    // Begin construction function
    out.types << "\t\ttemplate<typename OP>\n";
    out.types << "\t\tIL::ID Construct(IL::Emitter<OP>& emitter) const {\n";

    // Simple write?
    if (!structured) {
        // Allocate type
        out.types << "\t\t\tIL::ID value = emitter.UInt(32, 0);\n";

        // Current offset
        uint32_t bitOffset = 0;

        // Append all fields
        for (const Field& field : message.fields) {
            auto it = primitiveTypeMap.types.find(field.type);
            if (it == primitiveTypeMap.types.end()) {
                std::cerr << "Malformed command in line: " << message.line << ", type " << field.type << " not supported for non structured writes" << std::endl;
                return false;
            }

            // Optional bit size
            auto bits = field.attributes.Get("bits");

            // Determine the size of this field
            uint32_t bitSize = bits ? std::atoi(bits->value.c_str()) : (static_cast<uint32_t>(it->second.size) * 8);

            // Append value
            out.types << "\t\t\tvalue = emitter.BitOr(value, emitter.BitShiftLeft(" << field.name << ", emitter.UInt(32, " << bitOffset << ")));\n";

            // Next
            bitOffset += bitSize;
        }

        // Check non structured write limit
        if (bitOffset > 32) {
            std::cerr << "Malformed command in line: " << message.line << ", non structured size exceeded 32 bits with " << bitOffset << " bits" << std::endl;
            return false;
        }

        // Done!
        out.types << "\t\t\treturn value;\n";
    } else {
        // Soon (tm)
        std::cerr << "Malformed command in line: " << message.line << ", structured writes not supported yet" << std::endl;
        return false;
    }

    // End construction function
    out.types << "\t\t}\n\n";

    // Begin shader values
    for (const Field& field : message.fields) {
        out.types << "\t\tIL::ID " << field.name << "{IL::InvalidID};\n";
    }

    // End shader type
    out.types << "\t};\n\n";

    // Add caster and creator for non structured types
    if (!structured) {
        out.types << "\tuint32_t GetKey() const {\n";
        out.types << "\t\tunion {\n";
        out.types << "\t\t\tuint32_t key;\n";
        out.types << "\t\t\t" << message.name << "Message message;\n";
        out.types << "\t\t} u = {.message = *this};\n";
        out.types << "\t\treturn u.key;\n";
        out.types << "\t}\n";

        out.types << "\tstatic " << message.name << "Message FromKey(uint32_t key) {\n";
        out.types << "\t\tunion {\n";
        out.types << "\t\t\tuint32_t key;\n";
        out.types << "\t\t\t" << message.name << "Message message;\n";
        out.types << "\t\t} u = {.key = key};\n";
        out.types << "\t\treturn u.message;\n";
        out.types << "\t}\n";
    }

    // OK
    return true;
}

bool ShaderExportGenerator::GenerateCS(const Message &message, MessageStream &out) {
    // Structured?
    bool structured = message.attributes.GetBool("structured");
    out.types << "\t\tpublic const bool IsStructured = " << (structured ? "true" : "false") << ";\n\n";

    // Add getter and setter for non-structured types (i.e. single uint)
    if (!structured) {
        out.types << "\t\tpublic uint Key\n";
        out.types << "\t\t{\n";
        out.types << "\t\t\t[MethodImpl(MethodImplOptions.AggressiveInlining)]\n";
        out.types << "\t\t\tget => MemoryMarshal.Read<uint>(_memory.Slice(0, 4).AsRefSpan());\n\n";
        out.types << "\t\t\t[MethodImpl(MethodImplOptions.AggressiveInlining)]\n";
        out.types << "\t\t\tset => MemoryMarshal.Write<uint>(_memory.Slice(0, 4).AsRefSpan(), ref value);\n";
        out.types << "\t\t}\n";
    }

    return true;
}
