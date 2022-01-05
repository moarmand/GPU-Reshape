#include <Backends/Vulkan/Compiler/SpvModule.h>
#include <Backends/Vulkan/Compiler/SpvInstruction.h>
#include <Backends/Vulkan/Compiler/SpvStream.h>
#include <Backends/Vulkan/Compiler/SpvRelocationStream.h>

bool SpvModule::Recompile(const uint32_t *code, uint32_t wordCount, const SpvJob& job) {
    // Get the identifier map
    IL::IdentifierMap& ilIdentifierMap = program->GetIdentifierMap();

    // Compilation relocation stream
    SpvRelocationStream relocationStream(code, wordCount);

    // Get the header
    ProgramHeader jitHeader = header;
    relocationStream.AllocateFixedBlock(IL::SourceSpan{0, IL::WordCount<ProgramHeader>()}, &jitHeader);

    // Set the new number of bound values
    jitHeader.bound = program->GetIdentifierMap().GetMaxID();

    // Set the type counter
    typeMap->SetIdCounter(&jitHeader.bound);

    // Inject the export records
    InsertExportRecords(jitHeader, relocationStream, job);

    // Go through all functions
    for (auto fn = program->begin(); fn != program->end(); fn++) {
        // Create the declaration relocation block
        SpvRelocationBlock &declarationRelocationBlock = relocationStream.AllocateBlock(fn->GetDeclarationSourceSpan());

        // Find all dirty basic blocks
        for (auto bb = fn->begin(); bb != fn->end(); bb++) {
            // If not modified, skip
            if (!bb->IsModified()) {
                continue;
            }

            // Get the source span
            IL::SourceSpan span = bb->GetSourceSpan();

            // Forein block?
            if (span.begin == IL::InvalidOffset) {
                // TODO: What if it's the first, what then?
                auto previous = std::prev(bb);

                // Allocate foreign block right after previous
                span.begin = previous->GetSourceSpan().end;
                span.end = previous->GetSourceSpan().end;

                // Assign for successors
                bb->SetSourceSpan(span);
            }

            // Create a new relocation block
            SpvRelocationBlock &relocationBlock = relocationStream.AllocateBlock(span);

            // Destination stream
            SpvStream &stream = relocationBlock.stream;

            // Emit label
            SpvInstruction& label = stream.Allocate(SpvOpLabel, 2);
            label[1] = bb->GetID();

            // Emit all backend instructions
            for (auto instr = bb->begin(); instr != bb->end(); instr++) {
                // If trivial, just copy it directly
                if (instr->source.TriviallyCopyable()) {
                    auto ptr = instr.Get();
                    stream.Template(instr->source);
                    continue;
                }

                switch (instr->opCode) {
                    default: {
                        ASSERT(false, "Invalid instruction in basic block");
                        return false;
                    }
                    case IL::OpCode::Unexposed: {
                        ASSERT(false, "Non trivially copyable unexposed instruction");
                        break;
                    }
                    case IL::OpCode::Literal: {
                        auto *literal = instr.As<IL::LiteralInstruction>();

                        const SpvType* type{};
                        switch (literal->type) {
                            default:
                            ASSERT(false, "Invalid literal type");
                                return false;
                            case IL::LiteralType::Int: {
                                SpvIntType typeInt;
                                typeInt.bitWidth = literal->bitWidth;
                                typeInt.signedness = literal->signedness;
                                type = typeMap->FindTypeOrAdd(typeInt);
                                break;
                            }
                            case IL::LiteralType::FP: {
                                SpvFPType typeFP;
                                typeFP.bitWidth = literal->bitWidth;
                                type = typeMap->FindTypeOrAdd(typeFP);
                                break;
                            }
                        }

                        SpvInstruction& spv = declarationRelocationBlock.stream.Allocate(SpvOpConstant, 4);
                        spv[1] = type->id;
                        spv[2] = literal->result;
                        spv[3] = literal->value.integral;

                        typeMap->SetType(literal->result, type);
                        break;
                    }
                    case IL::OpCode::LoadTexture:
                    case IL::OpCode::StoreTexture:  {
                        ASSERT(false, "Not implemented");
                        break;
                    }
                    case IL::OpCode::IntType:
                        break;
                    case IL::OpCode::FPType:
                        break;
                    case IL::OpCode::Add: {
                        auto *add = instr.As<IL::AddInstruction>();

                        const SpvType* resultType = typeMap->GetType(add->lhs);
                        typeMap->SetType(add->result, resultType);

                        SpvOp op = resultType->kind == SpvTypeKind::FP ? SpvOpFAdd : SpvOpIAdd;

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, add->source);
                        spv[1] = resultType->id;
                        spv[2] = add->result;
                        spv[3] = add->lhs;
                        spv[4] = add->rhs;
                        break;
                    }
                    case IL::OpCode::Sub: {
                        auto *add = instr.As<IL::SubInstruction>();

                        const SpvType* resultType = typeMap->GetType(add->lhs);
                        typeMap->SetType(add->result, resultType);

                        SpvOp op = resultType->kind == SpvTypeKind::FP ? SpvOpFSub : SpvOpISub;

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, add->source);
                        spv[1] = resultType->id;
                        spv[2] = add->result;
                        spv[3] = add->lhs;
                        spv[4] = add->rhs;
                        break;
                    }
                    case IL::OpCode::Div: {
                        auto *add = instr.As<IL::DivInstruction>();

                        const SpvType* resultType = typeMap->GetType(add->lhs);
                        typeMap->SetType(add->result, resultType);

                        SpvOp op;
                        if (resultType->kind == SpvTypeKind::Int) {
                            op = resultType->As<SpvIntType>()->signedness ? SpvOpSDiv : SpvOpUDiv;
                        } else {
                            op = SpvOpFDiv;
                        }

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, add->source);
                        spv[1] = resultType->id;
                        spv[2] = add->result;
                        spv[3] = add->lhs;
                        spv[4] = add->rhs;
                        break;
                    }
                    case IL::OpCode::Mul: {
                        auto *add = instr.As<IL::MulInstruction>();

                        const SpvType* resultType = typeMap->GetType(add->lhs);
                        typeMap->SetType(add->result, resultType);

                        SpvOp op = resultType->kind == SpvTypeKind::FP ? SpvOpFMul : SpvOpIMul;

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, add->source);
                        spv[1] = resultType->id;
                        spv[2] = add->result;
                        spv[3] = add->lhs;
                        spv[4] = add->rhs;
                        break;
                    }
                    case IL::OpCode::Or: {
                        auto *_or = instr.As<IL::OrInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(_or->result, type);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpLogicalOr, 5, _or->source);
                        spv[1] = type->id;
                        spv[2] = _or->result;
                        spv[3] = _or->lhs;
                        spv[4] = _or->rhs;
                        break;
                    }
                    case IL::OpCode::And: {
                        auto *_and = instr.As<IL::AndInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(_and->result, type);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpLogicalAnd, 5, _and->source);
                        spv[1] = type->id;
                        spv[2] = _and->result;
                        spv[3] = _and->lhs;
                        spv[4] = _and->rhs;
                        break;
                    }
                    case IL::OpCode::Equal: {
                        auto *equal = instr.As<IL::EqualInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(equal->result, type);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpLogicalEqual, 5, equal->source);
                        spv[1] = type->id;
                        spv[2] = equal->result;
                        spv[3] = equal->lhs;
                        spv[4] = equal->rhs;
                        break;
                    }
                    case IL::OpCode::NotEqual: {
                        auto *notEqual = instr.As<IL::NotEqualInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(notEqual->result, type);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpLogicalNotEqual, 5, notEqual->source);
                        spv[1] = type->id;
                        spv[2] = notEqual->result;
                        spv[3] = notEqual->lhs;
                        spv[4] = notEqual->rhs;
                        break;
                    }
                    case IL::OpCode::LessThan: {
                        auto *lessThan = instr.As<IL::LessThanInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(lessThan->result, type);

                        const SpvType* lhsType = typeMap->GetType(lessThan->lhs);

                        SpvOp op;
                        if (lhsType->kind == SpvTypeKind::Int) {
                            op = lhsType->As<SpvIntType>()->signedness ? SpvOpSLessThan : SpvOpULessThan;
                        } else {
                            op = SpvOpFOrdLessThan;
                        }

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, lessThan->source);
                        spv[1] = type->id;
                        spv[2] = lessThan->result;
                        spv[3] = lessThan->lhs;
                        spv[4] = lessThan->rhs;
                        break;
                    }
                    case IL::OpCode::LessThanEqual: {
                        auto *lessThanEqual = instr.As<IL::LessThanEqualInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(lessThanEqual->result, type);

                        const SpvType* lhsType = typeMap->GetType(lessThanEqual->lhs);

                        SpvOp op;
                        if (lhsType->kind == SpvTypeKind::Int) {
                            op = lhsType->As<SpvIntType>()->signedness ? SpvOpSLessThanEqual : SpvOpULessThanEqual;
                        } else {
                            op = SpvOpFOrdLessThanEqual;
                        }

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, lessThanEqual->source);
                        spv[1] = type->id;
                        spv[2] = lessThanEqual->result;
                        spv[3] = lessThanEqual->lhs;
                        spv[4] = lessThanEqual->rhs;
                        break;
                    }
                    case IL::OpCode::GreaterThan: {
                        auto *greaterThan = instr.As<IL::GreaterThanInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(greaterThan->result, type);

                        const SpvType* lhsType = typeMap->GetType(greaterThan->lhs);

                        SpvOp op;
                        if (lhsType->kind == SpvTypeKind::Int) {
                            op = lhsType->As<SpvIntType>()->signedness ? SpvOpSGreaterThan : SpvOpUGreaterThan;
                        } else {
                            op = SpvOpFOrdGreaterThan;
                        }

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, greaterThan->source);
                        spv[1] = type->id;
                        spv[2] = greaterThan->result;
                        spv[3] = greaterThan->lhs;
                        spv[4] = greaterThan->rhs;
                        break;
                    }
                    case IL::OpCode::GreaterThanEqual: {
                        auto *greaterThanEqual = instr.As<IL::GreaterThanEqualInstruction>();

                        const SpvType* type = typeMap->FindTypeOrAdd(SpvBoolType());
                        typeMap->SetType(greaterThanEqual->result, type);

                        const SpvType* lhsType = typeMap->GetType(greaterThanEqual->lhs);

                        SpvOp op;
                        if (lhsType->kind == SpvTypeKind::Int) {
                            op = lhsType->As<SpvIntType>()->signedness ? SpvOpSGreaterThanEqual : SpvOpUGreaterThanEqual;
                        } else {
                            op = SpvOpFOrdGreaterThanEqual;
                        }

                        SpvInstruction& spv = stream.TemplateOrAllocate(op, 5, greaterThanEqual->source);
                        spv[1] = type->id;
                        spv[2] = greaterThanEqual->result;
                        spv[3] = greaterThanEqual->lhs;
                        spv[4] = greaterThanEqual->rhs;
                        break;
                    }
                    case IL::OpCode::Branch: {
                        auto *branch = instr.As<IL::BranchInstruction>();

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpBranch, 2, branch->source);
                        spv[1] = branch->branch;
                        break;
                    }
                    case IL::OpCode::BranchConditional: {
                        auto *branch = instr.As<IL::BranchConditionalInstruction>();

                        // Get the blocks
                        IL::BasicBlock* blockPass = fn->GetBlock(branch->pass);
                        IL::BasicBlock* blockFail = fn->GetBlock(branch->fail);

                        // Must be valid
                        if (!blockPass || !blockFail) {
                            return false;
                        }

                        // Get the terminators
                        auto terminatorPass = blockPass->GetTerminator().Get();
                        auto terminatorFail = blockFail->GetTerminator().Get();

                        // Resulting merge label, for structured cfg
                        IL::ID cfgMergeLabel{InvalidSpvId};

                        // Optional branch instructions
                        auto passBranch = terminatorPass->Cast<IL::BranchInstruction>();
                        auto failBranch = terminatorFail->Cast<IL::BranchInstruction>();

                        // If the pass block branches back to the fail block, assume merge block
                        if (passBranch) {
                            if (passBranch->branch == blockFail->GetID()) {
                                cfgMergeLabel = blockFail->GetID();
                            }
                        }

                        // If the fail block branches back to the success block, assume merge block
                        if (failBranch) {
                            if (failBranch->branch == blockPass->GetID()) {
                                cfgMergeLabel = blockPass->GetID();
                            }
                        }

                        // If neither, check shared branches
                        if (!cfgMergeLabel) {
                            // Must be non-conditional branches
                            if (!passBranch || !failBranch) {
                                return false;
                            }

                            // Not structured if diverging (not really true, but simplifies my life for now)
                            if (passBranch->branch != failBranch->branch) {
                                return false;
                            }

                            // Assume merge label
                            cfgMergeLabel = passBranch->branch;
                        }

                        // Write cfg
                        SpvInstruction& cfg = stream.Allocate(SpvOpSelectionMerge, 3);
                        cfg[1] = cfgMergeLabel;
                        cfg[2] = SpvSelectionControlMaskNone;

                        // Perform the branch, must be after cfg instruction
                        SpvInstruction& spv = stream.Allocate(SpvOpBranchConditional, 4);
                        spv[1] = branch->cond;
                        spv[2] = branch->pass;
                        spv[3] = branch->fail;
                        break;
                    }
                    case IL::OpCode::BitOr: {
                        auto *bitOr = instr.As<IL::BitOrInstruction>();

                        const SpvType* resultType = typeMap->GetType(bitOr->lhs);
                        typeMap->SetType(bitOr->result, resultType);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpBitwiseOr, 5, bitOr->source);
                        spv[1] = resultType->id;
                        spv[2] = bitOr->result;
                        spv[3] = bitOr->lhs;
                        spv[4] = bitOr->rhs;
                        break;
                    }
                    case IL::OpCode::BitAnd: {
                        auto *bitAnd = instr.As<IL::BitAndInstruction>();

                        const SpvType* resultType = typeMap->GetType(bitAnd->lhs);
                        typeMap->SetType(bitAnd->result, resultType);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpBitwiseAnd, 5, bitAnd->source);
                        spv[1] = resultType->id;
                        spv[2] = bitAnd->result;
                        spv[3] = bitAnd->lhs;
                        spv[4] = bitAnd->rhs;
                        break;
                    }
                    case IL::OpCode::BitShiftLeft: {
                        auto *bsl = instr.As<IL::BitShiftLeftInstruction>();

                        const SpvType* resultType = typeMap->GetType(bsl->value);
                        typeMap->SetType(bsl->result, resultType);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpShiftLeftLogical, 5, bsl->source);
                        spv[1] = resultType->id;
                        spv[2] = bsl->result;
                        spv[3] = bsl->value;
                        spv[4] = bsl->shift;
                        break;
                    }
                    case IL::OpCode::BitShiftRight: {
                        auto *bsr = instr.As<IL::BitShiftRightInstruction>();

                        const SpvType* resultType = typeMap->GetType(bsr->value);
                        typeMap->SetType(bsr->result, resultType);

                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpShiftRightLogical, 5, bsr->source);
                        spv[1] = resultType->id;
                        spv[2] = bsr->result;
                        spv[3] = bsr->value;
                        spv[4] = bsr->shift;
                        break;
                    }
                    case IL::OpCode::Export: {
                        auto *_export = instr.As<IL::ExportInstruction>();

                        // Note: This is quite ugly, will be changed

                        // UInt32
                        SpvIntType typeInt;
                        typeInt.bitWidth = 32;
                        typeInt.signedness = false;
                        const SpvType* uintType = typeMap->FindTypeOrAdd(typeInt);

                        // Uint32*
                        SpvPointerType typeUintImagePtr;
                        typeUintImagePtr.pointee = uintType;
                        typeUintImagePtr.storageClass = SpvStorageClassImage;
                        const SpvType* uintImagePtrType = typeMap->FindTypeOrAdd(typeUintImagePtr);

                        // Constant identifiers
                        uint32_t zeroUintId = jitHeader.bound++;
                        uint32_t streamOffsetId = jitHeader.bound++;
                        uint32_t scopeId = jitHeader.bound++;
                        uint32_t memSemanticId = jitHeader.bound++;
                        uint32_t offsetAdditionId = jitHeader.bound++;

                        // 0
                        SpvInstruction& spv = declarationRelocationBlock.stream.Allocate(SpvOpConstant, 4);
                        spv[1] = uintType->id;
                        spv[2] = zeroUintId;
                        spv[3] = 0;

                        // Index of the stream
                        SpvInstruction& spvOffset = declarationRelocationBlock.stream.Allocate(SpvOpConstant, 4);
                        spvOffset[1] = uintType->id;
                        spvOffset[2] = streamOffsetId;
                        spvOffset[3] = _export->exportID;

                        // Device scope
                        SpvInstruction& spvScope = declarationRelocationBlock.stream.Allocate(SpvOpConstant, 4);
                        spvScope[1] = uintType->id;
                        spvScope[2] = scopeId;
                        spvScope[3] = SpvScopeDevice;

                        // No memory mask
                        SpvInstruction& spvMemSem = declarationRelocationBlock.stream.Allocate(SpvOpConstant, 4);
                        spvMemSem[1] = uintType->id;
                        spvMemSem[2] = memSemanticId;
                        spvMemSem[3] = SpvMemorySemanticsMaskNone;

                        // The offset addition (will change for dynamic types in the future)
                        SpvInstruction& spvSize = declarationRelocationBlock.stream.Allocate(SpvOpConstant, 4);
                        spvSize[1] = uintType->id;
                        spvSize[2] = offsetAdditionId;
                        spvSize[3] = 1u;

                        uint32_t texelPtrId = jitHeader.bound++;

                        // Get the address of the texel to be atomically incremented
                        SpvInstruction& texelPtr = stream.Allocate(SpvOpImageTexelPointer, 6);
                        texelPtr[1] = uintImagePtrType->id;
                        texelPtr[2] = texelPtrId;
                        texelPtr[3] = metaData.exportMd.counterId;
                        texelPtr[4] = streamOffsetId;
                        texelPtr[5] = zeroUintId;

                        uint32_t atomicPositionId = jitHeader.bound++;

                        // Atomically increment the texel
                        SpvInstruction& atom = stream.Allocate(SpvOpAtomicIAdd, 7);
                        atom[1] = uintType->id;
                        atom[2] = atomicPositionId;
                        atom[3] = texelPtrId;
                        atom[4] = scopeId;
                        atom[5] = memSemanticId;
                        atom[6] = offsetAdditionId;

                        uint32_t accessId = jitHeader.bound++;

                        // Get the destination stream
                        SpvInstruction& chain = stream.Allocate(SpvOpAccessChain, 5);
                        chain[1] = metaData.exportMd.image32UIRWPtr->id;
                        chain[2] = accessId;
                        chain[3] = metaData.exportMd.streamId;
                        chain[4] = streamOffsetId;

                        uint32_t accessLoadId = jitHeader.bound++;

                        // Load the stream
                        SpvInstruction &load = stream.Allocate(SpvOpLoad, 4);
                        load[1] = metaData.exportMd.image32UIRW->id;
                        load[2] = accessLoadId;
                        load[3] = accessId;

                        // Write to the stream
                        SpvInstruction &write = stream.Allocate(SpvOpImageWrite, 5);
                        write[1] = accessLoadId;
                        write[2] = atomicPositionId;
                        write[3] = _export->value;
                        write[4] = SpvImageOperandsMaskNone;

                        break;
                    }
                    case IL::OpCode::Alloca: {
                        auto *bsr = instr.As<IL::BitShiftRightInstruction>();

                        const SpvType* resultType = typeMap->GetType(bsr->value);
                        typeMap->SetType(bsr->result, resultType);

                        SpvInstruction& spv = declarationRelocationBlock.stream.TemplateOrAllocate(SpvOpVariable, 4, bsr->source);
                        spv[1] = resultType->id;
                        spv[2] = bsr->result;
                        spv[3] = SpvStorageClassFunction;
                        break;
                    }
                    case IL::OpCode::Load: {
                        auto *load = instr.As<IL::LoadInstruction>();

                        const SpvType* resultType = typeMap->GetType(load->address);
                        ASSERT(resultType->kind == SpvTypeKind::Pointer, "Load must be pointer (shouldn't be assert....) TODO!");

                        auto* pointerType = resultType->As<SpvPointerType>();
                        typeMap->SetType(load->result, pointerType);

                        SpvInstruction& spv = declarationRelocationBlock.stream.TemplateOrAllocate(SpvOpLoad, 4, load->source);
                        spv[1] = pointerType->pointee->id;
                        spv[2] = load->result;
                        spv[3] = SpvStorageClassFunction;
                        break;
                    }
                    case IL::OpCode::Store: {
                        auto *load = instr.As<IL::StoreInstruction>();

                        SpvInstruction& spv = declarationRelocationBlock.stream.TemplateOrAllocate(SpvOpStore, 3, load->source);
                        spv[1] = load->address;
                        spv[2] = load->value;
                        break;
                    }
                    case IL::OpCode::StoreBuffer: {
                        auto *storeBuffer = instr.As<IL::StoreBufferInstruction>();

                        // Write image
                        SpvInstruction& spv = stream.TemplateOrAllocate(SpvOpImageWrite, 4, instr->source);
                        spv[1] = storeBuffer->buffer;
                        spv[2] = storeBuffer->index;
                        spv[3] = storeBuffer->value;
                        break;
                    }
                }
            }
        }
    }

    // Stitch the final program
    spirvProgram.clear();
    relocationStream.Stitch(spirvProgram);

    // OK!
    return true;
}

void SpvModule::InsertExportRecords(ProgramHeader& jitHeader, SpvRelocationStream &stream, const SpvJob& job) {
    // Note: This is quite ugly, will be changed

    // Get the layout sections
    const LayoutSection& capabilitySection = GetSection(LayoutSectionType::Capability);
    const LayoutSection& annotationSection = GetSection(LayoutSectionType::Annotation);
    const LayoutSection& declarationSection = GetSection(LayoutSectionType::Declarations);

    // Allocate new blocks
    SpvStream& capabilities = stream.AllocateBlock(capabilitySection.sourceSpan.AppendSpan()).stream;
    SpvStream& annotations = stream.AllocateBlock(annotationSection.sourceSpan.AppendSpan()).stream;
    SpvStream& declarations = stream.AllocateBlock(declarationSection.sourceSpan.AppendSpan()).stream;

    // Set insertion stream
    typeMap->SetDeclarationStream(&declarations);

    // UInt32
    SpvIntType intDecl;
    intDecl.bitWidth = 32;
    intDecl.signedness = false;
    const SpvType* intType = typeMap->FindTypeOrAdd(intDecl);

    // RWBuffer<uint>
    SpvImageType imageDecl;
    imageDecl.sampledType = intType;
    imageDecl.dimension = SpvDim::SpvDimBuffer;
    imageDecl.depth = 2;
    imageDecl.arrayed = 0;
    imageDecl.multisampled = 0;
    imageDecl.sampled = 2;
    imageDecl.format = SpvImageFormatR32ui;
    metaData.exportMd.image32UIRW = typeMap->FindTypeOrAdd(imageDecl);

    // RWBuffer<uint>*
    SpvPointerType imagePtrDecl;
    imagePtrDecl.storageClass = SpvStorageClassUniformConstant;
    imagePtrDecl.pointee = metaData.exportMd.image32UIRW;
    metaData.exportMd.image32UIRWPtr = typeMap->FindTypeOrAdd(imagePtrDecl);

    // RWBuffer<uint>[N]
    SpvArrayType arrayDecl;
    arrayDecl.elementType = metaData.exportMd.image32UIRW;
    arrayDecl.count = std::max(1u, job.streamCount);
    const SpvType* image32UIWWArray = typeMap->FindTypeOrAdd(arrayDecl);

    // RWBuffer<uint>[N]*
    imagePtrDecl.pointee = image32UIWWArray;
    metaData.exportMd.image32UIRWArrayPtr = typeMap->FindTypeOrAdd(imagePtrDecl);

    // Id allocations
    metaData.exportMd.counterId = jitHeader.bound++;
    metaData.exportMd.streamId = jitHeader.bound++;

    // Counter
    SpvInstruction& spvCounterVar = declarations.Allocate(SpvOpVariable, 4);
    spvCounterVar[1] = metaData.exportMd.image32UIRWPtr->id;
    spvCounterVar[2] = metaData.exportMd.counterId;
    spvCounterVar[3] = SpvStorageClassUniformConstant;

    // Streams
    SpvInstruction& spvStreamVar = declarations.Allocate(SpvOpVariable, 4);
    spvStreamVar[1] = metaData.exportMd.image32UIRWArrayPtr->id;
    spvStreamVar[2] = metaData.exportMd.streamId;
    spvStreamVar[3] = SpvStorageClassUniformConstant;

    // Descriptor set
    SpvInstruction& spvCounterSet = annotations.Allocate(SpvOpDecorate, 4);
    spvCounterSet[1] = metaData.exportMd.counterId;
    spvCounterSet[2] = SpvDecorationDescriptorSet;
    spvCounterSet[3] = job.instrumentationKey.pipelineLayoutUserSlots;

    // Binding
    SpvInstruction& spvCounterBinding = annotations.Allocate(SpvOpDecorate, 4);
    spvCounterBinding[1] = metaData.exportMd.counterId;
    spvCounterBinding[2] = SpvDecorationBinding;
    spvCounterBinding[3] = 0;

    // Descriptor set
    SpvInstruction& spvStreamSet = annotations.Allocate(SpvOpDecorate, 4);
    spvStreamSet[1] = metaData.exportMd.streamId;
    spvStreamSet[2] = SpvDecorationDescriptorSet;
    spvStreamSet[3] = job.instrumentationKey.pipelineLayoutUserSlots;

    // Binding
    SpvInstruction& spvStreamBinding = annotations.Allocate(SpvOpDecorate, 4);
    spvStreamBinding[1] = metaData.exportMd.streamId;
    spvStreamBinding[2] = SpvDecorationBinding;
    spvStreamBinding[3] = 1;
}