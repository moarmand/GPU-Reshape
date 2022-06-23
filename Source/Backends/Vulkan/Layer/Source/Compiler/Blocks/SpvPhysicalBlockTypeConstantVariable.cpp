#include <Backends/Vulkan/Compiler/Blocks/SpvPhysicalBlockTypeConstantVariable.h>
#include <Backends/Vulkan/Compiler/SpvPhysicalBlockTable.h>
#include <Backends/Vulkan/Compiler/SpvParseContext.h>

SpvPhysicalBlockTypeConstantVariable::SpvPhysicalBlockTypeConstantVariable(const Allocators &allocators, IL::Program &program, SpvPhysicalBlockTable &table) :
   SpvPhysicalBlockSection(allocators, program, table),
    typeMap(allocators, &program.GetTypeMap()) {

}

void SpvPhysicalBlockTypeConstantVariable::Parse() {
    block = table.scan.GetPhysicalBlock(SpvPhysicalBlockType::TypeConstantVariable);

    // Parse instructions
    SpvParseContext ctx(block->source);
    while (ctx) {
        // Create type association
        AssignTypeAssociation(ctx);

        // Handle instruction
        switch (ctx->GetOp()) {
            default:
                break;
            case SpvOpTypeInt: {
                Backend::IL::IntType type;
                type.bitWidth = ctx++;
                type.signedness = ctx++;
                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypeVoid: {
                Backend::IL::VoidType type;
                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypeBool: {
                Backend::IL::BoolType type;
                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypeFloat: {
                Backend::IL::FPType type;
                type.bitWidth = ctx++;
                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypeVector: {
                Backend::IL::VectorType type;
                type.containedType = typeMap.GetTypeFromId(ctx++);
                type.dimension = ctx++;
                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypeMatrix: {
                const Backend::IL::Type *columnType = typeMap.GetTypeFromId(ctx++);

                auto *columnVector = columnType->Cast<Backend::IL::VectorType>();
                ASSERT(columnVector, "Column type must be vector");

                Backend::IL::MatrixType type;
                type.containedType = columnVector->containedType;
                type.rows = columnVector->dimension;
                type.columns = ctx++;
                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypePointer: {
                Backend::IL::PointerType type;
                type.addressSpace = Translate(static_cast<SpvStorageClass>(ctx++));
                type.pointee = typeMap.GetTypeFromId(ctx++);
                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypeArray: {
                Backend::IL::ArrayType type;
                type.elementType = typeMap.GetTypeFromId(ctx++);
                type.count = ctx++;

                typeMap.AddType(ctx.GetResult(), type);
                break;
            }

            case SpvOpTypeImage: {
                // Sampled type
                const Backend::IL::Type *sampledType = typeMap.GetTypeFromId(ctx++);

                // Dimension of the image
                SpvDim dim = static_cast<SpvDim>(ctx++);

                // Cap operands
                bool isDepth = ctx++;
                bool isArrayed = ctx++;
                bool multisampled = ctx++;

                // Translate the sampler mode
                Backend::IL::ResourceSamplerMode samplerMode;
                switch (ctx++) {
                    default:
                        ASSERT(false, "Unknown sampler mode");
                        samplerMode = Backend::IL::ResourceSamplerMode::RuntimeOnly;
                        break;
                    case 0:
                        samplerMode = Backend::IL::ResourceSamplerMode::RuntimeOnly;
                        break;
                    case 1:
                        samplerMode = Backend::IL::ResourceSamplerMode::Compatible;
                        break;
                    case 2:
                        samplerMode = Backend::IL::ResourceSamplerMode::Writable;
                        break;
                }

                // Format, if present
                Backend::IL::Format format = Translate(static_cast<SpvImageFormat>(ctx++));

                // Texel buffer?
                if (dim == SpvDimBuffer) {
                    Backend::IL::BufferType type;
                    type.elementType = sampledType;
                    type.texelType = format;
                    type.samplerMode = samplerMode;
                    typeMap.AddType(ctx.GetResult(), type);
                } else {
                    Backend::IL::TextureType type;
                    type.sampledType = sampledType;
                    type.dimension = Translate(dim);

                    if (isArrayed) {
                        switch (type.dimension) {
                            default:
                                type.dimension = Backend::IL::TextureDimension::Unexposed;
                                break;
                            case Backend::IL::TextureDimension::Texture1D:
                                type.dimension = Backend::IL::TextureDimension::Texture1DArray;
                                break;
                            case Backend::IL::TextureDimension::Texture2D:
                                type.dimension = Backend::IL::TextureDimension::Texture2DArray;
                                break;
                        }
                    }

                    type.multisampled = multisampled;
                    type.samplerMode = samplerMode;
                    type.format = format;

                    typeMap.AddType(ctx.GetResult(), type);
                }
                break;
            }

            case SpvOpTypeFunction: {
                Backend::IL::FunctionType function;

                // Return type
                function.returnType = typeMap.GetTypeFromId(ctx++);

                // Parameter types
                while (ctx.HasPendingWords()) {
                    function.parameterTypes.push_back(typeMap.GetTypeFromId(ctx++));
                }

                typeMap.AddType(ctx.GetResult(), function);
                break;
            }
        }

        // Next instruction
        ctx.Next();
    }
}

void SpvPhysicalBlockTypeConstantVariable::AssignTypeAssociation(SpvParseContext &ctx) {
    // If there's an associated type, map it
    if (!ctx.HasResult() || !ctx.HasResultType()) {
        return;
    }

    // Get type, if not found assume unexposed
    const Backend::IL::Type* type = typeMap.GetTypeFromId(ctx.GetResultType());
    if (!type) {
        type = typeMap.AddType(ctx.GetResultType(), Backend::IL::UnexposedType{});
    }

    // Create type -> id mapping
    program.GetTypeMap().SetType(ctx.GetResult(), type);
}

void SpvPhysicalBlockTypeConstantVariable::CopyTo(SpvPhysicalBlockTable& remote, SpvPhysicalBlockTypeConstantVariable &out) {
    out.block = remote.scan.GetPhysicalBlock(SpvPhysicalBlockType::TypeConstantVariable);
    typeMap.CopyTo(out.typeMap);
}

void SpvPhysicalBlockTypeConstantVariable::Compile(SpvIdMap &idMap) {
    // Set the destination declaration stream
    typeMap.SetDeclarationStream(&block->stream);

    // Set the identifier proxy map
    typeMap.SetIdMap(&idMap);
}