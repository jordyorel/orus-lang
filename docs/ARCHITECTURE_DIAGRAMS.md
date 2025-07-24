# Orus Architecture Diagrams and Charts

This document contains detailed architectural diagrams and charts for the Orus programming language implementation.

## VM Register Architecture Detailed View

```mermaid
graph TB
    subgraph "Orus VM 256-Register Architecture"
        subgraph "Register File Layout"
            subgraph "Global Registers (R0-R63)"
                subgraph "System Globals (R0-R15)"
                    R0[R0: Program Counter]
                    R1[R1: Stack Pointer]
                    R2[R2: Frame Pointer]
                    R3[R3: Exception Handler]
                    R4[R4: Global Constants]
                    R5[R5-R15: Reserved]
                end
                
                subgraph "Module Globals (R16-R63)"
                    R16[R16-R31: Import Table]
                    R32[R32-R47: Export Table]
                    R48[R48-R63: Module State]
                end
            end
            
            subgraph "Frame Registers (R64-R191)"
                subgraph "Function Context (R64-R95)"
                    R64[R64-R71: Parameters]
                    R72[R72-R79: Return Values]
                    R80[R80-R95: Local Variables]
                end
                
                subgraph "Extended Locals (R96-R191)"
                    R96[R96-R127: Complex Locals]
                    R128[R128-R159: Nested Scope]
                    R160[R160-R191: Closure Capture]
                end
            end
            
            subgraph "Temporary Registers (R192-R239)"
                subgraph "Expression Evaluation (R192-R215)"
                    R192[R192-R199: Arithmetic]
                    R200[R200-R207: Comparison]
                    R208[R208-R215: Logical Ops]
                end
                
                subgraph "Compiler Temporaries (R216-R239)"
                    R216[R216-R223: Type Checking]
                    R224[R224-R231: Optimization]
                    R232[R232-R239: Scratch Space]
                end
            end
            
            subgraph "Module System (R240-R255)"
                R240[R240-R247: Module Loading]
                R248[R248-R255: Dynamic Linking]
            end
        end
        
        subgraph "Register Management"
            Allocator[Register Allocator]
            Allocator --> Lifetime[Lifetime Analysis]
            Allocator --> Pressure[Pressure Analysis]
            Allocator --> Spill[Spill Logic]
            
            Lifetime --> Short[Short-lived: R192-R239]
            Lifetime --> Medium[Medium-lived: R64-R191]
            Lifetime --> Long[Long-lived: R16-R63]
            Lifetime --> Permanent[Permanent: R0-R15]
        end
    end
```

## Compiler Pipeline Detailed Flow

```mermaid
flowchart TD
    subgraph "Source Processing"
        Source[Orus Source Code] --> Preprocess[Preprocessor]
        Preprocess --> TokenStream[Token Stream]
    end
    
    subgraph "Lexical Analysis"
        TokenStream --> Lexer[Lexer Engine]
        Lexer --> Keywords[Keyword Recognition]
        Lexer --> Literals[Literal Processing]
        Lexer --> Operators[Operator Parsing]
        Keywords --> Tokens[Structured Tokens]
        Literals --> Tokens
        Operators --> Tokens
    end
    
    subgraph "Syntax Analysis"
        Tokens --> Parser[Precedence Climbing Parser]
        Parser --> AST[Abstract Syntax Tree]
        Parser --> ParseErrors[Parse Error Recovery]
        ParseErrors --> ErrorReporting[Error Messages]
    end
    
    subgraph "Semantic Analysis"
        AST --> TypeInference[Type Inference Engine]
        TypeInference --> TypeChecking[Type Validation]
        TypeChecking --> ScopeAnalysis[Scope Analysis]
        ScopeAnalysis --> SafetyCheck[Safety Validation]
        SafetyCheck --> SemanticAST[Annotated AST]
    end
    
    subgraph "Backend Selection"
        SemanticAST --> ComplexityAnalyzer[Complexity Analysis]
        ComplexityAnalyzer --> Simple{Simple Program?}
        ComplexityAnalyzer --> Complex{Complex Features?}
        ComplexityAnalyzer --> Mixed{Mixed Complexity?}
        
        Simple --> SinglePass[Single-Pass Backend]
        Complex --> MultiPass[Multi-Pass Backend]
        Mixed --> Hybrid[Hybrid Backend]
    end
    
    subgraph "Code Generation"
        SinglePass --> FastCodegen[Fast Code Generation]
        MultiPass --> OptCodegen[Optimized Code Generation]
        Hybrid --> BalancedCodegen[Balanced Code Generation]
        
        FastCodegen --> Bytecode[VM Bytecode]
        OptCodegen --> Bytecode
        BalancedCodegen --> Bytecode
    end
    
    subgraph "Optimization Layers"
        OptCodegen --> LICM[Loop Invariant Code Motion]
        OptCodegen --> RegisterOpt[Register Allocation]
        OptCodegen --> DeadCode[Dead Code Elimination]
        OptCodegen --> ConstFold[Constant Folding]
        
        LICM --> OptimizedBytecode[Optimized Bytecode]
        RegisterOpt --> OptimizedBytecode
        DeadCode --> OptimizedBytecode
        ConstFold --> OptimizedBytecode
    end
```

## Type System Architecture

```mermaid
graph TB
    subgraph "Type System Components"
        subgraph "Type Representation"
            TypeObject[Type Object]
            TypeObject --> PrimitiveType[Primitive Types]
            TypeObject --> CompositeType[Composite Types]
            TypeObject --> GenericType[Generic Types]
            
            PrimitiveType --> NumericTypes[Numeric Types]
            PrimitiveType --> BoolType[Boolean Type]
            PrimitiveType --> StringType[String Type]
            PrimitiveType --> NilType[Nil Type]
            
            NumericTypes --> IntegerTypes[Integer Family]
            NumericTypes --> FloatTypes[Float Family]
            
            IntegerTypes --> SignedInts[i8, i16, i32, i64]
            IntegerTypes --> UnsignedInts[u8, u16, u32, u64]
            
            FloatTypes --> F32[f32]
            FloatTypes --> F64[f64]
            
            CompositeType --> ArrayType[Array Types]
            CompositeType --> FunctionType[Function Types]
            CompositeType --> StructType[Struct Types]
            CompositeType --> TupleType[Tuple Types]
            
            GenericType --> TypeVariable[Type Variables]
            GenericType --> ConstrainedType[Constrained Types]
            GenericType --> PolymorphicType[Polymorphic Types]
        end
        
        subgraph "Type Inference Engine"
            InferenceEngine[Hindley-Milner Engine]
            InferenceEngine --> ConstraintGeneration[Constraint Generation]
            InferenceEngine --> ConstraintSolving[Constraint Solving]
            InferenceEngine --> Unification[Type Unification]
            InferenceEngine --> Substitution[Substitution Application]
            
            ConstraintGeneration --> ExpressionConstraints[Expression Constraints]
            ConstraintGeneration --> DeclarationConstraints[Declaration Constraints]
            ConstraintGeneration --> FunctionConstraints[Function Constraints]
            
            ConstraintSolving --> UnificationAlgorithm[Unification Algorithm]
            ConstraintSolving --> OccursCheck[Occurs Check]
            ConstraintSolving --> ErrorRecovery[Error Recovery]
        end
        
        subgraph "Cast System"
            CastEngine[Cast Validation Engine]
            CastEngine --> SafeCasts[Safe Casts]
            CastEngine --> UnsafeCasts[Unsafe Casts]
            CastEngine --> ForbiddenCasts[Forbidden Casts]
            
            SafeCasts --> WidenCasts[Widening Casts]
            SafeCasts --> IdentityCasts[Identity Casts]
            
            UnsafeCasts --> NarrowCasts[Narrowing Casts]
            UnsafeCasts --> FloatIntCasts[Float-Integer Casts]
            
            ForbiddenCasts --> StringCasts[String to Numeric]
            ForbiddenCasts --> IncompatibleCasts[Incompatible Types]
        end
        
        subgraph "Type Checking"
            TypeChecker[Type Checker]
            TypeChecker --> ExpressionChecking[Expression Type Checking]
            TypeChecker --> StatementChecking[Statement Type Checking]
            TypeChecker --> FunctionChecking[Function Type Checking]
            
            ExpressionChecking --> BinaryOpChecking[Binary Operation Checking]
            ExpressionChecking --> UnaryOpChecking[Unary Operation Checking]
            ExpressionChecking --> CallChecking[Function Call Checking]
            
            StatementChecking --> AssignmentChecking[Assignment Checking]
            StatementChecking --> DeclarationChecking[Declaration Checking]
            StatementChecking --> ControlFlowChecking[Control Flow Checking]
        end
    end
    
    subgraph "Type Error System"
        TypeError[Type Error]
        TypeError --> TypeMismatch[Type Mismatch Errors]
        TypeError --> CastError[Invalid Cast Errors]
        TypeError --> ScopeError[Scope Violation Errors]
        TypeError --> ConstraintError[Constraint Violation Errors]
        
        TypeMismatch --> FriendlyMessage[User-Friendly Error Message]
        CastError --> FriendlyMessage
        ScopeError --> FriendlyMessage
        ConstraintError --> FriendlyMessage
        
        FriendlyMessage --> SourceContext[Source Code Context]
        FriendlyMessage --> HelpSuggestion[Actionable Help]
        FriendlyMessage --> Examples[Fix Examples]
    end
```

## VM Instruction Dispatch Architecture

```mermaid
graph TB
    subgraph "Instruction Dispatch System"
        subgraph "Bytecode Input"
            BytecodeStream[Bytecode Stream]
            BytecodeStream --> InstructionFetch[Instruction Fetch]
            InstructionFetch --> InstructionDecode[Instruction Decode]
        end
        
        subgraph "Dispatch Strategy Selection"
            InstructionDecode --> DispatchSelector{Dispatch Strategy}
            
            DispatchSelector --> |GCC/Clang with Labels-as-Values| ComputedGoto[Computed Goto Dispatch]
            DispatchSelector --> |Portable C| SwitchDispatch[Switch-Based Dispatch]
            DispatchSelector --> |Debug Mode| TracingDispatch[Tracing Dispatch]
        end
        
        subgraph "Computed Goto Implementation"
            ComputedGoto --> GotoTable[Jump Table Generation]
            GotoTable --> DirectJump[Direct Label Jump]
            DirectJump --> FastExecution[Fast Execution Path]
            
            FastExecution --> ArithmeticOps[Arithmetic Operations]
            FastExecution --> MemoryOps[Memory Operations]
            FastExecution --> ControlOps[Control Flow Operations]
            FastExecution --> TypeOps[Type Operations]
        end
        
        subgraph "Switch Implementation"
            SwitchDispatch --> SwitchTable[Switch Statement]
            SwitchTable --> CaseHandlers[Case Handlers]
            CaseHandlers --> PortableExecution[Portable Execution]
            
            PortableExecution --> ArithmeticCases[Arithmetic Cases]
            PortableExecution --> MemoryCases[Memory Cases]
            PortableExecution --> ControlCases[Control Cases]
            PortableExecution --> TypeCases[Type Cases]
        end
        
        subgraph "Instruction Categories"
            ArithmeticOps --> AddOps[ADD_I32_R, ADD_F64_R, ...]
            ArithmeticOps --> MulOps[MUL_I32_R, MUL_F64_R, ...]
            ArithmeticOps --> DivOps[DIV_I32_R, DIV_F64_R, ...]
            
            MemoryOps --> LoadOps[LOAD_LOCAL, LOAD_GLOBAL, ...]
            MemoryOps --> StoreOps[STORE_LOCAL, STORE_GLOBAL, ...]
            MemoryOps --> MoveOps[MOVE, MOVE_WIDE, ...]
            
            ControlOps --> JumpOps[JUMP, JUMP_IF_R, ...]
            ControlOps --> CallOps[CALL_DIRECT, CALL_INDIRECT, ...]
            ControlOps --> ReturnOps[RETURN, RETURN_VOID, ...]
            
            TypeOps --> CastOps[CAST_I32_F64, CAST_F64_I32, ...]
            TypeOps --> CheckOps[TYPE_CHECK, INSTANCE_OF, ...]
        end
        
        subgraph "Performance Optimizations"
            FastExecution --> InstructionPrefetch[Instruction Prefetching]
            FastExecution --> RegisterCaching[Register Caching]
            FastExecution --> BranchPrediction[Branch Prediction Hints]
            
            InstructionPrefetch --> CacheOptimized[Cache-Optimized Layout]
            RegisterCaching --> RegisterReuse[Register Reuse Analysis]
            BranchPrediction --> ProfileGuided[Profile-Guided Optimization]
        end
    end
```

## Memory Management Architecture

```mermaid
graph TB
    subgraph "Memory Management System"
        subgraph "Allocation Strategies"
            AllocationRequest[Memory Allocation Request]
            AllocationRequest --> LifetimeAnalysis{Lifetime Analysis}
            
            LifetimeAnalysis --> |Short-lived, Predictable| ArenaAllocation[Arena Allocation]
            LifetimeAnalysis --> |Reusable Objects| ObjectPooling[Object Pooling]
            LifetimeAnalysis --> |General Purpose| HeapAllocation[Heap Allocation]
            LifetimeAnalysis --> |Large Objects| LargeObjectHeap[Large Object Heap]
        end
        
        subgraph "Arena System"
            ArenaAllocation --> TypeArena[Type Object Arena]
            ArenaAllocation --> ASTArena[AST Node Arena]
            ArenaAllocation --> StringArena[String Literal Arena]
            ArenaAllocation --> ConstantArena[Constant Pool Arena]
            
            TypeArena --> BumpAllocator[Bump Pointer Allocator]
            ASTArena --> BumpAllocator
            StringArena --> BumpAllocator
            ConstantArena --> BumpAllocator
            
            BumpAllocator --> FastAllocation[O(1) Allocation]
            BumpAllocator --> BulkDeallocation[Bulk Deallocation]
        end
        
        subgraph "Object Pool System"
            ObjectPooling --> StringPool[String Object Pool]
            ObjectPooling --> ArrayPool[Array Object Pool]
            ObjectPooling --> FunctionPool[Function Object Pool]
            ObjectPooling --> ClosurePool[Closure Object Pool]
            
            StringPool --> FreeList[Free List Management]
            ArrayPool --> FreeList
            FunctionPool --> FreeList
            ClosurePool --> FreeList
            
            FreeList --> ReuseAnalysis[Object Reuse Analysis]
            FreeList --> SizeClassification[Size Classification]
        end
        
        subgraph "Heap Management"
            HeapAllocation --> YoungGeneration[Young Generation]
            HeapAllocation --> OldGeneration[Old Generation]
            HeapAllocation --> PermanentGeneration[Permanent Generation]
            
            YoungGeneration --> MinorGC[Minor GC (Frequent)]
            OldGeneration --> MajorGC[Major GC (Infrequent)]
            PermanentGeneration --> FullGC[Full GC (Rare)]
        end
        
        subgraph "Garbage Collection"
            GCTrigger[GC Trigger]
            GCTrigger --> AllocationThreshold[Allocation Threshold]
            GCTrigger --> MemoryPressure[Memory Pressure]
            GCTrigger --> ExplicitRequest[Explicit GC Request]
            
            GCTrigger --> GCPhases[GC Execution Phases]
            
            GCPhases --> MarkPhase[Mark Phase]
            GCPhases --> SweepPhase[Sweep Phase]
            GCPhases --> CompactPhase[Compact Phase (Optional)]
            
            MarkPhase --> RootScanning[Root Set Scanning]
            MarkPhase --> TransitiveClosure[Transitive Closure]
            
            RootScanning --> StackRoots[Stack References]
            RootScanning --> RegisterRoots[Register References]
            RootScanning --> GlobalRoots[Global References]
            
            SweepPhase --> FreeListUpdate[Free List Update]
            SweepPhase --> MemoryReclamation[Memory Reclamation]
            
            CompactPhase --> PointerUpdate[Pointer Update]
            CompactPhase --> MemoryDefragmentation[Memory Defragmentation]
        end
        
        subgraph "Memory Metrics"
            MemoryMonitoring[Memory Monitoring]
            MemoryMonitoring --> AllocationRate[Allocation Rate Tracking]
            MemoryMonitoring --> GCFrequency[GC Frequency Analysis]
            MemoryMonitoring --> MemoryFragmentation[Fragmentation Metrics]
            MemoryMonitoring --> LifetimeAnalysisMetrics[Object Lifetime Metrics]
            
            AllocationRate --> PerformanceTuning[Performance Tuning]
            GCFrequency --> PerformanceTuning
            MemoryFragmentation --> PerformanceTuning
            LifetimeAnalysisMetrics --> PerformanceTuning
        end
    end
```

## Error Handling System Flow

```mermaid
flowchart TD
    subgraph "Error Detection Sources"
        LexerError[Lexer Errors]
        ParserError[Parser Errors]
        TypeError[Type Errors]
        RuntimeError[Runtime Errors]
        SystemError[System Errors]
    end
    
    subgraph "Error Classification"
        LexerError --> SyntaxCategory[Syntax Category]
        ParserError --> SyntaxCategory
        TypeError --> TypeCategory[Type Category]
        RuntimeError --> RuntimeCategory[Runtime Category]
        SystemError --> SystemCategory[System Category]
    end
    
    subgraph "Feature-Based Error Handling"
        SyntaxCategory --> SyntaxHandler[Syntax Error Handler]
        TypeCategory --> TypeHandler[Type Error Handler]
        RuntimeCategory --> RuntimeHandler[Runtime Error Handler]
        SystemCategory --> SystemHandler[System Error Handler]
        
        SyntaxHandler --> SyntaxRecovery[Syntax Error Recovery]
        TypeHandler --> TypeInference[Type Error Analysis]
        RuntimeHandler --> StackTrace[Stack Trace Generation]
        SystemHandler --> SystemDiagnostics[System Diagnostics]
    end
    
    subgraph "Error Message Generation"
        SyntaxRecovery --> MessageFormatter[Error Message Formatter]
        TypeInference --> MessageFormatter
        StackTrace --> MessageFormatter
        SystemDiagnostics --> MessageFormatter
        
        MessageFormatter --> ContextExtraction[Source Context Extraction]
        MessageFormatter --> HelpGeneration[Help Text Generation]
        MessageFormatter --> ExampleGeneration[Example Generation]
        
        ContextExtraction --> UserMessage[User-Friendly Message]
        HelpGeneration --> UserMessage
        ExampleGeneration --> UserMessage
    end
    
    subgraph "Error Output"
        UserMessage --> ConsoleOutput[Console Output]
        UserMessage --> IDEIntegration[IDE Integration]
        UserMessage --> LoggingSystem[Logging System]
        
        ConsoleOutput --> ColorFormatting[Color Formatting]
        ConsoleOutput --> UnicodeSupport[Unicode Support]
        
        IDEIntegration --> LSPProtocol[Language Server Protocol]
        IDEIntegration --> ErrorHighlighting[Error Highlighting]
        
        LoggingSystem --> ErrorAggregation[Error Aggregation]
        LoggingSystem --> AnalyticsReporting[Analytics Reporting]
    end
    
    subgraph "Error Recovery Strategies"
        SyntaxRecovery --> PanicMode[Panic Mode Recovery]
        SyntaxRecovery --> SynchronizationPoints[Synchronization Points]
        SyntaxRecovery --> ErrorProductions[Error Productions]
        
        PanicMode --> TokenSkipping[Token Skipping]
        SynchronizationPoints --> StatementBoundaries[Statement Boundaries]
        ErrorProductions --> PartialParsing[Partial Parsing]
        
        TokenSkipping --> ContinuedParsing[Continued Parsing]
        StatementBoundaries --> ContinuedParsing
        PartialParsing --> ContinuedParsing
    end
```

## Performance Optimization Layers

```mermaid
graph TB
    subgraph "Performance Optimization Stack"
        subgraph "Source Level Optimizations"
            SourceCode[Orus Source Code]
            SourceCode --> ConstantFolding[Constant Folding]
            SourceCode --> DeadCodeElimination[Dead Code Elimination]
            SourceCode --> LoopUnrolling[Loop Unrolling]
            SourceCode --> InlineExpansion[Inline Expansion]
        end
        
        subgraph "AST Level Optimizations"
            ConstantFolding --> ASTOptimizer[AST Optimizer]
            DeadCodeElimination --> ASTOptimizer
            LoopUnrolling --> ASTOptimizer
            InlineExpansion --> ASTOptimizer
            
            ASTOptimizer --> ExpressionSimplification[Expression Simplification]
            ASTOptimizer --> ControlFlowOptimization[Control Flow Optimization]
            ASTOptimizer --> TypeSpecialization[Type Specialization]
        end
        
        subgraph "IR Level Optimizations"
            ExpressionSimplification --> IROptimizer[IR Optimizer]
            ControlFlowOptimization --> IROptimizer
            TypeSpecialization --> IROptimizer
            
            IROptimizer --> LICM[Loop Invariant Code Motion]
            IROptimizer --> CommonSubexpression[Common Subexpression Elimination]
            IROptimizer --> TailCallOptimization[Tail Call Optimization]
            IROptimizer --> RegisterPromotion[Register Promotion]
        end
        
        subgraph "Register Allocation"
            LICM --> RegisterAllocator[Advanced Register Allocator]
            CommonSubexpression --> RegisterAllocator
            TailCallOptimization --> RegisterAllocator
            RegisterPromotion --> RegisterAllocator
            
            RegisterAllocator --> LivenessAnalysis[Liveness Analysis]
            RegisterAllocator --> InterferenceGraph[Interference Graph]
            RegisterAllocator --> ColoringAlgorithm[Graph Coloring]
            RegisterAllocator --> SpillCode[Spill Code Generation]
        end
        
        subgraph "Bytecode Generation"
            LivenessAnalysis --> BytecodeGenerator[Bytecode Generator]
            InterferenceGraph --> BytecodeGenerator
            ColoringAlgorithm --> BytecodeGenerator
            SpillCode --> BytecodeGenerator
            
            BytecodeGenerator --> TypeSpecificOpcodes[Type-Specific Opcodes]
            BytecodeGenerator --> OptimizedInstructions[Optimized Instructions]
            BytecodeGenerator --> EfficientEncoding[Efficient Encoding]
        end
        
        subgraph "VM Runtime Optimizations"
            TypeSpecificOpcodes --> VMOptimizer[VM Runtime Optimizer]
            OptimizedInstructions --> VMOptimizer
            EfficientEncoding --> VMOptimizer
            
            VMOptimizer --> ComputedGoto[Computed Goto Dispatch]
            VMOptimizer --> InstructionPrefetch[Instruction Prefetching]
            VMOptimizer --> RegisterCaching[Register Caching]
            VMOptimizer --> BranchPrediction[Branch Prediction]
        end
        
        subgraph "Hardware Level Optimizations"
            ComputedGoto --> HardwareOptimizer[Hardware Optimizer]
            InstructionPrefetch --> HardwareOptimizer
            RegisterCaching --> HardwareOptimizer
            BranchPrediction --> HardwareOptimizer
            
            HardwareOptimizer --> SIMDInstructions[SIMD Instructions]
            HardwareOptimizer --> CacheOptimization[Cache Optimization]
            HardwareOptimizer --> VectorizationHints[Vectorization Hints]
            HardwareOptimizer --> ArchitectureSpecific[Architecture-Specific Tuning]
        end
    end
    
    subgraph "Performance Monitoring"
        PerformanceProfiler[Performance Profiler]
        PerformanceProfiler --> InstructionCounting[Instruction Counting]
        PerformanceProfiler --> TimingAnalysis[Timing Analysis]
        PerformanceProfiler --> MemoryProfiling[Memory Profiling]
        PerformanceProfiler --> CacheAnalysis[Cache Miss Analysis]
        
        InstructionCounting --> Hotspots[Hotspot Identification]
        TimingAnalysis --> Hotspots
        MemoryProfiling --> Hotspots
        CacheAnalysis --> Hotspots
        
        Hotspots --> OptimizationFeedback[Optimization Feedback Loop]
        OptimizationFeedback --> ProfileGuidedOptimization[Profile-Guided Optimization]
    end
```

## Build System Architecture

```mermaid
graph TB
    subgraph "Build System Components"
        subgraph "Source Management"
            SourceFiles[Source Files (.c, .h)]
            SourceFiles --> DependencyAnalysis[Dependency Analysis]
            DependencyAnalysis --> BuildOrder[Build Order Determination]
            BuildOrder --> IncrementalBuild[Incremental Build Support]
        end
        
        subgraph "Profile Management"
            BuildProfile{Build Profile Selection}
            
            BuildProfile --> DebugProfile[Debug Profile]
            BuildProfile --> ReleaseProfile[Release Profile]
            BuildProfile --> ProfilingProfile[Profiling Profile]
            BuildProfile --> CIProfile[CI Profile]
            
            DebugProfile --> DebugFlags[-O0 -g3 -DDEBUG]
            ReleaseProfile --> ReleaseFlags[-O3 -DNDEBUG -flto]
            ProfilingProfile --> ProfilingFlags[-O2 -g -pg]
            CIProfile --> CIFlags[-O2 -Werror -Wall]
        end
        
        subgraph "Architecture Detection"
            ArchDetection[Architecture Detection]
            ArchDetection --> AppleSilicon[Apple Silicon (M1/M2/M3)]
            ArchDetection --> Intel[Intel x86_64]
            ArchDetection --> ARM[ARM (Linux)]
            ArchDetection --> OtherArch[Other Architectures]
            
            AppleSilicon --> AppleFlags[-mcpu=apple-m1 -mtune=apple-m1]
            Intel --> IntelFlags[-march=native -mtune=native]
            ARM --> ARMFlags[-march=armv8-a]
            OtherArch --> GenericFlags[-march=native]
        end
        
        subgraph "Feature Detection"
            FeatureDetection[Feature Detection]
            FeatureDetection --> ComputedGoto[Computed Goto Support]
            FeatureDetection --> SIMDSupport[SIMD Support]
            FeatureDetection --> LTOSupport[Link-Time Optimization]
            FeatureDetection --> SanitizerSupport[Sanitizer Support]
            
            ComputedGoto --> GotoFlags[-DCOMPUTED_GOTO_DISPATCH]
            SIMDSupport --> SIMDFlags[-DSIMD_OPTIMIZATIONS]
            LTOSupport --> LTOFlags[-flto]
            SanitizerSupport --> SanitizerFlags[-fsanitize=address]
        end
        
        subgraph "Cross-Compilation"
            CrossCompilation[Cross-Compilation Support]
            CrossCompilation --> LinuxTarget[Linux Target]
            CrossCompilation --> WindowsTarget[Windows Target (MinGW)]
            CrossCompilation --> MacOSTarget[macOS Target]
            CrossCompilation --> EmbeddedTarget[Embedded Targets]
            
            LinuxTarget --> LinuxToolchain[Linux Toolchain]
            WindowsTarget --> MinGWToolchain[MinGW Toolchain]
            MacOSTarget --> ClangToolchain[Clang Toolchain]
            EmbeddedTarget --> GCCToolchain[GCC ARM Toolchain]
        end
        
        subgraph "Quality Assurance"
            QualityTools[Quality Assurance Tools]
            QualityTools --> StaticAnalysis[Static Analysis]
            QualityTools --> DynamicAnalysis[Dynamic Analysis]
            QualityTools --> CodeCoverage[Code Coverage]
            QualityTools --> PerformanceTesting[Performance Testing]
            
            StaticAnalysis --> CppCheck[cppcheck]
            StaticAnalysis --> ClangAnalyzer[clang-analyzer]
            StaticAnalysis --> PCLint[PC-Lint Plus]
            
            DynamicAnalysis --> Valgrind[valgrind]
            DynamicAnalysis --> AddressSanitizer[AddressSanitizer]
            DynamicAnalysis --> MemorySanitizer[MemorySanitizer]
            
            CodeCoverage --> GCOV[gcov]
            CodeCoverage --> LLVMCOV[llvm-cov]
            
            PerformanceTesting --> BenchmarkSuite[Benchmark Suite]
            PerformanceTesting --> ProfilingTools[Profiling Tools]
        end
        
        subgraph "Output Generation"
            CompilationOutput[Compilation Output]
            CompilationOutput --> Executable[Orus Interpreter]
            CompilationOutput --> StaticLibrary[Static Library]
            CompilationOutput --> SharedLibrary[Shared Library]
            CompilationOutput --> DebugSymbols[Debug Symbols]
            
            Executable --> ExecutableStripping[Symbol Stripping (Release)]
            StaticLibrary --> LibraryOptimization[Library Optimization]
            SharedLibrary --> SharedLinking[Dynamic Linking]
            DebugSymbols --> SymbolGeneration[Debug Symbol Generation]
        end
    end
    
    subgraph "Build Automation"
        MakefileEngine[Makefile Engine]
        MakefileEngine --> ParallelBuild[Parallel Build Support]
        MakefileEngine --> DependencyTracking[Dependency Tracking]
        MakefileEngine --> ChangeDetection[Change Detection]
        MakefileEngine --> CacheManagement[Build Cache Management]
        
        ParallelBuild --> CPUUtilization[CPU Utilization Optimization]
        DependencyTracking --> MinimalRebuild[Minimal Rebuild]
        ChangeDetection --> TimestampComparison[Timestamp Comparison]
        CacheManagement --> BuildArtifacts[Build Artifact Caching]
    end
```

These architectural diagrams provide comprehensive visual documentation of the Orus programming language implementation, covering all major subsystems in detail.