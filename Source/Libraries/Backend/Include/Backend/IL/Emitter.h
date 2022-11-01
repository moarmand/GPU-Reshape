#pragma once

// Backend
#include "BasicBlock.h"
#include "Program.h"
#include "TypeResult.h"
#include "ControlFlow.h"

// Common
#include <Common/Alloca.h>

namespace IL {
    namespace Op {
        /// Append operation
        struct Append {
            using Opaque = ConstOpaqueInstructionRef;

            /// Operation
            template<typename T>
            static InstructionRef <T> Op(BasicBlock *basicBlock, Opaque &insertionPoint, const T &instruction) {
                if (insertionPoint.IsValid()) {
                    auto value = basicBlock->Insert(insertionPoint, instruction);
                    insertionPoint = std::next(value);
                    return value.template Ref<T>();
                } else {
                    auto value = basicBlock->Append(instruction);
                    return value.template Ref<T>();
                }
            }
        };

        /// Replacement operation
        struct Replace {
            using Opaque = OpaqueInstructionRef;

            /// Operation
            template<typename T>
            static InstructionRef <T> Op(BasicBlock *basicBlock, Opaque &insertionPoint, const T &instruction) {
                ASSERT(insertionPoint.IsValid(), "Must have insertion point");

                auto value = basicBlock->Replace(insertionPoint, instruction);
                insertionPoint = value;
                return value.template Ref<T>();
            }
        };

        /// Instrumentation operation
        struct Instrument {
            using Opaque = OpaqueInstructionRef;

            /// Operation
            template<typename T>
            static InstructionRef <T> Op(BasicBlock *basicBlock, Opaque &insertionPoint, T &instruction) {
                ASSERT(insertionPoint.IsValid(), "Must have insertion point");

                // Instrumentation must inherit the source instruction for specialized backend operands
                //  ? Not all instruction parameters are exposed, and altering these may change the intended behaviour
                instruction.source = InstructionRef<>(insertionPoint)->source.Modify();

                auto value = basicBlock->Replace(insertionPoint, instruction);
                insertionPoint = value;
                return value.template Ref<T>();
            }
        };
    }

    /// Emitter, easy instruction emitting
    template<typename OP = Op::Append>
    struct Emitter {
        using Opaque = typename OP::Opaque;

        /// Default constructor
        Emitter() = default;

        /// Constructor
        Emitter(Program &program, BasicBlock &basicBlock, const Opaque &ref = {}) {
            SetProgram(&program);
            SetBasicBlock(&basicBlock);
            SetInsertionPoint(ref);
        }

        /// Set the current program
        void SetProgram(Program *value) {
            program = value;
            capabilityTable = &program->GetCapabilityTable();
            map = &value->GetIdentifierMap();
            typeMap = &value->GetTypeMap();
        }

        /// Set the current basic block
        void SetBasicBlock(BasicBlock *value) {
            basicBlock = value;
        }

        /// Set the insertion point
        void SetInsertionPoint(const Opaque &ref) {
            insertionPoint = ref;
        }

        /// Get the insertion point
        Opaque GetInsertionPoint() {
            return insertionPoint;
        }

        /// Add a new flag
        void AddBlockFlag(BasicBlockFlagSet flags) {
            basicBlock->AddFlag(flags);
        }

        /// Add an integral instruction
        /// \param bitWidth the bit width of the type
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> Integral(uint8_t bitWidth, int64_t value, bool signedness) {
            LiteralInstruction instr{};
            instr.opCode = OpCode::Literal;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.type = LiteralType::Int;
            instr.signedness = signedness;
            instr.bitWidth = bitWidth;
            instr.value.integral = value;
            return Op(instr);
        }

        /// Add an int constant
        /// \param bitWidth the bit width of the type
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> Int(uint8_t bitWidth, int64_t value) {
            return Integral(bitWidth, value, true);
        }

        /// Add a uint constant
        /// \param bitWidth the bit width of the type
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> UInt(uint8_t bitWidth, int64_t value) {
            return Integral(bitWidth, value, false);
        }

        /// Add a 32 bit integral instruction
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> Int32(int32_t value) {
            return Int(32, value);
        }

        /// Add a 16 bit integral instruction
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> Int16(int16_t value) {
            return Int(16, value);
        }

        /// Add a 8 bit integral instruction
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> Int8(int8_t value) {
            return Int(8, value);
        }

        /// Add a 32 bit integral instruction
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> UInt32(uint32_t value) {
            return UInt(32, value);
        }

        /// Add a 16 bit integral instruction
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> UInt16(uint16_t value) {
            return UInt(16, value);
        }

        /// Add a 8 bit integral instruction
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> UInt8(uint8_t value) {
            return UInt(8, value);
        }

        /// Add a floating point instruction
        /// \param result the result id
        /// \param bitWidth the bit width of the type
        /// \param value the constant value
        /// \return instruction reference
        InstructionRef <LiteralInstruction> FP(uint8_t bitWidth, double value) {
            LiteralInstruction instr{};
            instr.opCode = OpCode::Literal;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.type = LiteralType::FP;
            instr.signedness = true;
            instr.bitWidth = bitWidth;
            instr.value.fp = value;
            return Op(instr);
        }

        /// Load an address
        /// \param address the address to be loaded
        /// \return instruction reference
        InstructionRef <LoadInstruction> Load(ID address) {
            ASSERT(IsMapped(address), "Unmapped identifier");

            LoadInstruction instr{};
            instr.opCode = OpCode::Load;
            instr.source = Source::Invalid();
            instr.address = address;
            instr.result = map->AllocID();
            return Op(instr);
        }

        /// Store to an address
        /// \param address the address to be stored to
        /// \param value the value to be stored
        /// \return instruction reference
        InstructionRef <StoreInstruction> Store(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            StoreInstruction instr{};
            instr.opCode = OpCode::Store;
            instr.source = Source::Invalid();
            instr.result = InvalidID;
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Store an element to a buffer
        /// \param buffer the destination buffer
        /// \param index the index of the element
        /// \param value the element data
        /// \return instruction reference
        InstructionRef <StoreBufferInstruction> StoreBuffer(ID buffer, ID index, ID value) {
            ASSERT(IsMapped(buffer) && IsMapped(index) && IsMapped(value), "Unmapped identifier");

            StoreBufferInstruction instr{};
            instr.opCode = OpCode::StoreBuffer;
            instr.source = Source::Invalid();
            instr.buffer = buffer;
            instr.index = index;
            instr.value = value;
            instr.mask = ComponentMask::All;
            instr.result = InvalidID;
            return Op(instr);
        }

        /// Load an element from a buffer
        /// \param buffer the destination buffer
        /// \param index the index of the element
        /// \return instruction reference
        InstructionRef <LoadBufferInstruction> LoadBuffer(ID buffer, ID index) {
            ASSERT(IsMapped(buffer) && IsMapped(index), "Unmapped identifier");

            LoadBufferInstruction instr{};
            instr.opCode = OpCode::LoadBuffer;
            instr.source = Source::Invalid();
            instr.buffer = buffer;
            instr.index = index;
            instr.result = map->AllocID();
            return Op(instr);
        }

        /// Get the size of a resource
        /// \param resource the resource to be queried
        /// \return instruction reference
        InstructionRef <ResourceSizeInstruction> ResourceSize(ID resource) {
            ASSERT(IsMapped(resource), "Unmapped identifier");

            ResourceSizeInstruction instr{};
            instr.opCode = OpCode::ResourceSize;
            instr.source = Source::Invalid();
            instr.resource = resource;
            instr.result = map->AllocID();
            return Op(instr);
        }

        /// Get the identifier of a resource
        /// \param resource the resource to be queried
        /// \return instruction reference
        InstructionRef <ResourceTokenInstruction> ResourceToken(ID resource) {
            ASSERT(IsMapped(resource), "Unmapped identifier");

            ResourceTokenInstruction instr{};
            instr.opCode = OpCode::ResourceToken;
            instr.source = Source::Invalid();
            instr.resource = resource;
            instr.result = map->AllocID();
            return Op(instr);
        }

        /// Binary add two values
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <BitCastInstruction> BitCast(ID value, const Backend::IL::Type* type) {
            ASSERT(IsMapped(value), "Unmapped identifier");

            BitCastInstruction instr{};
            instr.opCode = OpCode::BitCast;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.value = value;
            return Op(instr, type);
        }

        /// Get the address of a composite element
        /// \param composite the base composite address
        /// \param index the uniform index
        /// \return instruction reference
        InstructionRef <AddressChainInstruction> AddressOf(ID composite, ID index) {
            ASSERT(IsMapped(composite) && IsMapped(index), "Unmapped identifier");

            auto instr = ALLOCA_SIZE(IL::AddressChainInstruction, IL::AddressChainInstruction::GetSize(1));
            instr->opCode = OpCode::AddressChain;
            instr->source = Source::Invalid();
            instr->result = map->AllocID();
            instr->composite = composite;
            instr->chains.count = 1;
            instr->chains[0].index = index;
            return Op(*instr);
        }

        /// Extract a value from a composite
        /// \param composite the base composite
        /// \param index the index
        /// \return instruction reference
        InstructionRef <ExtractInstruction> Extract(ID composite, ID index) {
            ASSERT(IsMapped(composite) && IsMapped(index), "Unmapped identifier");

            ExtractInstruction instr{};
            instr.opCode = OpCode::Extract;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.composite = composite;
            instr.index = index;
            return Op(instr);
        }

        /// Insert a value to a composite
        /// \param composite the base composite
        /// \param index the value to be inserted
        /// \return instruction reference
        InstructionRef <InsertInstruction> Insert(ID composite, ID value) {
            ASSERT(IsMapped(composite) && IsMapped(value), "Unmapped identifier");

            InsertInstruction instr{};
            instr.opCode = OpCode::Insert;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.composite = composite;
            instr.value = value;
            return Op(instr);
        }

        /// Select a value
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <SelectInstruction> Select(ID condition, ID pass, ID fail) {
            ASSERT(IsMapped(condition) && IsMapped(pass) && IsMapped(fail), "Unmapped identifier");

            SelectInstruction instr{};
            instr.opCode = OpCode::Select;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.condition = condition;
            instr.pass = pass;
            instr.fail = fail;
            return Op(instr);
        }

        /// Binary add two values
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <AddInstruction> Add(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            AddInstruction instr{};
            instr.opCode = OpCode::Add;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Binary sub two values
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <SubInstruction> Sub(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            SubInstruction instr{};
            instr.opCode = OpCode::Sub;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Binary div two values
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <DivInstruction> Div(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            DivInstruction instr{};
            instr.opCode = OpCode::Div;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Binary mul two values
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <MulInstruction> Mul(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            MulInstruction instr{};
            instr.opCode = OpCode::Mul;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check for equality
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <EqualInstruction> Equal(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            EqualInstruction instr{};
            instr.opCode = OpCode::Equal;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check if two values are not equal
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <NotEqualInstruction> NotEqual(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            NotEqualInstruction instr{};
            instr.opCode = OpCode::NotEqual;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check if one value is greater than another
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <GreaterThanInstruction> GreaterThan(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            GreaterThanInstruction instr{};
            instr.opCode = OpCode::GreaterThan;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check if one value is greater than or equal to another
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <GreaterThanEqualInstruction> GreaterThanEqual(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            GreaterThanEqualInstruction instr{};
            instr.opCode = OpCode::GreaterThanEqual;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check if one value is less than another
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <LessThanInstruction> LessThan(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            LessThanInstruction instr{};
            instr.opCode = OpCode::LessThan;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check if one value is less than or equal to another
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <LessThanEqualInstruction> LessThanEqual(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            LessThanEqualInstruction instr{};
            instr.opCode = OpCode::LessThanEqual;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check if a value is infinite
        /// \param value value operand
        /// \return instruction reference
        InstructionRef <IsInfInstruction> IsInf(ID value) {
            ASSERT(IsMapped(value), "Unmapped identifier");

            IsInfInstruction instr{};
            instr.opCode = OpCode::IsInf;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.value = value;
            return Op(instr);
        }

        /// Check if a value is NaN
        /// \param value value operand
        /// \return instruction reference
        InstructionRef <IsNaNInstruction> IsNaN(ID value) {
            ASSERT(IsMapped(value), "Unmapped identifier");

            IsNaNInstruction instr{};
            instr.opCode = OpCode::IsNaN;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.value = value;
            return Op(instr);
        }

        /// Perform a bitwise or
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <BitOrInstruction> BitOr(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            BitOrInstruction instr{};
            instr.opCode = OpCode::BitOr;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Perform a bitwise and
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <BitAndInstruction> BitAnd(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            BitAndInstruction instr{};
            instr.opCode = OpCode::BitAnd;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Perform a logical or
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <OrInstruction> Or(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            OrInstruction instr{};
            instr.opCode = OpCode::Or;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Perform a logical and
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <AndInstruction> And(ID lhs, ID rhs) {
            ASSERT(IsMapped(lhs) && IsMapped(rhs), "Unmapped identifier");

            AndInstruction instr{};
            instr.opCode = OpCode::And;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.lhs = lhs;
            instr.rhs = rhs;
            return Op(instr);
        }

        /// Check if all components may be evaluated to true
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <AllInstruction> All(ID value) {
            ASSERT(IsMapped(value), "Unmapped identifier");

            AllInstruction instr{};
            instr.opCode = OpCode::All;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.value = value;
            return Op(instr);
        }

        /// Check if any components may be evaluated to true
        /// \param lhs lhs operand
        /// \param rhs rhs operand
        /// \return instruction reference
        InstructionRef <AnyInstruction> Any(ID value) {
            ASSERT(IsMapped(value), "Unmapped identifier");

            AnyInstruction instr{};
            instr.opCode = OpCode::Any;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.value = value;
            return Op(instr);
        }

        /// Perform a bitwise left shift
        /// \param lhs value the value to be shifted
        /// \param rhs shift the bit shift count
        /// \return instruction reference
        InstructionRef <BitShiftLeftInstruction> BitShiftLeft(ID value, ID shift) {
            ASSERT(IsMapped(value) && IsMapped(shift), "Unmapped identifier");

            BitShiftLeftInstruction instr{};
            instr.opCode = OpCode::BitShiftLeft;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.value = value;
            instr.shift = shift;
            return Op(instr);
        }

        /// Perform a bitwise right shift
        /// \param lhs value the value to be shifted
        /// \param rhs shift the bit shift count
        /// \return instruction reference
        InstructionRef <BitShiftRightInstruction> BitShiftRight(ID value, ID shift) {
            ASSERT(IsMapped(value) && IsMapped(shift), "Unmapped identifier");

            BitShiftRightInstruction instr{};
            instr.opCode = OpCode::BitShiftRight;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.value = value;
            instr.shift = shift;
            return Op(instr);
        }

        /// Branch to a block
        /// \param branch the destination block
        /// \return instruction reference
        InstructionRef <BranchInstruction> Branch(BasicBlock* branch) {
            ASSERT(branch, "Invalid branch");

            BranchInstruction instr{};
            instr.opCode = OpCode::Branch;
            instr.source = Source::Invalid();
            instr.result = IL::InvalidID;
            instr.branch = branch->GetID();
            return Op(instr);
        }

        /// Conditionally branch to a block
        /// \param cond the condition
        /// \param pass the block branched to if cond is true
        /// \param fail the block branched ot if cond is false
        /// \param merge the structured control flow
        /// \return instruction reference
        InstructionRef <BranchConditionalInstruction> BranchConditional(ID cond, BasicBlock* pass, BasicBlock* fail, ControlFlow controlFlow) {
            ASSERT(IsMapped(cond), "Unmapped identifier");
            ASSERT(pass && fail, "Invalid branch");

            BranchConditionalInstruction instr{};
            instr.opCode = OpCode::BranchConditional;
            instr.source = Source::Invalid();
            instr.result = IL::InvalidID;
            instr.cond = cond;
            instr.pass = pass->GetID();
            instr.fail = fail->GetID();
            instr.controlFlow = controlFlow;
            return Op(instr);
        }

        /// Perform an atomic / interlocked or
        /// \param address base address
        /// \param value value
        /// \return original value
        InstructionRef<AtomicOrInstruction> AtomicOr(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            AtomicOrInstruction instr{};
            instr.opCode = OpCode::AtomicOr;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Perform an atomic / interlocked exclusive or
        /// \param address base address
        /// \param value value
        /// \return original value
        InstructionRef<AtomicXOrInstruction> AtomicXOr(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            AtomicXOrInstruction instr{};
            instr.opCode = OpCode::AtomicXOr;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Perform an atomic / interlocked and
        /// \param address base address
        /// \param value value
        /// \return original value
        InstructionRef<AtomicAndInstruction> AtomicAnd(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            AtomicAndInstruction instr{};
            instr.opCode = OpCode::AtomicAnd;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Perform an atomic / interlocked add
        /// \param address base address
        /// \param value value
        /// \return original value
        InstructionRef<AtomicAddInstruction> AtomicAdd(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            AtomicAddInstruction instr{};
            instr.opCode = OpCode::AtomicAdd;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Perform an atomic / interlocked min
        /// \param address base address
        /// \param value value
        /// \return original value
        InstructionRef<AtomicMinInstruction> AtomicMin(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            AtomicMinInstruction instr{};
            instr.opCode = OpCode::AtomicMin;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Perform an atomic / interlocked max
        /// \param address base address
        /// \param value value
        /// \return original value
        InstructionRef<AtomicMaxInstruction> AtomicMax(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            AtomicMaxInstruction instr{};
            instr.opCode = OpCode::AtomicMax;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Perform an atomic / interlocked exchange
        /// \param address base address
        /// \param value value
        /// \return original value
        InstructionRef<AtomicExchangeInstruction> AtomicExchange(ID address, ID value) {
            ASSERT(IsMapped(address) && IsMapped(value), "Unmapped identifier");

            AtomicExchangeInstruction instr{};
            instr.opCode = OpCode::AtomicExchange;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.value = value;
            return Op(instr);
        }

        /// Perform an atomic / interlocked compare exchange
        /// \param address base address
        /// \param comparator base value to be compared against for successful exchange
        /// \param value value
        /// \return original value
        InstructionRef<AtomicCompareExchangeInstruction> AtomicCompareExchange(ID address, ID comparator, ID value) {
            ASSERT(IsMapped(address) && IsMapped(comparator) && IsMapped(value), "Unmapped identifier");

            AtomicCompareExchangeInstruction instr{};
            instr.opCode = OpCode::AtomicCompareExchange;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.address = address;
            instr.comparator = comparator;
            instr.value = value;
            return Op(instr);
        }

        /// Export a shader export
        /// \param exportID the allocation id for the export
        /// \param value the value to be exported
        /// \return instruction reference
        InstructionRef <ExportInstruction> Export(ShaderExportID exportID, ID value) {
            ASSERT(IsMapped(value), "Unmapped identifier");

            ExportInstruction instr{};
            instr.opCode = OpCode::Export;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.exportID = exportID;
            instr.value = value;
            return Op(instr);
        }

        /// Construct and export a shader export
        /// \param exportID the allocation id for the export
        /// \param value the value to be exported, constructed internally
        /// \return instruction reference
        template<typename T>
        InstructionRef <ExportInstruction> Export(ShaderExportID exportID, const T& value) {
            ExportInstruction instr{};
            instr.opCode = OpCode::Export;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.exportID = exportID;
            instr.value = value.Construct(*this);
            return Op(instr);
        }

        /// Alloca a varaible
        /// \param type the varaible type
        /// \return instruction reference
        InstructionRef <AllocaInstruction> Alloca(ID type) {
            ASSERT(IsMapped(type), "Unmapped identifier");

            AllocaInstruction instr{};
            instr.opCode = OpCode::Alloca;
            instr.source = Source::Invalid();
            instr.result = map->AllocID();
            instr.type = type;
            return Op(instr);
        }

        /// Is this emitter good?
        bool Good() const {
            return program && basicBlock;
        }

        /// Get the current program
        Program *GetProgram() const {
            return program;
        }

        /// Get the current basic block
        BasicBlock *GetBasicBlock() const {
            return basicBlock;
        }

    private:
        /// Check if an id is mapped
        bool IsMapped(ID id) const {
            return id != InvalidID;
        }

        /// Perform the operation
        template<typename T>
        InstructionRef <T> Op(T &instruction) {
            // Set type of the instruction if relevant
            if (const Backend::IL::Type* type = Backend::IL::ResultOf(*program, &instruction)) {
                program->GetTypeMap().SetType(instruction.result, type);
            }

            // Perform the insertion
            return OP::template Op<T>(basicBlock, insertionPoint, instruction);
        }

        /// Perform the operation
        template<typename T>
        InstructionRef <T> Op(T &instruction, const Backend::IL::Type* type) {
            // Set type of the instruction
            program->GetTypeMap().SetType(instruction.result, type);

            // Perform the insertion
            return OP::template Op<T>(basicBlock, insertionPoint, instruction);
        }

    private:
        /// Current insertion point
        Opaque insertionPoint{};

        /// Current program
        Program *program{nullptr};

        /// Current capability table
        CapabilityTable* capabilityTable{nullptr};

        /// Current identifier map
        IdentifierMap *map{nullptr};

        /// Current type map
        Backend::IL::TypeMap* typeMap{nullptr};

        /// Current basic block
        BasicBlock *basicBlock{nullptr};
    };
}
